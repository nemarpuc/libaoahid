// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: opening and closing Nodes.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>

using aoa::detail::apply_registration;
using aoa::detail::check_commit_timeouts;
using aoa::detail::check_registration;
using aoa::detail::check_reservation;
using aoa::detail::check_spec_fits_device;
using aoa::detail::close_live_node_until;
using aoa::detail::find_registration;
using aoa::detail::finish_transport_result;
using aoa::detail::forget_empty_registrations;
using aoa::detail::initialize_node_state;
using aoa::detail::preflight_context;
using aoa::detail::set_error;
using aoa::detail::timeout_is_set;

extern "C" {

aoahid_result AOAHID_CALL aoahid_node_open(aoahid_device* device, aoahid_spec* spec,
                                           const aoahid_node_placement* placement,
                                           const aoahid_commit_timeouts* timeouts,
                                           aoahid_node** out_node) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(device), "aoahid_node_open");
    aoa::detail::clear_error();
    if (out_node == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_node", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_node = nullptr;
    if (device == nullptr || device->transport == nullptr || spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.open", "A live Device and immutable Spec are required.");
        return AOAHID_ERR_PARAM;
    }
    if (device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "device",
                  "The Device handle was consumed by close and cannot open a Node.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result checked_timeouts = check_commit_timeouts(timeouts);
    if (checked_timeouts != AOAHID_OK)
        return checked_timeouts;
    const aoahid_result deferred = preflight_context(device->context);
    if (deferred != AOAHID_OK)
        return deferred;
    if (!device->transport->present()) {
        set_error(AOAHID_ERR_NO_DEVICE, "node.open", "The device is gone.");
        return AOAHID_ERR_NO_DEVICE;
    }
    if (!aoa::detail::valid_struct(placement, placement == nullptr ? 0U : placement->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*placement)),
                                   "node_placement")) {
        return AOAHID_ERR_PARAM;
    }
    if (placement->registration == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "node.registration",
                  "A nonzero registration number is required.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    const aoahid_result reservation = check_reservation(device, *placement);
    if (reservation != AOAHID_OK)
        return reservation;
    const aoahid_result fits = check_spec_fits_device(device, spec);
    if (fits != AOAHID_OK)
        return fits;
    if (device->next_node_id == 0U) {
        set_error(AOAHID_ERR_OVERFLOW, "node.id", "The Device has no Node number left.");
        return AOAHID_ERR_OVERFLOW;
    }

    // Everything that can allocate happens before the first request. After Android accepts a
    // registration, publishing the Node into these pre-reserved vectors cannot allocate or orphan
    // it.
    aoa::detail::Registration* registration = find_registration(device, placement->registration);
    const bool new_registration = registration == nullptr;
    if (new_registration) {
        auto created = std::make_unique<aoa::detail::Registration>();
        created->number = placement->registration;
        registration = created.get();
        device->registrations.push_back(std::move(created));
    }
    const bool was_stale = registration->stale;
    const std::uint16_t previous_hid_id = registration->applied_hid_id;

    auto node = std::unique_ptr<aoahid_node>(new (std::nothrow) aoahid_node());
    if (node == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "node", "Memory allocation for the Node failed.");
        forget_empty_registrations(device);
        return AOAHID_ERR_INTERNAL;
    }
    bool retained = false;
    bool reservation_installed = false;
    bool joined = false;
    const auto undo = [&]() noexcept {
        if (joined) {
            auto& members = registration->members;
            members.erase(std::remove(members.begin(), members.end(), node.get()), members.end());
            joined = false;
        }
        if (reservation_installed) {
            static_cast<void>(device->transport->clear_reservation(node->reservation));
            reservation_installed = false;
        }
        if (retained) {
            static_cast<void>(aoa::detail::release_spec(spec));
            retained = false;
        }
        forget_empty_registrations(device);
    };
    try {
        node->device = device;
        node->spec = spec;
        node->registration = registration;
        node->active_completion_mutex =
            device->context->options.event_mode == AOAHID_EVENT_INTERNAL_THREAD
                ? &node->completion_mutex
                : nullptr;
        node->reserved_slots = placement->reserve_slot;
        const aoahid_result initialized = initialize_node_state(node.get());
        if (initialized != AOAHID_OK) {
            undo();
            return initialized;
        }
        device->nodes.reserve(device->nodes.size() + 1U);
        registration->members.reserve(registration->members.size() + 1U);
        device->stale_order.reserve(device->stale_order.size() + 1U);
        if (!aoa::detail::retain_spec(spec)) {
            set_error(AOAHID_ERR_OVERFLOW, "spec.references",
                      "The Spec reference count cannot be incremented for this Node.");
            undo();
            return AOAHID_ERR_OVERFLOW;
        }
        retained = true;
        const aoahid_result reserved_slots =
            device->transport->set_reservation(node->reserved_slots, &node->reservation);
        if (reserved_slots != AOAHID_OK) {
            static_cast<void>(finish_transport_result(
                reserved_slots, "node.reserve_slot",
                "The requested transfer-pool reservation could not be installed.", node.get()));
            undo();
            return reserved_slots;
        }
        reservation_installed = true;
        registration->members.push_back(node.get());
        joined = true;
        // The checks that need no I/O, so a staged Node is refused now and not at batch end.
        const aoahid_result allowed = check_registration(device, registration);
        if (allowed != AOAHID_OK) {
            undo();
            return allowed;
        }
    } catch (...) {
        undo();
        throw;
    }

    // apply_registration allocates while composing; a failure there is an error like any other,
    // and must not leave the Node in the registration's members.
    const auto apply = [&]() noexcept -> aoahid_result {
        try {
            return apply_registration(device, registration, *timeouts);
        } catch (...) {
            registration->stale = true;
            set_error(AOAHID_ERR_INTERNAL, "node.open", "Memory allocation failed.");
            return AOAHID_ERR_INTERNAL;
        }
    };
    if (device->in_batch) {
        try {
            aoa::detail::mark_stale(device, registration);
        } catch (...) {
            registration->stale = true; // the capacity was reserved above; kept for commit
        }
    } else {
        const aoahid_result applied = apply();
        if (applied != AOAHID_OK) {
            // Keep the error of the step that failed while the registration is put back.
            const aoa::detail::DeferredError failure = aoa::detail::current_deferred_error(
                applied, "node.open", "A registration step failed.");
            registration->members.erase(
                std::remove(registration->members.begin(), registration->members.end(), node.get()),
                registration->members.end());
            joined = false;
            if (!registration->members.empty() && registration->applied_hid_id != previous_hid_id) {
                // The old HID device went away with the failed step: register the old Nodes again.
                static_cast<void>(apply());
            } else {
                registration->stale = was_stale;
                if (!was_stale) {
                    auto& order = device->stale_order;
                    order.erase(std::remove(order.begin(), order.end(), registration), order.end());
                }
            }
            undo();
            return aoa::detail::publish_deferred_error(failure);
        }
    }

    // Capacity was established above and the elements are raw pointers, so this publication
    // performs neither allocation nor user code.
    node->node_id = device->next_node_id++;
    device->nodes.push_back(node.get());
    *out_node = node.release();
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("node.open")

