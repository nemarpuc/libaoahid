// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Context destroy and the graveyard: aggregated unregister errors, no terminal
 * CLOSE_PENDING, delayed cancels, blocking destroy and its deadline. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_context_destroy_aggregates_unregister_error() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t hid_id = aoahid_node_id(node);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);

    // This terminal error consumes Context: teardown completed and the exact
    // request-55 failure is returned instead of being discarded.
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 55);
    AOAHID_CHECK(aoahid_last_error()->node_id == hid_id);
    // The failed unregister consumed the Node, so teardown does not send request 55 again.
    AOAHID_CHECK(controls_for(55U).size() == 1U);
    aoahid_fake_libusb_reset();
}

void test_context_destroy_never_returns_terminal_pending() {
    aoahid_fake_libusb_reset();
    aoahid_context_options options = public_context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    aoa::detail::DeferredError invalid_terminal{};
    invalid_terminal.transport = aoa::transport::ErrorInfo{AOAHID_CLOSE_PENDING, 0, 0, 0U, 0U, 0U};
    invalid_terminal.field = "context.destroy";
    invalid_terminal.reason = "Injected invalid terminal pending state.";
    invalid_terminal.pending = true;
    {
        const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
        context->deferred_error = invalid_terminal;
    }
    // Context is consumed, but the consuming result can never be the one value
    // that authorizes the blocking wrapper to inspect it again.
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) ==
                 AOAHID_ERR_INTERNAL);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_INTERNAL);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "context.destroy");
    }
    aoahid_fake_libusb_reset();
}

void test_graveyard_publishes_unregister_and_cancel_errors() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t unregister_hid_id = aoahid_node_id(node);
    submit_delayed_public_report(node);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 55);
    AOAHID_CHECK(aoahid_last_error()->node_id == unregister_hid_id);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);

    aoahid_fake_libusb_reset();
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t cancel_hid_id = aoahid_node_id(node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(report.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_cancel_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->node_id == cancel_hid_id);
    AOAHID_CHECK(aoahid_last_error()->length == report.size());
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_graveyard_delayed_cancel() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    submit_delayed_public_report(node);

    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_CLOSE_PENDING);

    // poll executes CANCELLED, returns from the callback, and only then reaps
    // the complete device/node/spec/transport graph from the graveyard.
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_graveyard_blocking_destroy() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    submit_delayed_public_report(node);

    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    // A zero deadline is an unset timeout like any other, and destroys nothing.
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs,
                                                 session_drain_timeout_ms,
                                                 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs,
                                                 session_drain_timeout_ms, 50U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    aoahid_fake_libusb_reset();
}

void test_blocking_destroy_deadline_retains_context() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(report.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_cancel_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);

    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs,
                                                 session_drain_timeout_ms,
                                                 2U) == AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_CLOSE_PENDING);
    // CLOSE_PENDING retains Context. Make the original accepted transfer terminal,
    // publish the stored cancel failure, and then finish destruction.
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
