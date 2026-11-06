// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Node-side internals shared by the C ABI entry points: report state, neutralizing, and the
 * drain and teardown sequence of Nodes, Channels and Devices.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <utility>

using aoa::detail::close_live_node_until;
using aoa::detail::completion_reason;
using aoa::detail::consume_completion;
using aoa::detail::current_deferred_error;
using aoa::detail::DeferredErrorInput;
using aoa::detail::destroy_drained_device;
using aoa::detail::device_close_impl;
using aoa::detail::finish_transport_result;
using aoa::detail::graph_drained;
using aoa::detail::has_non_neutral_state;
using aoa::detail::initialize_node_state;
using aoa::detail::make_deferred_error;
using aoa::detail::park_in_graveyard;
using aoa::detail::poll_until_idle;
using aoa::detail::preflight_context;
using aoa::detail::publish_deferred_error;
using aoa::detail::release_channel_storage;
using aoa::detail::remaining_milliseconds;
using aoa::detail::report_id;
using aoa::detail::set_error;
using aoa::detail::wait_for_report_drain;

namespace {

// Each helper is deliberately noinline and touches exactly one NodeState
// alternative. This is a one-time close-path operation, never the per-report
// send path, so the call overhead is immaterial; keeping each alternative's
// vector access in its own non-inlined function also keeps GCC 14's
// -Wstringop-overflow analysis from unifying value ranges across unrelated
// sibling if/else-if arms of one large function, which produced a false
// positive at -O3 on musl. MSVC does not
// recognize the gnu:: attribute namespace and, under /WX, turns its own
// warning about that into a hard error, so this is spelled per compiler
// rather than as a portable [[gnu::noinline]] attribute.
#if defined(_MSC_VER) && !defined(__clang__)
#define AOAHID_NOINLINE __declspec(noinline)
#else
#define AOAHID_NOINLINE __attribute__((noinline))
#endif

AOAHID_NOINLINE bool neutralize_keyboard(aoa::detail::KeyboardState* keyboard) noexcept {
    const bool had_non_neutral_state =
        keyboard->modifiers != 0U || keyboard->pressed_bitmap_count != 0U;
    std::fill(keyboard->pressed_bitmap.begin(), keyboard->pressed_bitmap.end(), std::uint8_t{0});
    keyboard->pressed_bitmap_count = 0U;
    keyboard->modifiers = 0U;
    return had_non_neutral_state;
}

AOAHID_NOINLINE bool neutralize_mouse(aoa::detail::MouseState* mouse) noexcept {
    const bool had_non_neutral_state =
        std::any_of(mouse->buttons.begin(), mouse->buttons.end(),
                    [](const std::uint8_t value) { return value != 0U; });
    std::fill(mouse->buttons.begin(), mouse->buttons.end(), std::uint8_t{0});
    mouse->dx = mouse->dy = mouse->wheel = mouse->pan = 0;
    return had_non_neutral_state;
}

AOAHID_NOINLINE bool neutralize_gamepad(aoa::detail::GamepadState* gamepad,
                                        const aoa::detail::GamepadConfig* spec) noexcept {
    bool had_non_neutral_state = false;
    for (std::size_t index = 0U; index < gamepad->axes.size(); ++index) {
        had_non_neutral_state =
            had_non_neutral_state || gamepad->axes[index] != spec->axes[index].axis.neutral_value;
        gamepad->axes[index] = spec->axes[index].axis.neutral_value;
    }
    had_non_neutral_state = had_non_neutral_state || gamepad->hat_has_direction ||
                            std::any_of(gamepad->buttons.begin(), gamepad->buttons.end(),
                                        [](const std::uint8_t value) { return value != 0U; });
    std::fill(gamepad->buttons.begin(), gamepad->buttons.end(), std::uint8_t{0});
    gamepad->hat_has_direction = false;
    gamepad->hat = 0;
    return had_non_neutral_state;
}

AOAHID_NOINLINE bool neutralize_touch(aoa::detail::TouchState* touch) noexcept {
    bool had_non_neutral_state = false;
    for (auto& contact : touch->contacts) {
        had_non_neutral_state =
            had_non_neutral_state || contact.phase != aoa::detail::ContactPhase::none;
        if (contact.phase == aoa::detail::ContactPhase::down) {
            contact.phase = aoa::detail::ContactPhase::up;
        }
    }
    had_non_neutral_state =
        had_non_neutral_state || std::any_of(touch->buttons.begin(), touch->buttons.end(),
                                             [](const std::uint8_t value) { return value != 0U; });
    std::fill(touch->buttons.begin(), touch->buttons.end(), std::uint8_t{0});
    touch->packet_cursor = 0U;
    return had_non_neutral_state;
}

AOAHID_NOINLINE bool neutralize_pen(aoa::detail::PenState* pen) noexcept {
    const bool had_non_neutral_state = pen->desired.in_range != 0U || pen->desired.tip != 0U ||
                                       pen->desired.barrel_buttons != 0U ||
                                       pen->emitted.in_range != 0U || pen->emitted.tip != 0U ||
                                       pen->emitted.barrel_buttons != 0U;
    pen->desired.in_range = 0U;
    pen->desired.tip = 0U;
    pen->desired.barrel_buttons = 0U;
    pen->desired.pressure = 0;
    pen->tool_switch_departure = false;
    return had_non_neutral_state;
}

#undef AOAHID_NOINLINE

void make_neutral(aoahid_node* node) noexcept {
    bool had_non_neutral_state = false;
    if (auto* keyboard = std::get_if<aoa::detail::KeyboardState>(&node->state)) {
        had_non_neutral_state = neutralize_keyboard(keyboard);
    } else if (auto* mouse = std::get_if<aoa::detail::MouseState>(&node->state)) {
        had_non_neutral_state = neutralize_mouse(mouse);
    } else if (auto* consumer = std::get_if<aoa::detail::ConsumerState>(&node->state)) {
        had_non_neutral_state = std::any_of(consumer->pressed.begin(), consumer->pressed.end(),
                                            [](const std::uint8_t value) { return value != 0U; });
        std::fill(consumer->pressed.begin(), consumer->pressed.end(), std::uint8_t{0});
    } else if (auto* gamepad = std::get_if<aoa::detail::GamepadState>(&node->state)) {
        const auto* spec = std::get_if<aoa::detail::GamepadConfig>(&node->spec->config);
        had_non_neutral_state = neutralize_gamepad(gamepad, spec);
    } else if (auto* touch = std::get_if<aoa::detail::TouchState>(&node->state)) {
        had_non_neutral_state = neutralize_touch(touch);
    } else if (auto* pen = std::get_if<aoa::detail::PenState>(&node->state)) {
        had_non_neutral_state = neutralize_pen(pen);
    }
    node->dirty = (had_non_neutral_state || node->emitted_non_neutral) &&
                  node->spec->kind != AOAHID_PROFILE_RAW;
}

void remember_close_error(aoahid_device* device, const aoa::detail::DeferredError& error) noexcept {
    if (device == nullptr || !error.pending || error.transport.result == AOAHID_OK ||
        device->close_error.pending) {
        return;
    }
    device->close_error = error;
    device->close_result = error.transport.result;
}

void remember_current_close_error(aoahid_device* device, const aoahid_result result,
                                  const char* field, const char* reason,
                                  const aoahid_node* node = nullptr,
                                  const std::int32_t fallback_request = 0,
                                  const std::uint32_t fallback_offset = 0U,
                                  const std::uint32_t fallback_length = 0U) noexcept {
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

bool touch_frame_in_progress(const aoahid_node* node) noexcept {
    const auto* touch =
        node == nullptr ? nullptr : std::get_if<aoa::detail::TouchState>(&node->state);
    return touch != nullptr && touch->packet_cursor != 0U;
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
        device, make_deferred_error(DeferredErrorInput{
                    aoa::transport::ErrorInfo{result, native_status, 57, node->hid_id, 0U, length},
                    "node.completion", completion_reason(result), completed_report_id}));
}

} // namespace

