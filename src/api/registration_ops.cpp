// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Registrations: making the phone's HID devices match the open Nodes. A Device holds
 * registrations; each has the Nodes that share one AOA HID ID. Applying one drains its Nodes,
 * unregisters the HID ID the phone has (request 55) and registers the joined descriptor under a
 * new one (requests 54 and 56). Registrations are applied one at a time because AOA registers
 * HID IDs independently (docs/PROTOCOL.md).
 */

#include "api/support.hpp"

#include "profiles/hid_layer.hpp"

#include <algorithm>
#include <chrono>

using aoa::detail::allocate_hid_id;
using aoa::detail::check_descriptor_fits_device;
using aoa::detail::finish_transport_result;
using aoa::detail::JoinedDescriptor;
using aoa::detail::JoinRefusal;
using aoa::detail::make_neutral;
using aoa::detail::poll_until_idle;
using aoa::detail::remaining_milliseconds;
using aoa::detail::set_error;

namespace aoa::detail {

namespace {

// The members have no HID device on the phone any more.
void forget_applied(Registration* registration) noexcept {
    registration->applied_hid_id = 0U;
    for (aoahid_node* member : registration->members) {
        member->hid_id = 0U;
    }
}

void mark_clean(aoahid_device* device, Registration* registration) noexcept {
    registration->stale = false;
    auto& order = device->stale_order;
    order.erase(std::remove(order.begin(), order.end(), registration), order.end());
}

aoahid_result unregister_one(aoahid_device* device, const std::uint16_t hid_id,
                             const std::uint32_t control_timeout_ms) noexcept {
    const aoahid_result unregister =
        device->transport->unregister_hid(hid_id, control_timeout_ms);
    if (unregister == AOAHID_OK || unregister == AOAHID_ERR_NO_DEVICE) {
        return AOAHID_OK;
    }
    static_cast<void>(finish_transport_result(unregister, "node.unregister",
                                              "AOA request 55 failed while unregistering a HID.",
                                              nullptr, 55));
    return unregister;
}

} // namespace

Registration* find_registration(aoahid_device* device, const std::uint32_t number) noexcept {
    for (const auto& registration : device->registrations) {
        if (registration->number == number) {
            return registration.get();
        }
    }
    return nullptr;
}

void mark_stale(aoahid_device* device, Registration* registration) {
    if (!registration->stale) {
        device->stale_order.push_back(registration);
        registration->stale = true;
    }
}

aoahid_result check_registration(const aoahid_device* device,
                                 const Registration* registration) {
    if (registration->members.empty()) {
        return AOAHID_OK;
    }
    std::vector<const aoahid_spec*> specs;
    specs.reserve(registration->members.size());
    for (const aoahid_node* member : registration->members) {
        specs.push_back(member->spec);
    }
    JoinedDescriptor joined;
    JoinRefusal refusal;
    const aoahid_result result = join_descriptors(specs, &joined, &refusal);
    if (result != AOAHID_OK) {
        set_error(refusal.result, refusal.field, refusal.reason, 0, 0, 0, refusal.report_id);
        return result;
    }
    return specs.size() == 1U
               ? AOAHID_OK
               : check_descriptor_fits_device(device, joined.bytes.size(), joined.requirements);
}

aoahid_result apply_registration(aoahid_device* device, Registration* registration,
                                 const aoahid_commit_timeouts& timeouts) {
    aoa::transport::Device* transport = device->transport;
    // Until it succeeds the registration is out of step with the phone: a failure leaves it queued.
    mark_stale(device, registration);
    if (!transport->present()) {
        // Nothing can be registered or unregistered on a phone that is gone; the Nodes stay as
        // they are, frozen, and their reports say so.
        mark_clean(device, registration);
        return AOAHID_OK;
    }

    // 1. Compose: no I/O. A refusal changes nothing on the phone.
    JoinedDescriptor joined;
    if (!registration->members.empty()) {
        std::vector<const aoahid_spec*> specs;
        specs.reserve(registration->members.size());
        for (const aoahid_node* member : registration->members) {
            specs.push_back(member->spec);
        }
        JoinRefusal refusal;
        const aoahid_result composed = join_descriptors(specs, &joined, &refusal);
        if (composed != AOAHID_OK) {
            set_error(refusal.result, refusal.field, refusal.reason, 0, 0, 0, refusal.report_id);
            return composed;
        }
        if (specs.size() > 1U) {
            const aoahid_result fits =
                check_descriptor_fits_device(device, joined.bytes.size(), joined.requirements);
            if (fits != AOAHID_OK) {
                return fits;
            }
        }
    }

    // 2. Drain: the Nodes the phone has get their neutral report within one shared deadline.
    // The HID device they were reporting to is about to go.
    if (registration->applied_hid_id != 0U) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeouts.drain_timeout_ms);
        for (aoahid_node* member : registration->members) {
            if (member->hid_id == 0U) {
                continue;
            }
            make_neutral(member);
            const aoahid_result drained = poll_until_idle(
                member, std::max<std::uint32_t>(remaining_milliseconds(deadline), 1U), true);
            if (drained != AOAHID_OK) {
                return drained;
            }
        }
    }

    // 3. Unregister: an ID owed from an earlier failure first, then the one the phone has.
    if (registration->owed_hid_id != 0U) {
        const aoahid_result owed =
            unregister_one(device, registration->owed_hid_id, timeouts.control_timeout_ms);
        if (owed != AOAHID_OK) {
            return owed;
        }
        registration->owed_hid_id = 0U;
    }
    if (registration->applied_hid_id != 0U) {
        const std::uint16_t applied = registration->applied_hid_id;
        forget_applied(registration);
        const aoahid_result unregister = unregister_one(device, applied, timeouts.control_timeout_ms);
        if (unregister != AOAHID_OK) {
            // The phone may still have it: the next apply sends request 55 again first.
            registration->owed_hid_id = applied;
            return unregister;
        }
    }
    if (registration->members.empty()) {
        mark_clean(device, registration);
        return AOAHID_OK;
    }

    // 4. Register the joined descriptor under a new HID ID.
    std::uint16_t hid_id = 0U;
    const aoahid_result allocated = allocate_hid_id(device, &hid_id);
    if (allocated != AOAHID_OK) {
        return allocated;
    }
    const aoahid_node* first = registration->members.front();
    const aoahid_result registered = transport->register_hid(
        hid_id, joined.bytes.data(), joined.bytes.size(), timeouts.control_timeout_ms);
    if (registered != AOAHID_OK) {
        aoa::detail::log_event(
            device->context, AOAHID_LOG_ERROR, "node_register", device, first,
            aoa::detail::LogEventStatus{registered, 0, 54, 0U,
                                        static_cast<std::uint32_t>(joined.bytes.size())});
        static_cast<void>(finish_transport_result(
            registered, "node.register", "AOA HID registration or descriptor transfer failed.",
            nullptr, 54, 0U, static_cast<std::uint32_t>(joined.bytes.size())));
        return registered;
    }
    registration->applied_hid_id = hid_id;
    for (aoahid_node* member : registration->members) {
        member->hid_id = hid_id;
        // The new HID device has never seen a battery level.
        member->battery_dirty = member->battery.has_value;
    }
    aoa::detail::log_event(device->context, AOAHID_LOG_INFO, "node_register", device, first,
                           aoa::detail::LogEventStatus{AOAHID_OK, 0, 54, 0U,
                                                       static_cast<std::uint32_t>(
                                                           joined.bytes.size())});
    mark_clean(device, registration);
    return AOAHID_OK;
}

