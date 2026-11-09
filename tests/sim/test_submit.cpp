// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Report submission: the public error contract and raw padding, a first report that
 * stalls, a refused submit that keeps state, a deferred poll error after an accepted
 * submit, and a blocking submit that sleeps for a free slot. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_public_error_contract_and_raw_padding() {
    auto* stale_discovery = reinterpret_cast<aoahid_discovery*>(static_cast<std::uintptr_t>(1U));
    AOAHID_CHECK(aoahid_discover(nullptr, 0U, &stale_discovery) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(stale_discovery == nullptr);
    auto* stale_device = reinterpret_cast<aoahid_device*>(static_cast<std::uintptr_t>(1U));
    AOAHID_CHECK(aoahid_device_open(nullptr, nullptr, nullptr, &stale_device) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(stale_device == nullptr);
    AOAHID_CHECK(aoahid_context_poll(nullptr, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr && aoahid_last_error()->reason != nullptr);
    static_cast<void>(aoahid_version());
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->field == nullptr);
    AOAHID_CHECK(aoahid_last_error()->transport_status == 0);
    AOAHID_CHECK(aoahid_result_name(999) != nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_result_name(AOAHID_OK) != nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);

    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);

    const std::array<std::uint8_t, 2U> bad_padding{1U, 0x80U};
    AOAHID_CHECK(aoahid_raw_submit(node, bad_padding.data(), bad_padding.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    const aoahid_error_detail* padding_error = aoahid_last_error();
    AOAHID_CHECK(padding_error->field != nullptr && padding_error->reason != nullptr);
    AOAHID_CHECK(padding_error->aoa_request == 57);
    AOAHID_CHECK(padding_error->report_id == 1U);
    AOAHID_CHECK(padding_error->offset == 1U && padding_error->length == 1U);
    AOAHID_CHECK(controls_for(57U).empty());

    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_ERR_TIMEOUT);
    const aoahid_error_detail* completion_error = aoahid_last_error();
    AOAHID_CHECK(completion_error->code == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(completion_error->transport_status == LIBUSB_TRANSFER_TIMED_OUT);
    AOAHID_CHECK(completion_error->aoa_request == 57);
    AOAHID_CHECK(completion_error->node_id != 0U && completion_error->report_id == 1U);
    AOAHID_CHECK(completion_error->offset == 0U && completion_error->length == 2U);
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_TIMED_OUT);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->report_id == 1U);

    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_OVERFLOW, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_ERR_OVERFLOW);
    const aoahid_error_detail* overflow_error = aoahid_last_error();
    AOAHID_CHECK(overflow_error->code == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow_error->transport_status == LIBUSB_TRANSFER_OVERFLOW);
    AOAHID_CHECK(overflow_error->aoa_request == 57 && overflow_error->report_id == 1U);
    AOAHID_CHECK(overflow_error->length == report.size());
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_TRANSFER_OVERFLOW);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_first_report_stall_is_returned_without_retry() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);

    // A first report that races Android's HID registration and stalls goes
    // out once; the STALL reaches the caller, who decides whether to resend.
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    for (std::size_t attempt = 0U;
         attempt < 10U && node->transfer_inflight.load(std::memory_order_acquire); ++attempt) {
        AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_OK);
    }
    AOAHID_CHECK(!node->transfer_inflight.load(std::memory_order_acquire));
    AOAHID_CHECK(controls_for(57U).size() == 1U);

    // The STALL is reported once; the refused state stays pending, so the
    // caller's explicit resend is an ordinary submit of the same report.
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_ERR_STALL);
    AOAHID_CHECK(controls_for(57U).size() == 1U);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    const auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U && payloads[0] == payloads[1]);
    // A timeout may have been delivered, so it is not resent automatically.
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x05U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    for (std::size_t attempt = 0U;
         attempt < 10U && node->transfer_inflight.load(std::memory_order_acquire); ++attempt) {
        AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_OK);
    }
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(controls_for(57U).size() == 3U);

    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// With every pool slot taken by another Node, submit_blocking in
// internal-thread mode sleeps until a slot is released instead of spinning,
// and sends as soon as one is.
void test_submit_blocking_waits_for_a_free_slot_without_spinning() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(12U, {5U, 2U});
    aoahid_context_options context_options = public_context_options();
    context_options.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(candidate);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    aoahid_node_options node_options{};
    node_options.struct_size = sizeof(node_options);
    aoahid_node* holder = nullptr;
    aoahid_node* waiter = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &holder) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &waiter) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);

    // The holder's report takes the only slot and stays in flight.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1,
                                              std::numeric_limits<std::uint32_t>::max());
    AOAHID_CHECK(aoahid_keyboard_key(holder, 0x04U, 1U) == AOAHID_OK);
    // The holder's transfer must outlive the waiter's whole wait.
    AOAHID_CHECK(aoahid_node_submit(holder, 5000U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(waiter, 0x05U, 1U) == AOAHID_OK);

    std::atomic<bool> done{false};
    aoahid_result result = AOAHID_ERR_INTERNAL;
    [[maybe_unused]] std::int64_t cpu_ns = -1;
    std::thread thread([&]() {
#if defined(__linux__)
        timespec start{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
#endif
        result = aoahid_node_submit_blocking(waiter, 3000U);
#if defined(__linux__)
        timespec end{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
        cpu_ns = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
#endif
        done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    AOAHID_CHECK(!done.load());
    const auto released = std::chrono::steady_clock::now();
    aoahid_fake_libusb_make_pending_transfers_ready();
    while (!done.load() && std::chrono::steady_clock::now() - released < std::chrono::seconds(3))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto waited = std::chrono::steady_clock::now() - released;
    thread.join();
    AOAHID_CHECK(result == AOAHID_OK);
    AOAHID_CHECK(waited < std::chrono::milliseconds(100));
#if defined(__linux__)
    // A spinning wait would burn roughly the whole 200 ms.
    AOAHID_CHECK(cpu_ns >= 0 && cpu_ns < 50000000LL);
#endif
    AOAHID_CHECK(aoahid_keyboard_key(holder, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(waiter, 0x05U, 0U) == AOAHID_OK);
    // Both Nodes send a neutral report on close; a 1 ms budget would make this test timing-bound.
    constexpr std::uint32_t close_drain_timeout_ms = 1000U;
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, close_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, close_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_submit_rejection_retains_state_and_close_releases() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);

    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(controls_for(57U).empty());
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 1U);
    if (payloads.size() == 1U) {
        // Usage 0x04 is bitmap bit zero: the first byte after the modifier byte.
        AOAHID_CHECK(payloads[0].size() > 1U && payloads[0][1] == 0x01U);
    }

    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U) {
        AOAHID_CHECK(std::all_of(payloads[1].begin(), payloads[1].end(),
                                 [](const std::uint8_t value) { return value == 0U; }));
    }
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_accepted_submit_defers_opportunistic_poll_error() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);
    // These fixed null selectors are the structural fast-path contract: the
    // submit preflight/error latch and synchronous completion callback use the
    // caller's synchronization domain without acquiring std::mutex.
    AOAHID_CHECK(context->active_state_mutex == nullptr);
    AOAHID_CHECK(node->active_completion_mutex == nullptr);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);

    // The report was accepted, so the submit remains successful. The exact
    // event-handler failure is delivered on the next Context-using call.
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_ERR_IO);
    AOAHID_CHECK(aoahid_last_error()->transport_status == LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "context.event_pump");
    }
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
