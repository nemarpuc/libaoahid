// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: creating, polling and destroying a Context.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <new>
#include <thread>

using aoa::detail::current_deferred_error;
using aoa::detail::DeferredErrorInput;
using aoa::detail::device_close_impl;
using aoa::detail::finish_transport_result;
using aoa::detail::graveyard_empty;
using aoa::detail::last_active_device;
using aoa::detail::latch_context_error;
using aoa::detail::latch_terminal_event_error;
using aoa::detail::make_deferred_error;
using aoa::detail::peek_context_error;
using aoa::detail::peek_terminal_event_error;
using aoa::detail::preflight_context;
using aoa::detail::publish_deferred_error;
using aoa::detail::reap_graveyard;
using aoa::detail::report_id;
using aoa::detail::request_event_thread_stop;
using aoa::detail::set_error;
using aoa::detail::take_context_error;
using aoa::detail::timeout_is_set;
using aoa::detail::validate_context_options;

namespace {

// A long bounded wait removes the former 100 Hz idle wake cadence while still
// bounding shutdown if a backend cannot honor an event-handler interrupt.
constexpr std::uint32_t kInternalEventPollTimeoutMs = 60'000U;

} // namespace

extern "C" {

aoahid_result AOAHID_CALL aoahid_context_create(const aoahid_context_options* options,
                                                aoahid_context** out_context) try {
    aoa::detail::clear_error();
    if (out_context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_context", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_context = nullptr;
    const aoahid_result validation = validate_context_options(options);
    if (validation != AOAHID_OK)
        return validation;
    auto* context = new (std::nothrow) aoahid_context();
    if (context == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "context", "Memory allocation for the Context failed.");
        return AOAHID_ERR_INTERNAL;
    }
    context->options = *options;
    context->active_state_mutex =
        options->event_mode == AOAHID_EVENT_INTERNAL_THREAD ? &context->mutex : nullptr;
    const aoahid_result result =
        aoa::transport::Runtime::create(options->event_mode, &context->runtime);
    if (result != AOAHID_OK) {
        delete context;
        return finish_transport_result(result, "context.runtime",
                                       "libusb runtime initialization failed.");
    }
    if (options->event_mode == AOAHID_EVENT_INTERNAL_THREAD) {
        try {
            context->event_thread = std::thread([context]() noexcept {
                try {
                    while (!context->stop_event_thread.load(std::memory_order_acquire)) {
                        const aoahid_result poll_result =
                            context->runtime->poll(kInternalEventPollTimeoutMs);
                        if (poll_result != AOAHID_OK) {
                            const aoa::transport::ErrorInfo transport_error =
                                aoa::transport::last_error();
                            latch_context_error(
                                context,
                                make_deferred_error(DeferredErrorInput{
                                    aoa::transport::ErrorInfo{poll_result,
                                                              transport_error.result == poll_result
                                                                  ? transport_error.native_status
                                                                  : 0,
                                                              transport_error.result == poll_result
                                                                  ? transport_error.aoa_request
                                                                  : 0,
                                                              transport_error.result == poll_result
                                                                  ? transport_error.hid_id
                                                                  : std::uint16_t{0U},
                                                              transport_error.result == poll_result
                                                                  ? transport_error.offset
                                                                  : 0U,
                                                              transport_error.result == poll_result
                                                                  ? transport_error.length
                                                                  : 0U},
                                    "context.event_pump",
                                    "The internal libusb event pump failed; the first failure was "
                                    "retained for an application thread."}));
                        }
                        // poll() has returned from every callback it dispatched, so a
                        // drained graveyard graph can now be reclaimed safely.
                        reap_graveyard(context);
                        if (poll_result != AOAHID_OK) {
                            // A backend that fails immediately must not turn the
                            // internal event thread into a busy loop. This wait is
                            // confined to the failure path and remains interruptible.
                            std::unique_lock<std::mutex> lock(context->mutex);
                            static_cast<void>(context->graveyard_cv.wait_for(
                                lock, std::chrono::milliseconds(10U), [context]() noexcept {
                                    return context->stop_event_thread.load(
                                        std::memory_order_acquire);
                                }));
                        }
                    }
                } catch (...) {
                    latch_terminal_event_error(
                        context,
                        make_deferred_error(DeferredErrorInput{
                            aoa::transport::ErrorInfo{AOAHID_ERR_INTERNAL, 0, 0, 0U, 0U, 0U},
                            "context.event_pump",
                            "An internal exception stopped the Context event thread."}));
                }
                context->event_thread_exited.store(true, std::memory_order_release);
                context->graveyard_cv.notify_all();
            });
        } catch (...) {
            delete context->runtime;
            context->runtime = nullptr;
            delete context;
            throw;
        }
    }
    *out_context = context;
    aoa::detail::emit_log(context, AOAHID_LOG_INFO, "event=context_create result=AOAHID_OK");
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("context.create")

aoahid_result AOAHID_CALL aoahid_context_poll(aoahid_context* context,
                                              const std::uint32_t timeout_ms) try {
    AOAHID_SERIALIZED(context, "aoahid_context_poll");
    aoa::detail::clear_error();
    if (context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "context", "The Context handle is null.");
        return AOAHID_ERR_PARAM;
    }
    if (context->options.event_mode != AOAHID_EVENT_CALLER_POLL) {
        set_error(AOAHID_ERR_UNSUPPORTED, "context.event_mode",
                  "Caller polling is available only in caller-poll mode.");
        return AOAHID_ERR_UNSUPPORTED;
    }
    const aoahid_result deferred = preflight_context(context);
    if (deferred != AOAHID_OK)
        return deferred;
    const aoahid_result result = context->runtime->poll(timeout_ms);
    // libusb cancellation is asynchronous. A graveyard device can be released
    // only after poll has returned from every terminal callback.
    reap_graveyard(context);
    if (result != AOAHID_OK) {
        return finish_transport_result(result, "context.poll", "libusb event handling failed.");
    }
    return preflight_context(context);
}
AOAHID_C_RESULT_CATCH("context.poll")

static aoahid_result context_destroy_impl(aoahid_context* context, bool* context_consumed,
                                          const std::uint32_t control_timeout_ms,
                                          const std::uint32_t drain_timeout_ms) {
    *context_consumed = false;
    aoa::detail::clear_error();
    if (context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "context.destroy", "The Context handle is null.");
        return AOAHID_ERR_PARAM;
    }
    aoa::detail::DeferredError close_error{};
    for (;;) {
        aoahid_device* device = last_active_device(context);
        if (device == nullptr) {
            break;
        }
        bool device_consumed = false;
        const aoahid_result close =
            device_close_impl(device, &device_consumed, control_timeout_ms, drain_timeout_ms);
        if (!device_consumed) {
            const aoahid_result retained_result = close == AOAHID_OK ? AOAHID_ERR_INTERNAL : close;
            const aoa::detail::DeferredError error = current_deferred_error(
                retained_result, "device.close",
                "A Device teardown operation failed before ownership transfer.");
            // No Device pointer access is permitted after device_close_impl:
            // only its explicit ownership result decides whether this Context
            // remains retryable.
            latch_context_error(context, error);
            set_error(AOAHID_CLOSE_PENDING, "context.destroy",
                      "A Device could not yet transfer ownership to Context teardown; the Context "
                      "remains caller-owned.",
                      error.transport.native_status, error.transport.aoa_request,
                      error.transport.hid_id, error.report_id, error.transport.offset,
                      error.transport.length);
            return AOAHID_CLOSE_PENDING;
        }
        if (close != AOAHID_OK && close != AOAHID_CLOSE_PENDING) {
            const aoa::detail::DeferredError error = current_deferred_error(
                close, "device.close",
                "A Device teardown operation failed while destroying its Context.");
            if (!close_error.pending)
                close_error = error;
        }
    }
    latch_context_error(context, close_error);

    reap_graveyard(context);
    if (!graveyard_empty(context)) {
        aoa::detail::DeferredError prior{};
        static_cast<void>(peek_context_error(context, &prior));
        set_error(AOAHID_CLOSE_PENDING, "context.destroy",
                  "The Context graveyard still owns Device graphs with pending callbacks.",
                  prior.transport.native_status, prior.transport.aoa_request,
                  prior.transport.hid_id, prior.report_id, prior.transport.offset,
                  prior.transport.length);
        return AOAHID_CLOSE_PENDING;
    }

    request_event_thread_stop(context);
    if (context->event_thread.joinable())
        context->event_thread.join();
    aoa::detail::DeferredError terminal{};
    // A stopped event pump is the terminal cause that made caller takeover
    // necessary. Prefer it over an older recoverable poll diagnostic while
    // still clearing ordinary deferred storage before Context destruction.
    if (peek_terminal_event_error(context, &terminal)) {
        aoa::detail::DeferredError discarded{};
        static_cast<void>(take_context_error(context, &discarded));
    } else {
        static_cast<void>(take_context_error(context, &terminal));
    }
    if (terminal.pending && terminal.transport.result == AOAHID_CLOSE_PENDING) {
        terminal = make_deferred_error(DeferredErrorInput{
            aoa::transport::ErrorInfo{AOAHID_ERR_INTERNAL, terminal.transport.native_status,
                                      terminal.transport.aoa_request, terminal.transport.hid_id,
                                      terminal.transport.offset, terminal.transport.length},
            "context.destroy",
            "Context teardown reached a terminal state with an invalid CLOSE_PENDING diagnostic.",
            terminal.report_id});
    }
    // Publish and localize the terminal result before deleting Context. The
    // blocking wrapper may inspect Context only when this value is precisely
    // CLOSE_PENDING, which is excluded above for every consuming path.
    const aoahid_result terminal_result = publish_deferred_error(terminal);
    aoa::detail::emit_log(
        context, terminal_result == AOAHID_OK ? AOAHID_LOG_INFO : AOAHID_LOG_ERROR,
        terminal_result == AOAHID_OK ? "event=context_destroy result=AOAHID_OK"
                                     : "event=context_destroy result=deferred_error");
    *context_consumed = true;
    delete context->runtime;
    delete context;
    return terminal_result;
}

