// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Touch Scan Time across frames and rejected first attempts, and battery reports in
 * both event modes and behind a touch frame. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_public_touch_scan_time_lifecycle() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_touch_session(&context, &device, &node, true);

    const auto* scan_time = profile_field(node->spec, aoa::hid::FieldSemantic::scan_time);
    AOAHID_CHECK(scan_time != nullptr);
    auto* touch = std::get_if<aoa::detail::TouchState>(&node->state);
    AOAHID_CHECK(touch != nullptr);
    if (scan_time == nullptr || touch == nullptr) {
        AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                     AOAHID_OK);
        AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                     AOAHID_OK);
        aoahid_fake_libusb_reset();
        return;
    }

    // Put the epoch one full 16-bit cycle plus 1000 ticks in the past. The
    // accepted value must therefore prove both nonzero progression and modulo
    // wrap without sleeping or assuming an OS/libusb latency.
    constexpr std::uint64_t kModulus = 65'536U;
    constexpr std::uint64_t kElapsedTicks = kModulus + 1'000U;
    constexpr auto kTick = std::chrono::microseconds{100};
    const auto epoch = std::chrono::steady_clock::now() - kTick * kElapsedTicks;
    touch->scan_epoch = epoch;
    touch->scan_epoch_active = true;

    const auto before_submit = std::chrono::steady_clock::now();
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    const auto after_submit = std::chrono::steady_clock::now();
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U) {
        const std::uint64_t first = extract_report_value(payloads[0], *scan_time);
        const std::uint64_t continuation = extract_report_value(payloads[1], *scan_time);
        AOAHID_CHECK(first == continuation);
        AOAHID_CHECK(first != 0U);

        const auto ticks_since_epoch = [epoch](const auto point) {
            return static_cast<std::uint64_t>(
                       std::chrono::duration_cast<std::chrono::microseconds>(point - epoch)
                           .count()) /
                   100U;
        };
        const std::uint64_t first_possible_tick = ticks_since_epoch(before_submit);
        const std::uint64_t last_possible_tick = ticks_since_epoch(after_submit);
        AOAHID_CHECK(first_possible_tick >= kModulus);
        const auto in_modulo_window = [](const std::uint64_t value, const std::uint64_t first_tick,
                                         const std::uint64_t last_tick) {
            if (last_tick - first_tick >= kModulus)
                return true;
            const std::uint64_t first_modulo = first_tick % kModulus;
            const std::uint64_t last_modulo = last_tick % kModulus;
            return first_modulo <= last_modulo ? value >= first_modulo && value <= last_modulo
                                               : value >= first_modulo || value <= last_modulo;
        };
        AOAHID_CHECK(in_modulo_window(first, first_possible_tick, last_possible_tick));
    }

    AOAHID_CHECK(aoahid_touchscreen_contact(node, 1U, 0U, 100, 200, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchscreen_contact(node, 2U, 0U, 300, 400, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    AOAHID_CHECK(!touch->scan_epoch_active);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 4U);

    const aoa::detail::TouchContact next{3U, 500, 600, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(node, next.contact_id, 1U, next.x, next.y, nullptr) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 5U);
    if (payloads.size() == 5U) {
        AOAHID_CHECK(extract_report_value(payloads.back(), *scan_time) == 0U);
    }

    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, session_drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_touch_scan_time_rejected_first_attempt_stays_zero() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(15U, {5U, 5U});

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
    aoahid_device_options device_options = public_device_options();
    device_options.transfer_pool_slots = 1U;
    const std::uint32_t drain_timeout_ms = 100U;
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 21U> raw_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
        0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report raw_report{{1U, 1U, {}}, 2U};
    aoahid_raw_options raw_options{};
    raw_options.struct_size = sizeof(raw_options);
    raw_options.descriptor = raw_descriptor.data();
    raw_options.descriptor_length = raw_descriptor.size();
    raw_options.reports = &raw_report;
    raw_options.report_count = 1U;
    raw_options.acknowledges_no_android_support = 1U;
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &raw_spec) == AOAHID_OK);

    aoahid_touchscreen_options touchscreen_options{};
    touchscreen_options.struct_size = sizeof(touchscreen_options);
    touchscreen_options.maximum_contacts = 1U;
    touchscreen_options.contacts_per_report = 1U;
    touchscreen_options.contact_id = {0, 15, 4U, {}};
    touchscreen_options.x = {0, 1000, 16U, {}};
    touchscreen_options.y = {0, 1000, 16U, {}};
    touchscreen_options.contact_count = {0, 1, 1U, {}};
    touchscreen_options.enable_scan_time = 1U;
    touchscreen_options.scan_time = {0, 65535, 16U, {}};
    touchscreen_options.scan_time_unit_100us = 1U;
    aoahid_spec* touch_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touchscreen_options, &touch_spec) == AOAHID_OK);
    const auto* scan_time = profile_field(touch_spec, aoa::hid::FieldSemantic::scan_time);
    AOAHID_CHECK(scan_time != nullptr);

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* raw_node = nullptr;
    aoahid_node* touch_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, raw_spec, &node_options, kControlTimeoutMs, &raw_node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_open(device, touch_spec, &node_options, kControlTimeoutMs,
                                  &touch_node) == AOAHID_OK);
    const aoa::detail::TouchContact contact{1U, 100, 200, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touchscreen_contact(touch_node, contact.contact_id, 1U, contact.x,
                                            contact.y, nullptr) == AOAHID_OK);
    auto* touch = std::get_if<aoa::detail::TouchState>(&touch_node->state);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    static constexpr std::array<std::uint8_t, 2U> raw_payload{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(raw_payload.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, raw_payload.data(), raw_payload.size(),
                                   kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
    AOAHID_CHECK(aoahid_node_submit(touch_node, kSendTimeoutMs) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);

    // A libusb submission rejected synchronously has the same unaccepted-frame
    // semantics as an acquire failure and must roll the provisional epoch back.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_node_submit(touch_node, kSendTimeoutMs) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    AOAHID_CHECK(aoahid_node_submit_blocking(touch_node, 100U) == AOAHID_OK);
    const auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && scan_time != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads.back(), *scan_time) == 0U);
    }

    AOAHID_CHECK(aoahid_node_close(touch_node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(raw_node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_spec_release(touch_spec);
    aoahid_spec_release(raw_spec);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// The battery report shares the Node with the input report: input goes first,
// one report per submit, and the battery never rides along on close.
void test_battery_reports() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(15U, {5U, 6U});
    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(candidate);
    aoahid_device* device = nullptr;
    aoahid_device_options device_options = public_device_options();
    const std::uint32_t drain_timeout_ms = 2000U;
    device_options.validate_reports = 1U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.report_id = {1U, 1U, {0U, 0U, 0U}};
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.enable_battery = 1U;
    keyboard.battery.report_id = {1U, 2U, {0U, 0U, 0U}};
    keyboard.battery.strength = {1, 100, 8U, {}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);

    using Reports = std::vector<std::vector<std::uint8_t>>;
    const auto battery = [](const std::uint8_t value) {
        return std::vector<std::uint8_t>{2U, value};
    };
    std::size_t seen = 0U;
    // The request-57 payloads sent since the last call.
    const auto fresh = [&] {
        Reports all = control_payloads_for(57U);
        Reports added(all.begin() + static_cast<std::ptrdiff_t>(std::min(seen, all.size())),
                      all.end());
        seen = all.size();
        return added;
    };
    const auto is_key_report = [](const std::vector<std::uint8_t>& report) {
        return report.size() == 15U && report[0] == 1U;
    };

    // Nothing was set: nothing is sent.
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    AOAHID_CHECK(fresh().empty());

    // Battery alone.
    AOAHID_CHECK(aoahid_battery_update(node, 80) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    Reports sent = fresh();
    AOAHID_CHECK(sent.size() == 1U && sent[0] == battery(80U));

    // Input first, then the battery, from one blocking submit.
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_battery_update(node, 79) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(sent.size() == 2U && is_key_report(sent[0]) && sent[1] == battery(79U));

    // One report per aoahid_node_submit: input, then battery, then nothing.
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_battery_update(node, 78) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(sent.size() == 1U && is_key_report(sent[0]));
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(sent.size() == 1U && sent[0] == battery(78U));
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(fresh().empty());

    // The same value again is not sent.
    AOAHID_CHECK(aoahid_battery_update(node, 78) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    AOAHID_CHECK(fresh().empty());

    // A STALLed battery report stays pending and goes out on the next submit;
    // no input report is produced by it.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    AOAHID_CHECK(aoahid_battery_update(node, 77) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(!sent.empty() && sent.back() == battery(77U));
    AOAHID_CHECK(std::none_of(sent.begin(), sent.end(), is_key_report));
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    AOAHID_CHECK(fresh().empty());

    // While the battery report is in flight an input update is refused like
    // any other, and accepted once it has completed.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 2, 3U);
    AOAHID_CHECK(aoahid_battery_update(node, 76) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x05U, 1U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_battery_update(node, 75) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x05U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 500U) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(sent.size() == 2U && sent[0] == battery(76U) && is_key_report(sent[1]));

    // Close releases the held key and drops the unsent battery value.
    AOAHID_CHECK(aoahid_battery_update(node, 74) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    sent = fresh();
    AOAHID_CHECK(sent.size() == 1U && is_key_report(sent[0]) &&
                 std::all_of(sent[0].begin() + 1, sent[0].end(),
                             [](const std::uint8_t byte) { return byte == 0U; }));

    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// A touch frame of two packets is not split by the battery report.
void test_battery_waits_for_the_touch_frame() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_touch_session(&context, &device, &node, false, true);
    AOAHID_CHECK(aoahid_battery_update(node, 50) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 2000U) == AOAHID_OK);
    const auto sent = control_payloads_for(57U);
    AOAHID_CHECK(sent.size() == 3U);
    if (sent.size() == 3U) {
        AOAHID_CHECK(sent[0][0] == 1U && sent[1][0] == 1U);
        AOAHID_CHECK(sent[2] == (std::vector<std::uint8_t>{2U, 50U}));
    }
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, session_drain_timeout_ms) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// The same ordering with completions arriving on the internal event thread.
void test_battery_reports_internal_thread() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(16U, {5U, 7U});
    aoahid_context_options context_options = public_context_options();
    context_options.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(candidate);
    aoahid_device_options device_options = public_device_options();
    const std::uint32_t drain_timeout_ms = 2000U;
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.report_id = {1U, 1U, {0U, 0U, 0U}};
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.enable_battery = 1U;
    keyboard.battery.report_id = {1U, 2U, {0U, 0U, 0U}};
    keyboard.battery.strength = {1, 100, 8U, {}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);

    for (std::int32_t level = 100; level > 80; --level) {
        AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_battery_update(node, level) == AOAHID_OK);
        AOAHID_CHECK(aoahid_node_submit_blocking(node, 2000U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 0U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_node_submit_blocking(node, 2000U) == AOAHID_OK);
    }
    const auto sent = control_payloads_for(57U);
    // Each round: key down, battery, key up.
    AOAHID_CHECK(sent.size() == 60U);
    for (std::size_t round = 0U; round < 20U && sent.size() == 60U; ++round) {
        AOAHID_CHECK(sent[round * 3U][0] == 1U && sent[round * 3U + 2U][0] == 1U);
        AOAHID_CHECK(sent[round * 3U + 1U] ==
                     (std::vector<std::uint8_t>{2U, static_cast<std::uint8_t>(100U - round)}));
    }
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, drain_timeout_ms) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