namespace aoa::detail {

std::uint16_t report_id(const aoahid_node* node) noexcept {
    return node != nullptr && node->spec != nullptr && node->spec->layout.has_report_id
               ? node->spec->layout.report_id
               : 0U;
}

const char* completion_reason(const aoahid_result result) noexcept {
    switch (result) {
    case AOAHID_ERR_STALL:
        return "The device refused AOA request 57 (STALL); nothing was applied and the state "
               "stays pending for a resend. Right after aoahid_node_open this usually means "
               "Android has not finished registering the HID.";
    case AOAHID_ERR_TIMEOUT:
        return "AOA request 57 timed out; delivery to the accessory cannot be established.";
    case AOAHID_ERR_SHORT_TRANSFER:
        return "AOA request 57 completed with an actual payload length different from the "
               "requested length.";
    case AOAHID_ERR_NO_DEVICE:
        return "The device disappeared before or while AOA request 57 was sent.";
    case AOAHID_ERR_OVERFLOW:
        return "libusb completed AOA request 57 with a transfer-overflow status.";
    case AOAHID_ERR_IO:
        return "The asynchronous AOA request 57 transfer ended with a libusb I/O or cancellation "
               "status.";
    default:
        return "The asynchronous AOA request 57 transfer failed.";
    }
}

aoahid_result consume_completion(aoahid_node* node, const char* field) noexcept {
    const aoahid_result result =
        node->completion_result.exchange(AOAHID_OK, std::memory_order_acq_rel);
    if (result == AOAHID_OK) {
        node->completion_native_status.store(0, std::memory_order_relaxed);
        node->completion_report_length.store(0U, std::memory_order_relaxed);
        node->completion_report_id.store(0U, std::memory_order_relaxed);
        return AOAHID_OK;
    }
    const std::int32_t native_status =
        node->completion_native_status.exchange(0, std::memory_order_relaxed);
    const std::uint32_t length =
        node->completion_report_length.exchange(0U, std::memory_order_relaxed);
    const std::uint16_t completed_report_id =
        node->completion_report_id.exchange(0U, std::memory_order_relaxed);
    set_error(result, field, completion_reason(result), native_status, 57, node->hid_id,
              completed_report_id, 0U, length);
    return result;
}

aoahid_result initialize_node_state(aoahid_node* node) {
    switch (node->spec->kind) {
    case AOAHID_PROFILE_KEYBOARD: {
        const auto* spec = std::get_if<aoa::detail::KeyboardConfig>(&node->spec->config);
        aoa::detail::KeyboardState value{};
        const std::size_t key_count =
            static_cast<std::size_t>(spec->options.usage_maximum - spec->options.usage_minimum) +
            1U;
        value.key_transitions.resize(key_count);
        value.pressed_bitmap.resize(key_count);
        node->state = std::move(value);
        break;
    }
    case AOAHID_PROFILE_MOUSE: {
        const auto* spec = std::get_if<aoa::detail::MouseConfig>(&node->spec->config);
        aoa::detail::MouseState value{};
        value.buttons.resize(spec->options.button_count);
        value.button_transitions.resize(spec->options.button_count);
        node->state = std::move(value);
        break;
    }
    case AOAHID_PROFILE_TOGGLE: {
        const auto* spec = std::get_if<aoa::detail::ConsumerConfig>(&node->spec->config);
        aoa::detail::ConsumerState value{};
        value.pressed.resize(spec->allowed_usages.size());
        value.transitions.resize(spec->allowed_usages.size());
        node->state = std::move(value);
        break;
    }
    case AOAHID_PROFILE_GAMEPAD: {
        const auto* spec = std::get_if<aoa::detail::GamepadConfig>(&node->spec->config);
        aoa::detail::GamepadState value{};
        value.axes.reserve(spec->axes.size());
        for (const auto& axis : spec->axes)
            value.axes.push_back(axis.axis.neutral_value);
        value.buttons.resize(spec->options.button_count);
        value.button_transitions.resize(spec->options.button_count);
        node->state = std::move(value);
        break;
    }
    case AOAHID_PROFILE_TOUCHSCREEN:
    case AOAHID_PROFILE_TOUCHPAD: {
        const auto* spec = std::get_if<aoa::detail::TouchConfig>(&node->spec->config);
        aoa::detail::TouchState value{};
        value.buttons.resize(spec->fields.button_count);
        value.button_transitions.resize(spec->fields.button_count);
        node->state = std::move(value);
        break;
    }
    case AOAHID_PROFILE_PEN:
    case AOAHID_PROFILE_DIGITIZER:
        node->state = aoa::detail::PenState{};
        break;
    case AOAHID_PROFILE_RAW:
        node->state = aoa::detail::RawState{};
        node->dirty = false;
        break;
    default:
        set_error(AOAHID_ERR_INTERNAL, "spec.kind",
                  "A validated ProfileSpec contained an unknown profile kind.");
        return AOAHID_ERR_INTERNAL;
    }
    return AOAHID_OK;
}

bool has_non_neutral_state(const aoahid_node* node) noexcept {
    if (node == nullptr || node->spec == nullptr) {
        return false;
    }
    if (const auto* keyboard = std::get_if<aoa::detail::KeyboardState>(&node->state)) {
        return keyboard->modifiers != 0U || keyboard->pressed_bitmap_count != 0U;
    }
    if (const auto* mouse = std::get_if<aoa::detail::MouseState>(&node->state)) {
        return std::any_of(mouse->buttons.begin(), mouse->buttons.end(),
                           [](const std::uint8_t value) { return value != 0U; });
    }
    if (const auto* consumer = std::get_if<aoa::detail::ConsumerState>(&node->state)) {
        return std::any_of(consumer->pressed.begin(), consumer->pressed.end(),
                           [](const std::uint8_t value) { return value != 0U; });
    }
    if (const auto* gamepad = std::get_if<aoa::detail::GamepadState>(&node->state)) {
        const auto* spec = std::get_if<aoa::detail::GamepadConfig>(&node->spec->config);
        if (spec == nullptr)
            return false;
        for (std::size_t index = 0U; index < gamepad->axes.size(); ++index) {
            if (gamepad->axes[index] != spec->axes[index].axis.neutral_value)
                return true;
        }
        return gamepad->hat_has_direction ||
               std::any_of(gamepad->buttons.begin(), gamepad->buttons.end(),
                           [](const std::uint8_t value) { return value != 0U; });
    }
    if (const auto* touch = std::get_if<aoa::detail::TouchState>(&node->state)) {
        const bool active_contact =
            std::any_of(touch->contacts.begin(), touch->contacts.end(), [](const auto& contact) {
                return contact.phase == aoa::detail::ContactPhase::down;
            });
        return active_contact || std::any_of(touch->buttons.begin(), touch->buttons.end(),
                                             [](const std::uint8_t value) { return value != 0U; });
    }
    if (const auto* pen = std::get_if<aoa::detail::PenState>(&node->state)) {
        return pen->tool_switch_departure || pen->desired.in_range != 0U ||
               pen->desired.tip != 0U || pen->desired.barrel_buttons != 0U;
    }
    return false;
}

// drop_battery is the close path: a battery level has no neutral value, so an
// unsent one is discarded instead of being sent on the way out.
aoahid_result poll_until_idle(aoahid_node* node, const std::uint32_t timeout_ms,
                              const bool drop_battery) {
    if (timeout_ms == 0U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "timeout_ms", "A nonzero timeout is required.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    const aoahid_result preflight = preflight_context(node->device->context);
    if (preflight != AOAHID_OK)
        return preflight;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        // Set when every pool slot is taken by other Nodes: this Node has no
        // transfer of its own to wait for, so it waits for any slot instead.
        bool pool_busy = false;
        std::uint64_t slot_generation = 0U;
        if (!node->transfer_inflight.load(std::memory_order_acquire)) {
            const aoahid_result completion = consume_completion(node, "node.completion");
            if (completion != AOAHID_OK)
                return completion;
            if (drop_battery) {
                node->battery_dirty = false;
            }
            if (!node->dirty && !node->battery_dirty)
                return AOAHID_OK;
            slot_generation = node->device->slot_generation.load();
            // The blocking timeout also bounds each report it sends: what is left of it.
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            const std::uint32_t send_timeout_ms = static_cast<std::uint32_t>(std::clamp<long long>(
                left, 1LL, static_cast<long long>(std::numeric_limits<std::uint32_t>::max())));
            const aoahid_result submit = aoahid_node_submit(node, send_timeout_ms);
            if (submit != AOAHID_OK && submit != AOAHID_ERR_BUSY)
                return submit;
            pool_busy = submit == AOAHID_ERR_BUSY &&
                        !node->transfer_inflight.load(std::memory_order_acquire);
        }
        if (node->device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
            const aoahid_result poll = node->device->context->runtime->poll(1U);
            if (poll != AOAHID_OK && poll != AOAHID_ERR_TIMEOUT) {
                return finish_transport_result(poll, "context.poll",
                                               "libusb event handling failed.", node);
            }
        } else if (pool_busy) {
            // Woken by the next completion on this Device (see
            // transfer_complete()); a bump before this wait makes it return
            // at once, so no release is missed. The quantum only rechecks
            // Context errors.
            aoahid_device* device = node->device;
            std::unique_lock<std::mutex> lock(device->slot_mutex);
            device->slot_waiters.fetch_add(1U);
            const auto quantum = std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
            static_cast<void>(device->slot_cv.wait_until(
                lock, std::min(deadline, quantum), [device, slot_generation]() noexcept {
                    return device->slot_generation.load() != slot_generation;
                }));
            device->slot_waiters.fetch_sub(1U);
            lock.unlock();
            const aoahid_result event_error = preflight_context(node->device->context);
            if (event_error != AOAHID_OK)
                return event_error;
        } else {
            std::unique_lock<std::mutex> lock(node->completion_mutex);
            node->completion_waiters.fetch_add(1U, std::memory_order_acq_rel);
            const auto quantum = std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
            static_cast<void>(node->completion_cv.wait_until(
                lock, std::min(deadline, quantum), [node]() noexcept {
                    return !node->transfer_inflight.load(std::memory_order_acquire);
                }));
            node->completion_waiters.fetch_sub(1U, std::memory_order_acq_rel);
            // Do not nest the Context mutex below the per-Node completion
            // mutex. The event thread publishes Context errors independently.
            lock.unlock();
            const aoahid_result event_error = preflight_context(node->device->context);
            if (event_error != AOAHID_OK)
                return event_error;
        }
    } while (std::chrono::steady_clock::now() < deadline);
    set_error(AOAHID_ERR_TIMEOUT, "node.submit_blocking",
              "The report sequence did not reach a terminal callback before the deadline.", 0, 57,
              node->hid_id, report_id(node), 0U,
              static_cast<std::uint32_t>(node->spec->layout.wire_bytes));
    return AOAHID_ERR_TIMEOUT;
}

std::uint32_t
remaining_milliseconds(const std::chrono::steady_clock::time_point deadline) noexcept {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0U;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    if (remaining <= 0) {
        return 1U;
    }
    return remaining > static_cast<std::chrono::milliseconds::rep>(
                           std::numeric_limits<std::uint32_t>::max())
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(remaining);
}

// The Node stays live only while a transfer of it is in flight, because the transfer's callback
// still points at it: that case returns AOAHID_CLOSE_PENDING. Every other result means the Node is
// gone. A step that fails does not stop the ones after it, and the first failure is returned.
aoahid_result close_live_node_until(aoahid_node* node,
                                    const std::chrono::steady_clock::time_point deadline,
                                    const std::uint32_t control_timeout_ms) {
    aoahid_device* device = node->device;
    aoa::detail::DeferredError failure{};
    // Every step records its own error detail, which is kept for the first failure.
    const auto keep_first = [&](const aoahid_result result) {
        if (result != AOAHID_OK && !failure.pending)
            failure = current_deferred_error(result, "node.close", "A close step failed.", node);
    };
    const auto still_in_flight = [&]() {
        if (!node->transfer_inflight.load(std::memory_order_acquire))
            return false;
        set_error(AOAHID_CLOSE_PENDING, "node.close",
                  "A report transfer of the Node was still in flight at the close deadline.", 0, 57,
                  node->hid_id, report_id(node), 0U,
                  static_cast<std::uint32_t>(node->spec->layout.wire_bytes));
        return true;
    };

    // A touch frame that has started must be finished before the neutral frame replaces it.
    if (node->transfer_inflight.load(std::memory_order_acquire) || touch_frame_in_progress(node)) {
        const std::uint32_t remaining = remaining_milliseconds(deadline);
        if (remaining != 0U)
            keep_first(poll_until_idle(node, remaining, true));
    }
    if (still_in_flight())
        return AOAHID_CLOSE_PENDING;
    keep_first(consume_completion(node, "node.completion"));
    node->battery_dirty = false;

    if (device->transport->present()) {
        make_neutral(node);
        if (node->dirty) {
            const std::uint32_t remaining = remaining_milliseconds(deadline);
            if (remaining == 0U) {
                set_error(AOAHID_ERR_TIMEOUT, "node.close",
                          "No time was left to send the neutral report.");
                keep_first(AOAHID_ERR_TIMEOUT);
            } else {
                keep_first(poll_until_idle(node, remaining, true));
            }
            if (still_in_flight())
                return AOAHID_CLOSE_PENDING;
        }
        const aoahid_result unregister =
            device->transport->unregister_hid(node->hid_id, control_timeout_ms);
        if (unregister != AOAHID_ERR_NO_DEVICE) {
            keep_first(finish_transport_result(unregister, "node.unregister",
                                               "AOA request 55 failed while closing the Node.",
                                               node, 55));
        }
    }
    if (device->transport->clear_reservation(node->reservation) != AOAHID_OK) {
        set_error(AOAHID_CLOSE_PENDING, "node.reserved_slots",
                  "The Node's reserved transfer slot is still in use.", 0, 57, node->hid_id);
        return AOAHID_CLOSE_PENDING;
    }

    auto& nodes = device->nodes;
    nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
    aoa::detail::log_event(device->context, AOAHID_LOG_INFO, "node_close", device, node,
                           aoa::detail::LogEventStatus{failure.transport.result, 0, 55});
    release_node_storage(node);
    return publish_deferred_error(failure);
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

    while (!device->nodes.empty()) {
        aoahid_node* node = device->nodes.back();
        device->nodes.pop_back();
        node->closed = true;
        remember_terminal_completion(device, node);
        if (device->transport->present()) {
            const aoahid_result unregister =
                device->transport->unregister_hid(node->hid_id, device->close_control_timeout_ms);
            if (unregister != AOAHID_OK && unregister != AOAHID_ERR_NO_DEVICE) {
                remember_current_close_error(device, unregister, "node.unregister",
                                             "AOA request 55 failed during Device teardown.", node,
                                             55);
            }
        }
        const aoahid_result clear_reservation =
            device->transport->clear_reservation(node->reservation);
        remember_current_close_error(
            device, clear_reservation, "node.reserved_slots",
            "A transfer-pool reservation could not be released during Device teardown.", node);
        release_node_storage(node);
    }

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

    try {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(device->close_drain_timeout_ms);
        while (!device->nodes.empty()) {
            aoahid_node* closing_node = device->nodes.back();
            const aoahid_result result =
                close_live_node_until(closing_node, deadline, control_timeout_ms);
            // The pending Node and the ones not closed yet go to the graveyard with the Device.
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