aoahid_result AOAHID_CALL aoahid_context_destroy(aoahid_context* context,
                                                 const std::uint32_t control_timeout_ms,
                                                 const std::uint32_t drain_timeout_ms) try {
    AOAHID_SERIALIZED_PROBE(context, "aoahid_context_destroy");
    aoa::detail::clear_error();
    if (context != nullptr && (!timeout_is_set(control_timeout_ms, "control_timeout_ms") ||
                               !timeout_is_set(drain_timeout_ms, "drain_timeout_ms"))) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    bool consumed = false;
    return context_destroy_impl(context, &consumed, control_timeout_ms, drain_timeout_ms);
}
AOAHID_C_RESULT_CATCH("context.destroy")

aoahid_result AOAHID_CALL aoahid_context_destroy_blocking(
    aoahid_context* context, const std::uint32_t control_timeout_ms,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const std::uint32_t drain_timeout_ms, const std::uint32_t timeout_ms) try {
    AOAHID_SERIALIZED_PROBE(context, "aoahid_context_destroy_blocking");
    aoa::detail::clear_error();
    if (context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "context.destroy", "The Context handle is null.");
        return AOAHID_ERR_PARAM;
    }
    if (!timeout_is_set(control_timeout_ms, "control_timeout_ms") ||
        !timeout_is_set(drain_timeout_ms, "drain_timeout_ms") ||
        !timeout_is_set(timeout_ms, "timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    aoa::detail::DeferredError observed{};
    do {
        bool consumed = false;
        const aoahid_result result =
            context_destroy_impl(context, &consumed, control_timeout_ms, drain_timeout_ms);
        if (consumed) {
            return observed.pending ? publish_deferred_error(observed) : result;
        }
        if (result != AOAHID_CLOSE_PENDING) {
            return observed.pending ? publish_deferred_error(observed) : result;
        }
        if (context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
            const aoahid_result poll = aoahid_context_poll(context, 1U);
            if (poll != AOAHID_OK && !observed.pending) {
                observed = current_deferred_error(
                    poll, "context.poll",
                    "libusb event handling failed while draining Context teardown.");
            }
        } else if (context->event_thread_failed.load(std::memory_order_acquire)) {
            // A terminal event-thread failure leaves no pump to deliver cancel
            // callbacks. Stop and join that thread before the caller takes over;
            // the normal internal-thread path never polls here, so two threads
            // cannot enter libusb event handling concurrently.
            if (context->event_thread.joinable()) {
                if (!context->event_thread_exited.load(std::memory_order_acquire)) {
                    request_event_thread_stop(context);
                }
                if (context->event_thread.get_id() != std::this_thread::get_id()) {
                    context->event_thread.join();
                }
            }
            const aoahid_result poll = context->runtime->poll(1U);
            reap_graveyard(context);
            if (poll != AOAHID_OK) {
                latch_context_error(
                    context, current_deferred_error(poll, "context.poll",
                                                    "Caller fallback event handling failed after "
                                                    "the internal event thread terminated."));
                const auto quantum =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
                std::unique_lock<std::mutex> lock(context->mutex);
                static_cast<void>(
                    context->graveyard_cv.wait_until(lock, std::min(deadline, quantum)));
            }
        } else {
            std::unique_lock<std::mutex> lock(context->mutex);
            const auto quantum = std::chrono::steady_clock::now() + std::chrono::milliseconds(10U);
            static_cast<void>(context->graveyard_cv.wait_until(lock, std::min(deadline, quantum)));
        }
    } while (std::chrono::steady_clock::now() < deadline);
    aoa::detail::DeferredError deferred{};
    if (observed.pending) {
        latch_context_error(context, observed);
        deferred = observed;
    } else {
        static_cast<void>(peek_context_error(context, &deferred));
    }
    set_error(AOAHID_CLOSE_PENDING, "context.destroy",
              "Teardown did not drain before the deadline; the Context remains caller-owned.",
              deferred.transport.native_status, deferred.transport.aoa_request,
              deferred.transport.hid_id, deferred.report_id, deferred.transport.offset,
              deferred.transport.length);
    return AOAHID_CLOSE_PENDING;
}
AOAHID_C_RESULT_CATCH("context.destroy_blocking")

} // extern "C"
