// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Device open through the public ABI: no AOA request on open, caller policies checked
 * before any USB I/O, cancellation that stays within one Device, and a blocked event wait
 * woken by cancel. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_open_sends_no_aoa_request() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(6U, {1U, 7U});
    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected{candidate.bus,
                                      candidate.address,
                                      candidate.port_path.data(),
                                      candidate.port_path.size(),
                                      candidate.vendor_id,
                                      candidate.product_id,
                                      0U,
                                      candidate.serial.c_str(),
                                      candidate.product.c_str()};
    aoahid_device_options options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
    AOAHID_CHECK(device != nullptr);
    AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);

    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_required_device_policies_precede_usb_io() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(6U, {1U, 7U});
    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected{candidate.bus,
                                      candidate.address,
                                      candidate.port_path.data(),
                                      candidate.port_path.size(),
                                      candidate.vendor_id,
                                      candidate.product_id,
                                      0U,
                                      candidate.serial.c_str(),
                                      candidate.product.c_str()};

    using ClearPolicy = void (*)(aoahid_device_options&);
    const std::array<ClearPolicy, 4U> required_byte_policies{
        [](aoahid_device_options& options) { options.aoa_descriptor_wire_policy_bytes = 0U; },
        [](aoahid_device_options& options) { options.linux_hid.descriptor_bytes = 0U; },
        [](aoahid_device_options& options) { options.target_ep0_data_policy_bytes = 0U; },
        [](aoahid_device_options& options) { options.host_control_buffer_policy_bytes = 0U; }};
    for (const ClearPolicy clear : required_byte_policies) {
        aoahid_device_options rejected = public_device_options();
        clear(rejected);
        aoahid_device* rejected_device = nullptr;
        AOAHID_CHECK(aoahid_device_open(context, &selected, &rejected, &rejected_device) ==
                     AOAHID_ERR_UNSET_FIELD);
        AOAHID_CHECK(rejected_device == nullptr);
        AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);
        AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
        AOAHID_CHECK(aoahid_last_error()->field != nullptr);
        if (aoahid_last_error()->field != nullptr) {
            AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "device_options");
        }
    }

    aoahid_device* device = nullptr;
    aoahid_device_options options = public_device_options();
    options.linux_hid.fields_per_report = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid.usages = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid.global_stack_depth = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid.report_data_bits = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid.report_size_bits = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_cancel_and_device_isolation() {
    aoahid_fake_libusb_reset();
    const auto first_candidate = add_candidate(2U, {3U, 1U});
    const auto second_candidate = add_candidate(4U, {3U, 2U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    Device* first = nullptr;
    Device* second = nullptr;
    AOAHID_CHECK(Device::open(runtime, first_candidate, config, &first) == AOAHID_OK);
    AOAHID_CHECK(Device::open(runtime, second_candidate, config, &second) == AOAHID_OK);

    CompletionState cancelled{};
    CompletionState unaffected{};
    PreparedTransfer first_transfer{};
    PreparedTransfer second_transfer{};
    AOAHID_CHECK(first->acquire(1U, {}, 1U, &cancelled, &completed, &first_transfer) == AOAHID_OK);
    AOAHID_CHECK(second->acquire(1U, {}, 1U, &unaffected, &completed, &second_transfer) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 50U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 0U);
    AOAHID_CHECK(first->submit(first_transfer, 1U, 0U, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(second->submit(second_transfer, 1U, 0U, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(first->cancel_all() == AOAHID_OK);
    for (std::size_t poll = 0U; poll < 20U && (cancelled.calls == 0U || unaffected.calls == 0U);
         ++poll) {
        AOAHID_CHECK(runtime->poll(1U) == AOAHID_OK);
    }
    AOAHID_CHECK(cancelled.calls == 1U && cancelled.result == AOAHID_ERR_IO);
    AOAHID_CHECK(unaffected.calls == 1U && unaffected.result == AOAHID_OK);
    AOAHID_CHECK(first->drained());
    AOAHID_CHECK(second->drained());
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);

    delete first;
    delete second;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_blocking_event_wait_is_woken_by_cancel() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(5U, {3U, 3U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_INTERNAL_THREAD, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    Device* device = nullptr;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    CompletionState completion{};
    PreparedTransfer prepared{};
    AOAHID_CHECK(device->acquire(1U, {}, 1U, &completion, &completed, &prepared) == AOAHID_OK);
    prepared.payload[0] = 1U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 100'000U);
    AOAHID_CHECK(device->submit(prepared, 1U, 0U, kSendTimeoutMs) == AOAHID_OK);

    // The generous timeout is only a deadlock escape, not a performance
    // threshold. The fake backend reports when poll() is actually blocked, so
    // cancellation can be issued at a deterministic synchronization point.
    std::atomic<aoahid_result> poll_result{AOAHID_ERR_INTERNAL};
    std::thread event_thread(
        [&] { poll_result.store(runtime->poll(5'000U), std::memory_order_release); });
    AOAHID_CHECK(aoahid_fake_libusb_wait_for_event_waiters(1U, 5'000U) == LIBUSB_SUCCESS);
    const aoahid_fake_libusb_event_stats waiting = event_stats();
    AOAHID_CHECK(waiting.active_waiters == 1U);
    AOAHID_CHECK(waiting.zero_timeout_calls == 0U);
    AOAHID_CHECK(waiting.requested_timeout_us == 5'000'000U);
    AOAHID_CHECK(waiting.callbacks_dispatched == 0U);

    AOAHID_CHECK(device->cancel_all() == AOAHID_OK);
    event_thread.join();
    const aoahid_fake_libusb_event_stats completed_stats = event_stats();
    AOAHID_CHECK(poll_result.load(std::memory_order_acquire) == AOAHID_OK);
    AOAHID_CHECK(completed_stats.notification_wakeups == waiting.notification_wakeups + 1U);
    AOAHID_CHECK(completed_stats.cancellation_wakeups == waiting.cancellation_wakeups + 1U);
    AOAHID_CHECK(completed_stats.callbacks_dispatched == 1U);
    AOAHID_CHECK(completion.calls == 1U && completion.result == AOAHID_ERR_IO);
    AOAHID_CHECK(device->drained());
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