std::uint16_t AOAHID_CALL aoahid_node_id(const aoahid_node* node) try {
    aoa::detail::clear_error();
    if (node == nullptr || node->closed) {
        set_error(AOAHID_ERR_PARAM, "node",
                  "The Node handle is null or closed; zero is the failure sentinel.");
        return 0U;
    }
    return node->node_id;
}
AOAHID_C_VALUE_CATCH("node.id", 0U)

aoahid_result AOAHID_CALL aoahid_node_manifest(const aoahid_node* node,
                                               aoahid_capability_manifest* manifest) try {
    aoa::detail::clear_error();
    if (node == nullptr || node->closed || node->spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.manifest", "A live node is required.");
        return AOAHID_ERR_PARAM;
    }
    return aoahid_spec_manifest(node->spec, manifest);
}
AOAHID_C_RESULT_CATCH("node.manifest")

aoahid_result AOAHID_CALL aoahid_node_close(aoahid_node* node,
                                            const aoahid_commit_timeouts* timeouts) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(node), "aoahid_node_close");
    aoa::detail::clear_error();
    const aoahid_result checked_timeouts = check_commit_timeouts(timeouts);
    if (checked_timeouts != AOAHID_OK)
        return checked_timeouts;
    if (node == nullptr || node->closed || node->device == nullptr ||
        node->device->transport == nullptr ||
        node->device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "node.close",
                  "The Node handle is null, closed, detached, or owned by a closing Device.");
        return AOAHID_ERR_PARAM;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeouts->drain_timeout_ms);
    return close_live_node_until(node, deadline, *timeouts);
}
AOAHID_C_RESULT_CATCH("node.close")

} // extern "C"