aoahid_result commit_device(aoahid_device* device, const aoahid_commit_timeouts& timeouts) {
    aoahid_result first_result = AOAHID_OK;
    DeferredError first_error{};
    // apply_registration edits stale_order, so walk a copy.
    std::vector<Registration*> order = device->stale_order;
    for (const auto& registration : device->registrations) {
        // One that could not be queued (an allocation failed) is still stale.
        if (registration->stale &&
            std::find(order.begin(), order.end(), registration.get()) == order.end()) {
            order.push_back(registration.get());
        }
    }
    for (Registration* registration : order) {
        const aoahid_result result = apply_registration(device, registration, timeouts);
        if (result != AOAHID_OK && first_result == AOAHID_OK) {
            first_result = result;
            first_error = current_deferred_error(result, "device.commit",
                                                 "A registration could not be applied.");
        }
    }
    forget_empty_registrations(device);
    return first_result == AOAHID_OK ? AOAHID_OK : publish_deferred_error(first_error);
}

void forget_empty_registrations(aoahid_device* device) noexcept {
    auto& all = device->registrations;
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const std::unique_ptr<Registration>& registration) {
                                 return registration->members.empty() &&
                                        registration->applied_hid_id == 0U &&
                                        registration->owed_hid_id == 0U && !registration->stale;
                             }),
              all.end());
}

} // namespace aoa::detail
