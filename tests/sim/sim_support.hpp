// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Helpers the sim tests share: one fake phone, the options a public session
 * opens with, and the Specs the tests send through. Everything goes through
 * the public C ABI; nothing here talks to a real USB device. */
#pragma once

#include "aoahid.h"
#include "fake_libusb/libusb.h"
#include "test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sim {

// Every wait is an argument of the call and has no default; these are the tests' own choices.
inline constexpr std::uint32_t kControlTimeoutMs = 20U;
inline constexpr std::uint32_t kSendTimeoutMs = 20U;
inline constexpr std::uint32_t kDrainTimeoutMs = 20U;
// The upper bound of a close that waits for the internal event thread.
inline constexpr std::uint32_t kThreadDrainTimeoutMs = 2000U;

inline constexpr std::uint8_t kAdbEndpointIn = 0x81U;
inline constexpr std::uint8_t kAdbEndpointOut = 0x01U;
inline constexpr std::uint16_t kAdbMaxPacket = 512U;

inline constexpr std::array<std::uint8_t, 2U> kPhonePath{3U, 4U};

// One fake phone in accessory mode (0x18D1:0x2D00, AOA protocol 2) at bus 1, address 5.
struct Phone {
    std::size_t index{};
    aoahid_device_info info{};
};

inline Phone add_phone(const std::uint16_t product_id = 0x2D00U, const bool adb = false) {
    const aoahid_fake_libusb_device_config fake{
        1U,         5U, kPhonePath.data(), kPhonePath.size(), 0x18D1U,
        product_id, 2U, "sim-serial",      "Android",         LIBUSB_SUCCESS};
    const int index = aoahid_fake_libusb_add_device(&fake);
    AOAHID_CHECK(index >= 0);
    if (adb) {
        // The ADB interface, as AOSP adb.h defines it: 0xFF/0x42/0x01.
        const aoahid_fake_libusb_bulk_interface interface {
            1U, 0xFFU, 0x42U, 0x01U, kAdbEndpointIn, kAdbEndpointOut, kAdbMaxPacket
        };
        aoahid_fake_libusb_add_bulk_interface(static_cast<std::size_t>(index), &interface);
    }
    Phone phone{};
    phone.index = static_cast<std::size_t>(index);
    phone.info = aoahid_device_info{
        fake.bus,        fake.address, kPhonePath.data(), kPhonePath.size(), fake.vendor_id,
        fake.product_id, 0U,           fake.serial,       fake.product};
    return phone;
}

inline aoahid_context_options
context_options(const aoahid_event_mode mode = AOAHID_EVENT_CALLER_POLL) {
    aoahid_context_options options{};
    options.struct_size = sizeof(options);
    options.event_mode = mode;
    options.log_level = AOAHID_LOG_DISABLED;
    return options;
}

inline aoahid_device_options device_options() {
    aoahid_device_options options{};
    options.struct_size = sizeof(options);
    options.descriptor_fragment_bytes = 64U;
    options.transfer_pool_slots = 2U;
    options.maximum_report_bytes = 64U;
    options.validate_reports = 1U;
    options.aoa_descriptor_wire_policy_bytes = 1024U;
    options.linux_hid.descriptor_bytes = 1024U;
    options.linux_hid.fields_per_report = 256U;
    options.linux_hid.global_stack_depth = 4U;
    options.linux_hid.usages = 12288U;
    options.linux_hid.report_data_bits = 65528U;
    options.linux_hid.report_size_bits = 256U;
    options.target_ep0_data_policy_bytes = 4096U;
    options.host_control_buffer_policy_bytes = 4096U;
    options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    options.interface_number = -1;
    return options;
}

// The ADB interface read as a stream of one-packet transfers.
inline aoahid_channel_options adb_stream_options() {
    aoahid_channel_options options;
    aoahid_channel_options_init(&options);
    options.read_mode = AOAHID_CHANNEL_READ_STREAM;
    options.in_transfers = 2U;
    options.transfer_bytes = kAdbMaxPacket;
    return options;
}

inline aoahid_node_options node_options() {
    aoahid_node_options options;
    aoahid_node_options_init(&options);
    return options;
}

// Report ID 1 with one 1-bit button: a 2-byte report.
inline constexpr std::array<std::uint8_t, 21U> kRawDescriptor{
    0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
    0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
inline constexpr aoahid_raw_report kRawReport{{1U, 1U, {}}, 2U};

inline aoahid_raw_options raw_options() {
    aoahid_raw_options options;
    aoahid_raw_options_init(&options);
    options.descriptor = kRawDescriptor.data();
    options.descriptor_length = kRawDescriptor.size();
    options.reports = &kRawReport;
    options.report_count = 1U;
    options.acknowledges_no_android_support = 1U;
    return options;
}

// Returns null and counts a failure when the Spec cannot be made.
inline aoahid_spec* make_raw_spec() {
    const aoahid_raw_options options = raw_options();
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&options, &spec) == AOAHID_OK);
    return spec;
}

inline aoahid_spec* make_keyboard_spec() {
    aoahid_keyboard_options options;
    aoahid_keyboard_options_init(&options);
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    return spec;
}

// A Context, a Device on one fake phone and, when a Spec is given, a Node.
struct Session {
    aoahid_context* context{};
    aoahid_device* device{};
    aoahid_node* node{};
    Phone phone{};

    bool open(aoahid_spec* spec, const aoahid_event_mode mode = AOAHID_EVENT_CALLER_POLL,
              const bool adb = false) {
        phone = add_phone(0x2D00U, adb);
        const aoahid_context_options context_config = context_options(mode);
        if (aoahid_context_create(&context_config, &context) != AOAHID_OK)
            return false;
        const aoahid_device_options device_config = device_options();
        if (aoahid_device_open(context, &phone.info, &device_config, &device) != AOAHID_OK)
            return false;
        if (spec == nullptr)
            return true;
        const aoahid_node_options options = node_options();
        return aoahid_node_open(device, spec, &options, kControlTimeoutMs, &node) == AOAHID_OK;
    }

    // Closes what open() made; the Context's destroy consumes any Device and Node left.
    aoahid_result close(const std::uint32_t drain_timeout_ms = kThreadDrainTimeoutMs) {
        if (context == nullptr)
            return AOAHID_OK;
        const aoahid_result result = aoahid_context_destroy_blocking(
            context, kControlTimeoutMs, kDrainTimeoutMs, drain_timeout_ms);
        context = nullptr;
        device = nullptr;
        node = nullptr;
        return result;
    }
};

// True when the last error names the result the call returned.
inline bool last_error_is(const aoahid_result result) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return detail != nullptr && detail->code == result && detail->reason != nullptr;
}

} // namespace sim
