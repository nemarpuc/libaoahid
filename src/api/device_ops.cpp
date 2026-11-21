// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Device-side internals of the C ABI: the teardown of a Device and its children, the first-error
 * latch of a close, and the release of Node storage.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <new>
#include <thread>

using aoa::detail::current_deferred_error;
using aoa::detail::DeferredErrorInput;
using aoa::detail::drain_node_for_close;
using aoa::detail::finish_transport_result;
using aoa::detail::make_deferred_error;
using aoa::detail::park_in_graveyard;
using aoa::detail::publish_deferred_error;
using aoa::detail::remaining_milliseconds;
using aoa::detail::set_error;

namespace aoa::detail {

void remember_close_error(aoahid_device* device, const aoa::detail::DeferredError& error) noexcept {
    if (device == nullptr || !error.pending || error.transport.result == AOAHID_OK ||
        device->close_error.pending) {
        return;
    }
    device->close_error = error;
    device->close_result = error.transport.result;
}

void remember_current_close_error(aoahid_device* device, const aoahid_result result,
                                  const char* field, const char* reason, const aoahid_node* node,
                                  const std::int32_t fallback_request,
                                  const std::uint32_t fallback_offset,
                                  const std::uint32_t fallback_length) noexcept {
    if (result == AOAHID_OK || result == AOAHID_CLOSE_PENDING)
        return;
    remember_close_error(device,
                         current_deferred_error(result, field, reason, node, fallback_request,
                                                fallback_offset, fallback_length));
}

void release_node_storage(aoahid_node* node) noexcept {
    if (node == nullptr) {
        return;
    }
    // Only internal-thread mode can race Node teardown against its callback.
    // Caller-poll callbacks execute inside the caller's synchronization domain,
    // so its null active mutex deliberately keeps teardown lock-free too.
    { const aoa::detail::ModeMutexGuard callback_exit(node->active_completion_mutex); }
    node->closed = true;
    static_cast<void>(aoa::detail::release_spec(node->spec));
    node->spec = nullptr;
    node->device = nullptr;
    delete node;
}

void remember_terminal_completion(aoahid_device* device, aoahid_node* node) noexcept {
    if (device == nullptr || node == nullptr)
        return;
    const aoahid_result result =
        node->completion_result.exchange(AOAHID_OK, std::memory_order_acq_rel);
    const std::int32_t native_status =
        node->completion_native_status.exchange(0, std::memory_order_relaxed);
    const std::uint32_t length =
        node->completion_report_length.exchange(0U, std::memory_order_relaxed);
    const std::uint16_t completed_report_id =
        node->completion_report_id.exchange(0U, std::memory_order_relaxed);
    if (result == AOAHID_OK || aoa::transport::is_cancelled_completion_status(native_status)) {
        return;
    }
    remember_close_error(
        device,
        make_deferred_error(DeferredErrorInput{
            aoa::transport::ErrorInfo{result, native_status, 57, node->hid_id, 0U, length},
            "node.completion", completion_reason(result), completed_report_id, node->node_id}));
}

// A Device graph may be freed only after its own transfers and every child
// Channel's Bulk transfers have delivered their callbacks.
bool graph_drained(const aoahid_device* device) noexcept {
    if (device == nullptr || device->transport == nullptr || !device->transport->drained()) {
        return false;
    }
    return std::all_of(device->channels.begin(), device->channels.end(),
                       [](const aoahid_channel* channel) { return channel->transport->drained(); });
}

// libusb's WinUSB backend sends a device-recipient control transfer through a
// WinUSB interface it claims for that transfer and releases on completion. On
// a phone that interface is usually the ADB one a Channel wants, and the
// auto-release would undo a Channel claim made meanwhile (or a Channel release
// would abort the transfer). So a Channel claims and releases its interface
// only while no HID report transfer is in flight.
bool wait_for_report_drain(aoahid_device* device,
                           const std::chrono::steady_clock::time_point deadline,
                           aoahid_result* poll_error) noexcept {
    *poll_error = AOAHID_OK;
    while (!device->transport->drained()) {
        if (remaining_milliseconds(deadline) == 0U) {
            return false;
        }
        if (device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
            const aoahid_result poll = device->context->runtime->poll(1U);
            if (poll != AOAHID_OK) {
                *poll_error = poll;
                return false;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

void release_channel_storage(aoahid_channel* channel) noexcept {
    delete channel->transport;
    channel->transport = nullptr;
    channel->device = nullptr;
    delete channel;
}

aoa::detail::DeferredError destroy_drained_device(aoahid_device* device) noexcept {
    if (!graph_drained(device)) {
        return make_deferred_error(
            DeferredErrorInput{aoa::transport::ErrorInfo{AOAHID_CLOSE_PENDING, 0, 0, 0U, 0U, 0U},
                               "device.close", "Terminal transfer callbacks have not drained."});
    }

    // One request 55 for each HID ID the phone has, owed or applied, however many Nodes share it.
    if (device->transport->present()) {
        for (const auto& registration : device->registrations) {
            for (const std::uint16_t hid_id :
                 {registration->owed_hid_id, registration->applied_hid_id}) {
                if (hid_id == 0U) {
                    continue;
                }
                const aoahid_result unregister =
                    device->transport->unregister_hid(hid_id, device->close_control_timeout_ms);
                if (unregister != AOAHID_OK && unregister != AOAHID_ERR_NO_DEVICE) {
                    remember_current_close_error(device, unregister, "node.unregister",
                                                 "AOA request 55 failed during Device teardown.",
                                                 nullptr, 55);
                }
            }
        }
    }
    while (!device->nodes.empty()) {
        aoahid_node* node = device->nodes.back();
        device->nodes.pop_back();
        node->closed = true;
        remember_terminal_completion(device, node);
        const aoahid_result clear_reservation =
            device->transport->clear_reservation(node->reservation);
        remember_current_close_error(
            device, clear_reservation, "node.reserved_slots",
            "A transfer-pool reservation could not be released during Device teardown.", node);
        node->registration = nullptr;
        release_node_storage(node);
    }
    device->registrations.clear();
    device->stale_order.clear();

    for (aoahid_channel* channel : device->channels) {
        release_channel_storage(channel);
    }
    device->channels.clear();

    const aoa::detail::DeferredError outcome = device->close_error;
    const aoahid_result result = outcome.pending ? outcome.transport.result : AOAHID_OK;
    aoa::detail::log_event(device->context,
                           result == AOAHID_OK ? AOAHID_LOG_INFO : AOAHID_LOG_ERROR, "device_close",
                           device, nullptr, aoa::detail::LogEventStatus{result});
    delete device->transport;
    device->transport = nullptr;
    device->context = nullptr;
    delete device;
    return outcome;
}

aoahid_result device_close_impl(aoahid_device* device, bool* consumed,
                                // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                const std::uint32_t control_timeout_ms,
                                const std::uint32_t drain_timeout_ms) try {
    aoa::detail::clear_error();
    if (consumed == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "device.close",
                  "The internal Device ownership result pointer is null.");
        return AOAHID_ERR_INTERNAL;
    }
    *consumed = false;
    if (device == nullptr || device->transport == nullptr || device->context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "device.close",
                  "A live Device attached to its Context and transport is required.");
        return AOAHID_ERR_PARAM;
    }
    aoahid_context* context = device->context;
    // Device open reserves both Context vectors for every live graph. Moving
    // this pointer from devices to graveyard therefore cannot allocate after
    // the closing flag consumes the public handle.
    {
        const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
        bool expected = false;
        if (!device->closing.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
            set_error(AOAHID_ERR_PARAM, "device.close",
                      "The Device handle was already consumed by an earlier close call.");
            return AOAHID_ERR_PARAM;
        }
        auto& devices = context->devices;
        devices.erase(std::remove(devices.begin(), devices.end(), device), devices.end());
        *consumed = true;
        // The Context graveyard may finish this teardown after the call returns.
        device->close_control_timeout_ms = control_timeout_ms;
        device->close_drain_timeout_ms = drain_timeout_ms;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(device->close_drain_timeout_ms);
    try {
        // The Nodes' neutral reports go out here; their registrations are unregistered once each
        // when the Device is destroyed. A Node whose report is still in flight, and the ones not
        // drained yet, stay for the graveyard.
        for (auto node = device->nodes.rbegin(); node != device->nodes.rend(); ++node) {
            const aoahid_result result = drain_node_for_close(*node, deadline);
            if (result == AOAHID_CLOSE_PENDING)
                break;
            remember_current_close_error(device, result, "node.close",
                                         "A Node close step failed during Device teardown.");
        }
    } catch (const std::bad_alloc&) {
        remember_close_error(
            device,
            make_deferred_error(DeferredErrorInput{
                aoa::transport::ErrorInfo{AOAHID_ERR_INTERNAL, 0, 0, 0U, 0U, 0U}, "device.close",
                "Memory allocation failed after Device ownership was consumed; teardown "
                "continued."}));
    } catch (...) {
        remember_close_error(
            device,
            make_deferred_error(DeferredErrorInput{
                aoa::transport::ErrorInfo{AOAHID_ERR_INTERNAL, 0, 0, 0U, 0U, 0U}, "device.close",
                "An internal operation failed after Device ownership was consumed; teardown "
                "continued."}));
    }

    // Channels share the Device's handle; cancel them with its transfers.
    for (aoahid_channel* channel : device->channels) {
        channel->transport->lose();
    }
    const aoahid_result cancel = device->transport->cancel_all();
    if (cancel != AOAHID_OK && cancel != AOAHID_ERR_NO_DEVICE) {
        static_cast<void>(finish_transport_result(cancel, "device.cancel",
                                                  "libusb transfer cancellation failed."));
        remember_current_close_error(device, cancel, "device.cancel",
                                     "libusb transfer cancellation failed.", nullptr, 57);
    }
    for (aoahid_node* node : device->nodes) {
        node->closed = true;
    }
    if (!device->channels.empty()) {
        // Read-ahead IN transfers are always in flight; give their
        // cancellation the close budget (pumping in caller-poll mode) before
        // falling back to the graveyard. A Device without Channels skips this.
        const auto channel_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(device->close_drain_timeout_ms);
        while (!graph_drained(device) && remaining_milliseconds(channel_deadline) != 0U) {
            if (context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
                const aoahid_result poll = context->runtime->poll(1U);
                if (poll != AOAHID_OK) {
                    static_cast<void>(finish_transport_result(poll, "context.poll",
                                                              "libusb event handling failed."));
                    remember_current_close_error(device, poll, "context.poll",
                                                 "libusb event handling failed while Channel "
                                                 "transfers drained during Device teardown.");
                    break;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    if (context->options.event_mode == AOAHID_EVENT_INTERNAL_THREAD) {
        // A Node close returns as soon as its last completion has published,
        // while the event thread may still be returning from that callback.
        // Give it what is left of the drain budget instead of parking a Device
        // whose transfers have all completed.
        while (!graph_drained(device) && remaining_milliseconds(deadline) != 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (graph_drained(device)) {
        const aoa::detail::DeferredError outcome = destroy_drained_device(device);
        return publish_deferred_error(outcome);
    }

    // The Context finishes the teardown once the callbacks have run (nodes and retained Specs are
    // their user_data); aoahid_context_poll and aoahid_context_destroy report its errors.
    aoa::detail::log_event(context, AOAHID_LOG_INFO, "device_close_pending", device, nullptr,
                           aoa::detail::LogEventStatus{AOAHID_CLOSE_PENDING});
    aoa::detail::clear_error();
    // In internal-thread mode the event thread may reap the Device as soon as it is published.
    park_in_graveyard(context, device);
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("device.close")

} // namespace aoa::detail
