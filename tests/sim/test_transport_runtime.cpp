// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Runtime, Device and Port at the transport boundary: libusb initialisation, AOA HID
 * registration and its reservations, failed registrations that unregister, open without
 * accessory requests, interface claims and Port reuse by discovery. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_runtime_initialization_options() {
    aoahid_fake_libusb_reset();
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_init_context_count() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_init_option_count() == 2U);

    aoahid_fake_libusb_init_option_record log_level{};
    aoahid_fake_libusb_init_option_record log_callback{};
    AOAHID_CHECK(aoahid_fake_libusb_get_init_option(0U, &log_level) == LIBUSB_SUCCESS);
    AOAHID_CHECK(aoahid_fake_libusb_get_init_option(1U, &log_callback) == LIBUSB_SUCCESS);
    AOAHID_CHECK(log_level.option == LIBUSB_OPTION_LOG_LEVEL);
    AOAHID_CHECK(log_level.integer_value == LIBUSB_LOG_LEVEL_NONE);
    AOAHID_CHECK(log_level.callback_present == 0);
    AOAHID_CHECK(log_callback.option == LIBUSB_OPTION_LOG_CB);
    AOAHID_CHECK(log_callback.callback_present == 1);

    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_registration_reservations_and_async_results() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(3U, {2U, 4U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    DeviceConfig config = current_mode_config();
    config.pool_slots = 3U;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    std::array<std::uint8_t, 130U> descriptor{};
    for (std::size_t index = 0U; index < descriptor.size(); ++index) {
        descriptor[index] = static_cast<std::uint8_t>(index);
    }
    AOAHID_CHECK(device->register_hid(7U, descriptor.data(), descriptor.size(),
                                      kControlTimeoutMs) == AOAHID_OK);
    const auto registrations = controls_for(54U);
    AOAHID_CHECK(registrations.size() == 1U);
    if (!registrations.empty()) {
        AOAHID_CHECK(registrations[0].request_type == 0x40U && registrations[0].value == 7U &&
                     registrations[0].index == descriptor.size() && registrations[0].length == 0U &&
                     registrations[0].asynchronous == 0);
    }
    const auto fragments = controls_for(56U);
    AOAHID_CHECK(fragments.size() == 3U);
    if (fragments.size() == 3U) {
        AOAHID_CHECK(fragments[0].value == 7U && fragments[0].index == 0U &&
                     fragments[0].length == 64U);
        AOAHID_CHECK(fragments[1].value == 7U && fragments[1].index == 64U &&
                     fragments[1].length == 64U);
        AOAHID_CHECK(fragments[2].value == 7U && fragments[2].index == 128U &&
                     fragments[2].length == 2U);
        AOAHID_CHECK(fragments[0].request_type == 0x40U && fragments[0].asynchronous == 0);
    }
    const auto descriptor_payloads = control_payloads_for(56U);
    AOAHID_CHECK(descriptor_payloads.size() == 3U);
    if (descriptor_payloads.size() == 3U) {
        AOAHID_CHECK(std::equal(descriptor_payloads[0].begin(), descriptor_payloads[0].end(),
                                descriptor.begin()));
        AOAHID_CHECK(std::equal(descriptor_payloads[1].begin(), descriptor_payloads[1].end(),
                                descriptor.begin() + 64));
        AOAHID_CHECK(std::equal(descriptor_payloads[2].begin(), descriptor_payloads[2].end(),
                                descriptor.begin() + 128));
    }

    aoa::transport::ReservationToken reservation7{};
    aoa::transport::ReservationToken reservation8{};
    aoa::transport::ReservationToken reservation9{};
    AOAHID_CHECK(device->set_reservation(7U, 1U, &reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(8U, 1U, &reservation8) == AOAHID_OK);
    CompletionState first{};
    CompletionState second{};
    CompletionState shared{};
    PreparedTransfer prepared_first{};
    PreparedTransfer prepared_second{};
    PreparedTransfer prepared_shared{};
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &shared, &completed, &prepared_shared) == AOAHID_OK);
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &first, &completed, &prepared_first) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(7U, reservation7, 3U, &first, &completed, &prepared_first) ==
                 AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &second, &completed, &prepared_second) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(8U, reservation8, 3U, &second, &completed, &prepared_second) ==
                 AOAHID_OK);
    device->abandon(prepared_shared);
    device->abandon(prepared_first);
    device->abandon(prepared_second);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation8) == AOAHID_OK);

    AOAHID_CHECK(device->set_reservation(7U, 0U, &reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(8U, 2U, &reservation8) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(9U, 1U, &reservation9) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation8) == AOAHID_OK);

    // A STALL, even on a Node's first report, is returned as is:
    // exactly one request 57 goes out and the caller decides what to do.
    CompletionState stalled{};
    PreparedTransfer stalled_transfer{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &stalled, &completed, &stalled_transfer) == AOAHID_OK);
    stalled_transfer.payload[0] = 1U;
    stalled_transfer.payload[1] = 2U;
    stalled_transfer.payload[2] = 3U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    AOAHID_CHECK(device->submit(stalled_transfer, 3U, 11U, kSendTimeoutMs) == AOAHID_OK);
    poll_until(runtime, &stalled);
    AOAHID_CHECK(stalled.result == AOAHID_ERR_STALL);
    const auto stalled_events = controls_for(57U);
    AOAHID_CHECK(stalled_events.size() == 1U);
    for (const aoahid_fake_libusb_control_record event : stalled_events) {
        AOAHID_CHECK(event.request_type == 0x40U && event.value == 7U && event.index == 0U &&
                     event.length == 3U && event.asynchronous == 1);
    }
    const auto stalled_payloads = control_payloads_for(57U);
    AOAHID_CHECK(stalled_payloads.size() == 1U);
    for (const auto& payload : stalled_payloads) {
        AOAHID_CHECK(payload == std::vector<std::uint8_t>({1U, 2U, 3U}));
    }
    AOAHID_CHECK(device->latched_error() == AOAHID_ERR_STALL);

    CompletionState short_transfer{};
    PreparedTransfer short_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &short_transfer, &completed, &short_prepared) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 2, 0U);
    AOAHID_CHECK(device->submit(short_prepared, 3U, 23U, kSendTimeoutMs) == AOAHID_OK);
    poll_until(runtime, &short_transfer);
    AOAHID_CHECK(short_transfer.result == AOAHID_ERR_SHORT_TRANSFER);
    aoa::transport::ErrorInfo short_error{};
    AOAHID_CHECK(device->latched_error(&short_error) == AOAHID_ERR_SHORT_TRANSFER);
    AOAHID_CHECK(short_error.hid_id == 7U && short_error.report_id == 23U);
    AOAHID_CHECK(device->latched_error() == AOAHID_OK);

    CompletionState overflow{};
    PreparedTransfer overflow_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &overflow, &completed, &overflow_prepared) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_OVERFLOW, 0, 0U);
    AOAHID_CHECK(device->submit(overflow_prepared, 3U, 29U, kSendTimeoutMs) == AOAHID_OK);
    poll_until(runtime, &overflow);
    AOAHID_CHECK(overflow.result == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow.native_status == LIBUSB_TRANSFER_OVERFLOW);
    aoa::transport::ErrorInfo overflow_error{};
    AOAHID_CHECK(device->latched_error(&overflow_error) == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow_error.hid_id == 7U && overflow_error.report_id == 29U);
    AOAHID_CHECK(overflow_error.length == 3U);
    AOAHID_CHECK(device->latched_error() == AOAHID_OK);

    CompletionState removed{};
    PreparedTransfer removed_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 2U, &removed, &completed, &removed_prepared) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(device->submit(removed_prepared, 2U, 47U, kSendTimeoutMs) == AOAHID_OK);
    poll_until(runtime, &removed);
    AOAHID_CHECK(removed.result == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(!device->present());
    PreparedTransfer rejected{};
    AOAHID_CHECK(device->acquire(7U, {}, 1U, &removed, &completed, &rejected) ==
                 AOAHID_ERR_NO_DEVICE);
    aoa::transport::ErrorInfo removed_error{};
    AOAHID_CHECK(device->latched_error(&removed_error) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(removed_error.hid_id == 7U && removed_error.report_id == 47U);
    AOAHID_CHECK(device->latched_error(&removed_error) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(removed_error.report_id == 47U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_uncertain_register_attempts_unregister_and_preserves_error() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(4U, {2U, 5U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    const DeviceConfig config = current_mode_config();
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 3U> descriptor{0x05U, 0x01U, 0xC0U};
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_TIMEOUT);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(19U, descriptor.data(), descriptor.size(),
                                      kControlTimeoutMs) == AOAHID_ERR_TIMEOUT);

    const auto registrations = controls_for(54U);
    const auto cleanups = controls_for(55U);
    AOAHID_CHECK(registrations.size() == 1U);
    AOAHID_CHECK(cleanups.size() == 1U);
    if (!cleanups.empty()) {
        AOAHID_CHECK(cleanups[0].value == 19U && cleanups[0].index == 0U &&
                     cleanups[0].length == 0U);
    }
    const aoa::transport::ErrorInfo failure = aoa::transport::last_error();
    AOAHID_CHECK(failure.result == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(failure.native_status == LIBUSB_ERROR_TIMEOUT);
    AOAHID_CHECK(failure.aoa_request == 54 && failure.hid_id == 19U);
    AOAHID_CHECK(failure.length == descriptor.size());

    struct RegisterFailureCase {
        int native_status;
        aoahid_result expected;
        std::uint16_t hid_id;
    };
    static constexpr std::array<RegisterFailureCase, 2U> ambiguous_failures{
        RegisterFailureCase{LIBUSB_ERROR_IO, AOAHID_ERR_IO, 20U},
        RegisterFailureCase{1, AOAHID_ERR_SHORT_TRANSFER, 21U}};
    for (const RegisterFailureCase attempt : ambiguous_failures) {
        const std::size_t cleanup_count = controls_for(55U).size();
        aoahid_fake_libusb_queue_control_result(attempt.native_status);
        aoahid_fake_libusb_queue_control_result(LIBUSB_SUCCESS);
        AOAHID_CHECK(device->register_hid(attempt.hid_id, descriptor.data(), descriptor.size(),
                                          kControlTimeoutMs) == attempt.expected);
        AOAHID_CHECK(controls_for(55U).size() == cleanup_count + 1U);
        const aoa::transport::ErrorInfo attempt_error = aoa::transport::last_error();
        AOAHID_CHECK(attempt_error.result == attempt.expected);
        AOAHID_CHECK(attempt_error.native_status == attempt.native_status);
        AOAHID_CHECK(attempt_error.aoa_request == 54 && attempt_error.hid_id == attempt.hid_id);
    }

    // A setup STALL is an explicit target rejection, unlike timeout/I/O/short
    // completion ambiguity, so there is no possibly-live request-54 entry to
    // clean up.
    const std::size_t cleanup_count = controls_for(55U).size();
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(22U, descriptor.data(), descriptor.size(),
                                      kControlTimeoutMs) == AOAHID_ERR_STALL);
    AOAHID_CHECK(controls_for(55U).size() == cleanup_count);

    // On disconnect the audited Android gadget destroys every HID registration;
    // request 55 cannot be delivered and is unnecessary.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_NO_DEVICE);
    AOAHID_CHECK(device->register_hid(24U, descriptor.data(), descriptor.size(),
                                      kControlTimeoutMs) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(controls_for(55U).size() == cleanup_count);
    AOAHID_CHECK(!device->present());

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_descriptor_failure_unregisters_and_preserves_error() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(12U, {2U, 6U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    const DeviceConfig config = current_mode_config();
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    std::array<std::uint8_t, 130U> descriptor{};
    aoahid_fake_libusb_queue_control_result(LIBUSB_SUCCESS); // request 54
    aoahid_fake_libusb_queue_control_result(64);             // request 56, offset 0
    aoahid_fake_libusb_queue_control_result(1);              // request 56, offset 64
    // The cleanup result must not replace the partial request-56 diagnostic.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(23U, descriptor.data(), descriptor.size(),
                                      kControlTimeoutMs) == AOAHID_ERR_SHORT_TRANSFER);

    const auto fragments = controls_for(56U);
    AOAHID_CHECK(fragments.size() == 2U);
    if (fragments.size() == 2U) {
        AOAHID_CHECK(fragments[0].value == 23U && fragments[0].index == 0U &&
                     fragments[0].length == 64U);
        AOAHID_CHECK(fragments[1].value == 23U && fragments[1].index == 64U &&
                     fragments[1].length == 64U);
    }
    const auto cleanups = controls_for(55U);
    AOAHID_CHECK(cleanups.size() == 1U);
    if (!cleanups.empty()) {
        AOAHID_CHECK(cleanups[0].value == 23U);
    }
    const aoa::transport::ErrorInfo failure = aoa::transport::last_error();
    AOAHID_CHECK(failure.result == AOAHID_ERR_SHORT_TRANSFER);
    AOAHID_CHECK(failure.native_status == 1);
    AOAHID_CHECK(failure.aoa_request == 56 && failure.hid_id == 23U);
    AOAHID_CHECK(failure.offset == 64U && failure.length == 64U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_error_report_id_is_published_before_user_callback_returns() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(10U, {3U, 8U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_INTERNAL_THREAD, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    Device* device = nullptr;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    BlockingCompletionState completion{};
    PreparedTransfer prepared{};
    AOAHID_CHECK(device->acquire(37U, {}, 2U, &completion, &blocking_completed, &prepared) ==
                 AOAHID_OK);
    prepared.payload[0] = 91U;
    prepared.payload[1] = 1U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(device->submit(prepared, 2U, 91U, kSendTimeoutMs) == AOAHID_OK);

    std::atomic<aoahid_result> poll_result{AOAHID_ERR_INTERNAL};
    std::thread event_thread(
        [&] { poll_result.store(runtime->poll(100U), std::memory_order_release); });
    completion.entered.wait(false, std::memory_order_acquire);

    aoa::transport::ErrorInfo error{};
    AOAHID_CHECK(device->latched_error(&error) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(error.hid_id == 37U && error.report_id == 91U);
    AOAHID_CHECK(error.length == 2U);

    completion.release.store(true, std::memory_order_release);
    completion.release.notify_all();
    event_thread.join();
    AOAHID_CHECK(poll_result.load(std::memory_order_acquire) == AOAHID_OK);
    AOAHID_CHECK(completion.result.load(std::memory_order_relaxed) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(device->drained());

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_current_mode_open_and_explicit_claim() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(5U, {1U, 6U}, 0x1234U, 0x5678U);

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.claim_policy = AOAHID_INTERFACE_CLAIM_EXPLICIT;
    config.interface_number = 7;

    Device* device = nullptr;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device) == AOAHID_OK);

    AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);

    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 1U);
    aoahid_fake_libusb_claim_record claim{};
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(0U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 7 && claim.release == 0);
    delete device;
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U);
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(1U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 7 && claim.release == 1);
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_port_ownership_and_discovery_reuse() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(5U, {1U, 6U}, 0x1234U, 0x5678U);

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_INTERNAL_THREAD, &runtime) == AOAHID_OK);

    // Each Device open owns one handle through its own Port.
    DeviceConfig config = current_mode_config();
    config.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    Device* first = nullptr;
    Device* second = nullptr;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &first) == AOAHID_OK);
    AOAHID_CHECK(Device::open(runtime, candidate, config, &second) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 2U);
    AOAHID_CHECK(first->port() != nullptr && first->port() != second->port());

    // Discovery probes an open device through its existing handle.
    const std::size_t opens_before = aoahid_fake_libusb_open_call_count();
    std::vector<Candidate> found;
    AOAHID_CHECK(runtime->discover(20U, &found) == AOAHID_OK);
    AOAHID_CHECK(found.size() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == opens_before);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 2U);
    delete first;
    delete second;
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    found.clear();
    AOAHID_CHECK(runtime->discover(20U, &found) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == opens_before + 1U);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);

    // A Port closes only with its last reference; claims are counted.
    Port* port = nullptr;
    AOAHID_CHECK(runtime->acquire_port(candidate, &port) == AOAHID_OK);
    AOAHID_CHECK(port != nullptr && port->references() == 1U);
    port->retain();
    AOAHID_CHECK(port->claim_interface(3) == LIBUSB_SUCCESS);
    AOAHID_CHECK(port->claim_interface(3) == LIBUSB_SUCCESS);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 1U);
    port->release_interface(3);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 1U);
    port->release_interface(3);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U);
    aoahid_fake_libusb_claim_record claim{};
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(1U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 3 && claim.release == 1);
    // A claim still held at the last release is released before close.
    AOAHID_CHECK(port->claim_interface(4) == LIBUSB_SUCCESS);
    port->release();
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 1U);
    port->release();
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 4U);
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(3U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 4 && claim.release == 1);

    // Opening an absent device leaves nothing behind.
    aoahid_fake_libusb_set_present(0U, 0);
    Port* missing = nullptr;
    AOAHID_CHECK(runtime->acquire_port(candidate, &missing) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(missing == nullptr && aoahid_fake_libusb_open_handle_count() == 0U);

    delete runtime;
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
