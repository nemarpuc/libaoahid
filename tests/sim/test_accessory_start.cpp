// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Accessory start: requests 51, 52 and 53 in order with their strings, and every
 * rejection and failure on the way. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_accessory_start_sends_51_52_53_and_closes() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_candidate(4U, {2U, 3U}, 0x04E8U, 0x6860U);
    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);

    aoahid_accessory_options options = accessory_options("Maker", "Model");
    options.strings.version = "1.0";
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(controls_for(51U).size() == 1U);
    const auto strings = controls_for(52U);
    const auto payloads = control_payloads_for(52U);
    // Only non-null strings are sent, each with its AOA string ID and NUL.
    AOAHID_CHECK(strings.size() == 3U && payloads.size() == 3U);
    if (strings.size() == 3U && payloads.size() == 3U) {
        AOAHID_CHECK(strings[0].index == 0U && payloads[0] == nul_terminated("Maker"));
        AOAHID_CHECK(strings[1].index == 1U && payloads[1] == nul_terminated("Model"));
        AOAHID_CHECK(strings[2].index == 3U && payloads[2] == nul_terminated("1.0"));
        for (const aoahid_fake_libusb_control_record& record : strings) {
            AOAHID_CHECK(record.request_type == 0x40U && record.value == 0U &&
                         record.timeout == 500U && record.asynchronous == 0);
        }
    }
    const auto start = controls_for(53U);
    AOAHID_CHECK(start.size() == 1U);
    if (!start.empty()) {
        AOAHID_CHECK(start[0].request_type == 0x40U && start[0].value == 0U &&
                     start[0].index == 0U && start[0].length == 0U);
    }
    // Nothing is left open and nothing waits for re-enumeration.
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    AOAHID_CHECK(controls_for(54U).empty());

    // The phone re-enumerates as an accessory on the same port; the ordinary
    // discover/open/node sequence takes over from here.
    aoahid_fake_libusb_unplug(0U);
    const Candidate accessory = add_candidate(9U, {2U, 3U}, 0x18D1U, 0x2D00U);
    aoahid_discovery* discovery = nullptr;
    AOAHID_CHECK(aoahid_discover(context, 0U, &discovery) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(discovery == nullptr);
    AOAHID_CHECK(aoahid_discover(context, 100U, &discovery) == AOAHID_OK);
    AOAHID_CHECK(aoahid_discovery_count(discovery) == 1U);
    const aoahid_device_info* found = aoahid_discovery_get(discovery, 0U);
    AOAHID_CHECK(found != nullptr && found->vendor_id == 0x18D1U && found->product_id == 0x2D00U &&
                 found->port_path_length == 2U && found->port_path[0] == 2U &&
                 found->protocol_version == 2U);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(found != nullptr &&
                 aoahid_device_open(context, found, &device_options, &device) == AOAHID_OK);
    aoahid_discovery_destroy(discovery);
    // A second start on an accessory-mode device sends nothing.
    const std::size_t before = aoahid_fake_libusb_control_count();
    const aoahid_device_info accessory_info = public_info(accessory);
    AOAHID_CHECK(aoahid_accessory_start(context, &accessory_info, &options,
                                        kAccessoryControlTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_control_count() == before);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_accessory_start_rejects_and_reports_failures() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_candidate(4U, {2U, 3U}, 0x04E8U, 0x6860U);
    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);

    aoahid_accessory_options options = accessory_options("Maker", nullptr);
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_UNSET_FIELD);
    options = accessory_options("", "Model");
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_UNSET_FIELD);
    options = accessory_options("Maker", "Model");
    options.reserved = 1U;
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_UNSET_FIELD);
    options = accessory_options("Maker", "Model");
    options.struct_size -= 1U;
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    options = accessory_options("Maker", "Model");
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, 0U) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_accessory_start(nullptr, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_accessory_start(context, nullptr, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, nullptr, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    // AOA 1.0 caps each string at 256 bytes with its NUL; an oversized one is
    // rejected before any request, and exactly 256 bytes is accepted later.
    const std::string longest(255U, 'x');
    const std::string too_long(256U, 'x');
    options = accessory_options("Maker", "Model");
    options.strings.description = too_long.c_str();
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_OVERFLOW);
    const aoahid_error_detail* overflow = aoahid_last_error();
    AOAHID_CHECK(overflow != nullptr && overflow->aoa_request == 52 && overflow->offset == 2U);
    AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);
    options = accessory_options(longest.c_str(), "Model");
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_OK);
    const auto longest_payloads = control_payloads_for(52U);
    AOAHID_CHECK(!longest_payloads.empty() && longest_payloads[0].size() == 256U);
    aoahid_fake_libusb_reset();
    static_cast<void>(add_candidate(4U, {2U, 3U}, 0x04E8U, 0x6860U));
    options = accessory_options("Maker", "Model");

    // A device without AOA support gets neither strings nor START.
    aoahid_fake_libusb_set_protocol(0U, 0U);
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_NOT_AOA);
    AOAHID_CHECK(controls_for(52U).empty() && controls_for(53U).empty());
    aoahid_fake_libusb_set_protocol(0U, 2U);

    // A failed string request stops the sequence; nothing is retried.
    aoahid_fake_libusb_queue_control_result(2);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_ERR_STALL);
    const aoahid_error_detail* detail = aoahid_last_error();
    AOAHID_CHECK(detail != nullptr && detail->aoa_request == 52 && detail->offset == 0U);
    AOAHID_CHECK(controls_for(52U).size() == 1U && controls_for(53U).empty());
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);

    // An open Device lends its handle instead of a second libusb_open.
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);
    const std::size_t opens = aoahid_fake_libusb_open_call_count();
    AOAHID_CHECK(aoahid_accessory_start(context, &selected, &options, kAccessoryControlTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == opens);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 1U);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
