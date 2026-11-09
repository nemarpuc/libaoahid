// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Every public entry point refuses a null handle, a null output, a zero
 * timeout, a malformed option structure and a handle in the wrong state with
 * the documented result, says which argument in aoahid_last_error(), and
 * changes nothing: the fake phone sees no request it should not. */
#include "sim/sim_support.hpp"

#include <array>
#include <cstring>
#include <string>

namespace {

using sim::kControlTimeoutMs;
using sim::kDrainTimeoutMs;
using sim::kSendTimeoutMs;
using sim::kThreadDrainTimeoutMs;
using sim::last_error_is;

bool field_is(const char* field) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return detail->field != nullptr && std::strcmp(detail->field, field) == 0;
}

void test_context_arguments() {
    aoahid_fake_libusb_reset();
    aoahid_context_options options = sim::context_options();
    AOAHID_CHECK(aoahid_context_create(&options, nullptr) == AOAHID_ERR_PARAM &&
                 field_is("out_context"));
    aoahid_context* context = reinterpret_cast<aoahid_context*>(&options);
    AOAHID_CHECK(aoahid_context_create(nullptr, &context) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(context == nullptr);
    options.struct_size = 1U;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_ERR_PARAM &&
                 field_is("context_options"));

    // Each invalid choice on its own.
    const auto refused = [](const aoahid_context_options& value) {
        aoahid_context* out = nullptr;
        return aoahid_context_create(&value, &out) == AOAHID_ERR_UNSET_FIELD && out == nullptr &&
               last_error_is(AOAHID_ERR_UNSET_FIELD);
    };
    options = sim::context_options();
    options.reserved = 1U;
    AOAHID_CHECK(refused(options));
    options = sim::context_options();
    options.event_mode = static_cast<aoahid_event_mode>(0);
    AOAHID_CHECK(refused(options));
    options.event_mode = static_cast<aoahid_event_mode>(3);
    AOAHID_CHECK(refused(options));
    options = sim::context_options();
    options.log_level = static_cast<aoahid_log_level>(-1);
    AOAHID_CHECK(refused(options));
    options.log_level = static_cast<aoahid_log_level>(AOAHID_LOG_TRACE + 1);
    AOAHID_CHECK(refused(options));
    options.log_level = AOAHID_LOG_ERROR; // a level with no sink
    AOAHID_CHECK(refused(options));
    AOAHID_CHECK(aoahid_fake_libusb_init_context_count() == 0U);

    AOAHID_CHECK(aoahid_context_poll(nullptr, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_context_destroy(nullptr, kControlTimeoutMs, kDrainTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_context_destroy_blocking(nullptr, kControlTimeoutMs, kDrainTimeoutMs,
                                                 kDrainTimeoutMs) == AOAHID_ERR_PARAM);

    options = sim::context_options();
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    // A zero wait is never a default: each one is refused and the Context stays usable.
    AOAHID_CHECK(aoahid_context_destroy(context, 0U, kDrainTimeoutMs) == AOAHID_ERR_UNSET_FIELD &&
                 field_is("control_timeout_ms"));
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, 0U) == AOAHID_ERR_UNSET_FIELD &&
                 field_is("drain_timeout_ms"));
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, 0U, kDrainTimeoutMs, 1U) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs, 0U, 1U) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs, kDrainTimeoutMs, 0U) ==
                     AOAHID_ERR_UNSET_FIELD &&
                 field_is("timeout_ms"));
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, kControlTimeoutMs, kDrainTimeoutMs,
                                                 kDrainTimeoutMs) == AOAHID_OK);

    // An internal-thread Context has nothing for the caller to poll.
    options = sim::context_options(AOAHID_EVENT_INTERNAL_THREAD);
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_ERR_UNSUPPORTED &&
                 field_is("context.event_mode"));
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_runtime_failures() {
    aoahid_fake_libusb_reset();
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    // libusb's own event failure is returned as it is, with the libusb status.
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_NO_MEM);
    AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_ERR_INTERNAL &&
                 aoahid_last_error()->transport_status == LIBUSB_ERROR_NO_MEM);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_INTERRUPTED);
    AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_OK);
    // A discovery whose enumeration fails reports the probe request.
    AOAHID_CHECK(aoahid_discover(context, kControlTimeoutMs, nullptr) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_discover(nullptr, kControlTimeoutMs, nullptr) == AOAHID_ERR_PARAM);
    aoahid_discovery* discovery = nullptr;
    AOAHID_CHECK(aoahid_discover(context, 0U, &discovery) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_internal_event_pump_failure_reaches_the_next_call() {
    aoahid_fake_libusb_reset();
    const sim::Phone phone = sim::add_phone();
    aoahid_context_options options = sim::context_options(AOAHID_EVENT_INTERNAL_THREAD);
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_wait_for_event_waiters(1U, 1000U) == LIBUSB_SUCCESS);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    // The pump is woken, meets the failure, waits and polls again.
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_fake_libusb_wait_for_event_calls(4U, 2000U) == LIBUSB_SUCCESS);

    // The first failure is kept for an application thread and reported once.
    const aoahid_device_options device_config = sim::device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &phone.info, &device_config, &device) ==
                     AOAHID_ERR_IO &&
                 device == nullptr && field_is("context.event_pump") &&
                 aoahid_last_error()->transport_status == LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_device_open(context, &phone.info, &device_config, &device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_discovery_arguments() {
    aoahid_fake_libusb_reset();
    static_cast<void>(sim::add_phone());
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    aoahid_discovery* discovery = nullptr;
    AOAHID_CHECK(aoahid_discover(context, kControlTimeoutMs, &discovery) == AOAHID_OK);
    AOAHID_CHECK(aoahid_discovery_count(discovery) == 1U);
    const aoahid_device_info* info = aoahid_discovery_get(discovery, 0U);
    AOAHID_CHECK(info != nullptr && info->protocol_version == 2U &&
                 std::strcmp(info->serial, "sim-serial") == 0);
    AOAHID_CHECK(aoahid_discovery_get(discovery, 1U) == nullptr &&
                 last_error_is(AOAHID_ERR_PARAM) && field_is("discovery.index") &&
                 aoahid_last_error()->offset == 1U && aoahid_last_error()->length == 1U);
    AOAHID_CHECK(aoahid_discovery_get(nullptr, 0U) == nullptr && field_is("discovery"));
    AOAHID_CHECK(aoahid_discovery_count(nullptr) == 0U && last_error_is(AOAHID_ERR_PARAM));
    aoahid_discovery_destroy(nullptr);
    AOAHID_CHECK(last_error_is(AOAHID_ERR_PARAM) && field_is("discovery"));
    aoahid_discovery_destroy(discovery);

    // Enumeration fails as a whole only when the device list cannot be read; a device that
    // cannot be opened or probed is just left out.
    aoahid_fake_libusb_set_present(0U, 0);
    AOAHID_CHECK(aoahid_discover(context, kControlTimeoutMs, &discovery) == AOAHID_OK);
    AOAHID_CHECK(aoahid_discovery_count(discovery) == 0U);
    aoahid_discovery_destroy(discovery);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_device_selection_arguments() {
    aoahid_fake_libusb_reset();
    sim::Phone phone = sim::add_phone();
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    const aoahid_device_options device_config = sim::device_options();
    aoahid_accessory_options accessory;
    aoahid_accessory_options_init(&accessory);
    aoahid_device* device = nullptr;

    // A path longer than libusb's seven ports, and a path length without a path.
    std::array<std::uint8_t, 8U> long_path{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    aoahid_device_info bad = phone.info;
    bad.port_path = long_path.data();
    bad.port_path_length = long_path.size();
    AOAHID_CHECK(aoahid_device_open(context, &bad, &device_config, &device) ==
                     AOAHID_ERR_OVERFLOW &&
                 field_is("device.port_path"));
    AOAHID_CHECK(aoahid_accessory_start(context, &bad, &accessory, kControlTimeoutMs) ==
                 AOAHID_ERR_OVERFLOW);
    bad.port_path = nullptr;
    bad.port_path_length = 2U;
    AOAHID_CHECK(aoahid_device_open(context, &bad, &device_config, &device) == AOAHID_ERR_PARAM &&
                 field_is("device.port_path"));
    AOAHID_CHECK(aoahid_accessory_start(context, &bad, &accessory, kControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);

    AOAHID_CHECK(aoahid_device_open(nullptr, &phone.info, &device_config, &device) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_device_open(context, nullptr, &device_config, &device) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_device_open(context, &phone.info, &device_config, nullptr) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_device_open(context, &phone.info, nullptr, &device) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_accessory_start(context, &phone.info, &accessory, 0U) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_accessory_start(nullptr, &phone.info, &accessory, kControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_accessory_start(context, nullptr, &accessory, kControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_accessory_start(context, &phone.info, nullptr, kControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    aoahid_accessory_options wrong_size = accessory;
    wrong_size.struct_size = 0U;
    AOAHID_CHECK(aoahid_accessory_start(context, &phone.info, &wrong_size, kControlTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == 0U);
    AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);

    // The phone is gone between discovery and open; the failure names the open.
    aoahid_fake_libusb_set_present(phone.index, 0);
    AOAHID_CHECK(aoahid_device_open(context, &phone.info, &device_config, &device) ==
                     AOAHID_ERR_NO_DEVICE &&
                 device == nullptr && last_error_is(AOAHID_ERR_NO_DEVICE));
    AOAHID_CHECK(aoahid_device_latched_error(nullptr) == AOAHID_ERR_PARAM &&
                 field_is("device.latched_error"));
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// Every tuning value and product policy is the caller's; each zero is refused on its own.
void test_device_option_fields() {
    aoahid_fake_libusb_reset();
    const sim::Phone phone = sim::add_phone();
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    const auto result_of = [&](const aoahid_device_options& value) {
        aoahid_device* device = nullptr;
        const aoahid_result result = aoahid_device_open(context, &phone.info, &value, &device);
        AOAHID_CHECK(device == nullptr || result == AOAHID_OK);
        if (device != nullptr)
            AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) ==
                         AOAHID_OK);
        return result;
    };
    using Edit = void (*)(aoahid_device_options*);
    static constexpr std::array<Edit, 20U> unset{
        [](aoahid_device_options* value) { value->reserved = 1U; },
        [](aoahid_device_options* value) { value->descriptor_fragment_bytes = 0U; },
        [](aoahid_device_options* value) { value->transfer_pool_slots = 0U; },
        [](aoahid_device_options* value) { value->maximum_report_bytes = 0U; },
        [](aoahid_device_options* value) { value->aoa_descriptor_wire_policy_bytes = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.descriptor_bytes = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.fields_per_report = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.global_stack_depth = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.usages = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.report_data_bits = 0U; },
        [](aoahid_device_options* value) { value->linux_hid.report_size_bits = 0U; },
        [](aoahid_device_options* value) { value->target_ep0_data_policy_bytes = 0U; },
        [](aoahid_device_options* value) { value->host_control_buffer_policy_bytes = 0U; },
        [](aoahid_device_options* value) { value->validate_reports = 2U; },
        [](aoahid_device_options* value) {
            value->interface_claim_policy = static_cast<aoahid_interface_claim_policy>(0);
        },
        [](aoahid_device_options* value) { value->interface_number = 0; },
        [](aoahid_device_options* value) {
            value->interface_claim_policy = AOAHID_INTERFACE_CLAIM_EXPLICIT;
            value->interface_number = -1;
        },
        [](aoahid_device_options* value) {
            value->interface_claim_policy = AOAHID_INTERFACE_CLAIM_EXPLICIT;
            value->interface_number = 256;
        },
        [](aoahid_device_options* value) {
            value->interface_claim_policy = static_cast<aoahid_interface_claim_policy>(3);
        },
        [](aoahid_device_options* value) { value->struct_size = 0U; },
    };
    for (std::size_t index = 0U; index < unset.size(); ++index) {
        aoahid_device_options value = sim::device_options();
        unset[index](&value);
        const aoahid_result expected =
            index + 1U == unset.size() ? AOAHID_ERR_PARAM : AOAHID_ERR_UNSET_FIELD;
        AOAHID_CHECK(result_of(value) == expected);
    }
    // Sizes that do not fit the AOA 16-bit fields or the other policies overflow.
    static constexpr std::array<Edit, 7U> overflow{
        [](aoahid_device_options* value) { value->descriptor_fragment_bytes = 65536U; },
        [](aoahid_device_options* value) { value->maximum_report_bytes = 65536U; },
        [](aoahid_device_options* value) { value->aoa_descriptor_wire_policy_bytes = 65536U; },
        [](aoahid_device_options* value) {
            value->descriptor_fragment_bytes = value->target_ep0_data_policy_bytes + 1U;
        },
        [](aoahid_device_options* value) {
            value->target_ep0_data_policy_bytes = 8192U;
            value->descriptor_fragment_bytes = value->host_control_buffer_policy_bytes + 1U;
        },
        [](aoahid_device_options* value) {
            value->maximum_report_bytes = value->target_ep0_data_policy_bytes + 1U;
        },
        [](aoahid_device_options* value) {
            value->target_ep0_data_policy_bytes = 8192U;
            value->maximum_report_bytes = value->host_control_buffer_policy_bytes + 1U;
        },
    };
    for (const Edit edit : overflow) {
        aoahid_device_options value = sim::device_options();
        edit(&value);
        AOAHID_CHECK(result_of(value) == AOAHID_ERR_OVERFLOW && field_is("device.size_policies"));
    }
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == 0U);
    AOAHID_CHECK(result_of(sim::device_options()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_accessory_option_fields() {
    aoahid_fake_libusb_reset();
    const sim::Phone phone = sim::add_phone(0x4EE7U);
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    const auto start = [&](const aoahid_accessory_options& value) {
        return aoahid_accessory_start(context, &phone.info, &value, kControlTimeoutMs);
    };
    aoahid_accessory_options value;
    aoahid_accessory_options_init(&value);
    value.reserved = 1U;
    AOAHID_CHECK(start(value) == AOAHID_ERR_UNSET_FIELD);
    aoahid_accessory_options_init(&value);
    value.strings.manufacturer = nullptr;
    AOAHID_CHECK(start(value) == AOAHID_ERR_UNSET_FIELD);
    value.strings.manufacturer = "";
    AOAHID_CHECK(start(value) == AOAHID_ERR_UNSET_FIELD);
    aoahid_accessory_options_init(&value);
    value.strings.model = nullptr;
    AOAHID_CHECK(start(value) == AOAHID_ERR_UNSET_FIELD);
    value.strings.model = "";
    AOAHID_CHECK(start(value) == AOAHID_ERR_UNSET_FIELD);
    // Each of the six strings is limited to 256 bytes with its NUL; the index says which.
    const std::string longest(255U, 'a');
    const std::string too_long(256U, 'a');
    for (std::uint32_t index = 0U; index < 6U; ++index) {
        aoahid_accessory_options_init(&value);
        const char** strings[6U] = {&value.strings.manufacturer, &value.strings.model,
                                    &value.strings.description,  &value.strings.version,
                                    &value.strings.uri,          &value.strings.serial};
        *strings[index] = too_long.c_str();
        AOAHID_CHECK(start(value) == AOAHID_ERR_OVERFLOW && field_is("accessory_options.strings") &&
                     aoahid_last_error()->aoa_request == 52 &&
                     aoahid_last_error()->offset == index);
        *strings[index] = longest.c_str();
        AOAHID_CHECK(start(value) == AOAHID_OK);
    }
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_arguments() {
    aoahid_fake_libusb_reset();
    aoahid_spec* keyboard = sim::make_keyboard_spec();
    aoahid_spec* raw = sim::make_raw_spec();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr));
    const aoahid_node_options options = sim::node_options();
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, &options, kControlTimeoutMs, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("out_node"));
    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, &options, 0U, &node) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_open(nullptr, keyboard, &options, kControlTimeoutMs, &node) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_node_open(session.device, nullptr, &options, kControlTimeoutMs, &node) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, nullptr, kControlTimeoutMs, &node) ==
                 AOAHID_ERR_PARAM);
    aoahid_node_options wrong_size = options;
    wrong_size.struct_size = 4U;
    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, &wrong_size, kControlTimeoutMs,
                                  &node) == AOAHID_ERR_PARAM &&
                 field_is("node_options"));
    AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);

    // A rejected request 54 opens no Node and leaves no reservation behind.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, &options, kControlTimeoutMs, &node) ==
                     AOAHID_ERR_STALL &&
                 node == nullptr && aoahid_last_error()->aoa_request == 54);

    AOAHID_CHECK(aoahid_node_open(session.device, keyboard, &options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_id(nullptr) == 0U && last_error_is(AOAHID_ERR_PARAM));
    AOAHID_CHECK(aoahid_node_id(node) != 0U);
    aoahid_capability_manifest manifest;
    aoahid_capability_manifest_init(&manifest);
    AOAHID_CHECK(aoahid_node_manifest(nullptr, &manifest) == AOAHID_ERR_PARAM &&
                 field_is("node.manifest"));
    AOAHID_CHECK(aoahid_node_manifest(node, &manifest) == AOAHID_OK &&
                 manifest.profile_kind == AOAHID_PROFILE_KEYBOARD &&
                 manifest.input_supported == 1U);
    AOAHID_CHECK(aoahid_node_close(nullptr, kControlTimeoutMs, kDrainTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_node_close(node, 0U, kDrainTimeoutMs) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, 0U) == AOAHID_ERR_UNSET_FIELD);

    // Submitting: a raw Node has no generated report, and a Node takes one report at a time.
    AOAHID_CHECK(aoahid_node_submit(nullptr, kSendTimeoutMs) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_node_submit(node, 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_node_submit_blocking(nullptr, kSendTimeoutMs) == AOAHID_ERR_PARAM &&
                 field_is("node.submit_blocking"));
    aoahid_node* raw_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(session.device, raw, &options, kControlTimeoutMs, &raw_node) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(raw_node, kSendTimeoutMs) == AOAHID_ERR_UNSUPPORTED &&
                 field_is("node.submit"));
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 50U);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(node, kSendTimeoutMs) == AOAHID_ERR_BUSY &&
                 field_is("node.submit"));

    // Raw reports: the timeout, the Node kind, the pointer and the declared lengths.
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    AOAHID_CHECK(aoahid_raw_submit(raw_node, report.data(), report.size(), 0U) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_raw_submit(nullptr, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, nullptr, report.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, report.data(), 1U, kSendTimeoutMs) ==
                     AOAHID_ERR_PARAM &&
                 field_is("raw.report"));
    const std::array<std::uint8_t, 2U> wrong_id{2U, 1U};
    AOAHID_CHECK(aoahid_raw_submit(raw_node, wrong_id.data(), wrong_id.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_PARAM);
    // The pool has two slots; the keyboard holds one. A rejected submit returns the slot.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, report.data(), report.size(), kSendTimeoutMs) ==
                     AOAHID_ERR_BUSY &&
                 field_is("raw.submit") &&
                 aoahid_last_error()->transport_status == LIBUSB_ERROR_BUSY);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 50U);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(raw);
    aoahid_fake_libusb_reset();
}

void test_channel_arguments() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr, AOAHID_EVENT_CALLER_POLL, true));
    const aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, nullptr) ==
                     AOAHID_ERR_PARAM &&
                 field_is("out_channel"));
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, 0U, &channel) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_channel_open(nullptr, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_channel_open(session.device, nullptr, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_PARAM);
    using Edit = void (*)(aoahid_channel_options*);
    static constexpr std::array<Edit, 9U> unset{
        [](aoahid_channel_options* value) { value->reserved = 1U; },
        [](aoahid_channel_options* value) { value->reserved8 = 1U; },
        [](aoahid_channel_options* value) { value->zero_length_termination = 2U; },
        [](aoahid_channel_options* value) {
            value->read_mode = static_cast<aoahid_channel_read_mode>(0);
        },
        [](aoahid_channel_options* value) {
            value->read_mode = static_cast<aoahid_channel_read_mode>(3);
        },
        [](aoahid_channel_options* value) { value->out_transfers = 0U; },
        [](aoahid_channel_options* value) { value->transfer_bytes = 0U; },
        [](aoahid_channel_options* value) { value->in_transfers = 0U; },
        [](aoahid_channel_options* value) { value->struct_size = 0U; },
    };
    for (std::size_t index = 0U; index < unset.size(); ++index) {
        aoahid_channel_options value = options;
        unset[index](&value);
        const aoahid_result expected =
            index + 1U == unset.size() ? AOAHID_ERR_PARAM : AOAHID_ERR_UNSET_FIELD;
        AOAHID_CHECK(aoahid_channel_open(session.device, &value, kDrainTimeoutMs, &channel) ==
                     expected);
    }
    // A request-read Channel needs no read-ahead transfers.
    aoahid_channel_options request = options;
    request.read_mode = AOAHID_CHANNEL_READ_REQUEST;
    request.in_transfers = 0U;
    AOAHID_CHECK(aoahid_channel_open(session.device, &request, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_channel_close(channel, 0U) == AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(aoahid_channel_close(nullptr, kDrainTimeoutMs) == AOAHID_ERR_PARAM);
    std::size_t moved = 7U;
    const std::array<std::uint8_t, 1U> byte{0x55U};
    AOAHID_CHECK(aoahid_channel_write(nullptr, byte.data(), byte.size(), &moved, 1U) ==
                     AOAHID_ERR_PARAM &&
                 moved == 0U && field_is("channel.write"));
    AOAHID_CHECK(aoahid_channel_write(nullptr, byte.data(), byte.size(), nullptr, 1U) ==
                 AOAHID_ERR_PARAM);
    moved = 7U;
    std::array<std::uint8_t, 8U> buffer{};
    AOAHID_CHECK(aoahid_channel_read(nullptr, buffer.data(), buffer.size(), &moved, 1U) ==
                     AOAHID_ERR_PARAM &&
                 moved == 0U && field_is("channel.read"));
    AOAHID_CHECK(aoahid_channel_read(nullptr, buffer.data(), buffer.size(), nullptr, 1U) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);

    // No interface with the class triple: UNSUPPORTED, and nothing is claimed.
    aoahid_channel_options other = options;
    other.interface_subclass = 0x43U;
    AOAHID_CHECK(aoahid_channel_open(session.device, &other, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_UNSUPPORTED);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// A Channel waits for reports in flight before it claims its interface; a report that does
// not complete in that budget makes the open time out.
void test_channel_open_waits_for_reports() {
    aoahid_fake_libusb_reset();
    aoahid_spec* raw = sim::make_raw_spec();
    sim::Session session{};
    AOAHID_CHECK(session.open(raw, AOAHID_EVENT_CALLER_POLL, true));
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    AOAHID_CHECK(aoahid_raw_submit(session.node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    const aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, 2U, &channel) ==
                     AOAHID_ERR_TIMEOUT &&
                 channel == nullptr && field_is("channel.open"));
    // An event failure while waiting is returned instead.
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, 50U, &channel) == AOAHID_ERR_IO &&
                 field_is("context.poll"));
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_spec_release(raw);
    aoahid_fake_libusb_reset();
}

// A Channel whose cancelled transfers do not complete stays the caller's, and a later close
// with a working pump finishes it.
void test_channel_close_pending() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr, AOAHID_EVENT_CALLER_POLL, true));
    const aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_CLOSE_PENDING &&
                 field_is("channel.close") &&
                 aoahid_last_error()->transport_status == LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// A Device whose close could not drain waits in the Context's graveyard; until the Context
// reaps it, its handle and its Channel's are refused instead of used.
void test_closed_device_handle_is_refused() {
    aoahid_fake_libusb_reset();
    aoahid_spec* raw = sim::make_raw_spec();
    sim::Session session{};
    AOAHID_CHECK(session.open(raw, AOAHID_EVENT_CALLER_POLL, true));
    const aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    AOAHID_CHECK(aoahid_raw_submit(session.node, report.data(), report.size(), kSendTimeoutMs) ==
                 AOAHID_OK);
    // The report's cancellation is refused, so it stays in flight past the close.
    aoahid_fake_libusb_queue_cancel_result(LIBUSB_ERROR_NOT_FOUND);
    aoahid_device* parked = session.device;
    AOAHID_CHECK(aoahid_device_close(parked, kControlTimeoutMs, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(parked, kControlTimeoutMs, 1U) == AOAHID_ERR_PARAM &&
                 field_is("device.close"));
    AOAHID_CHECK(aoahid_device_latched_error(parked) == AOAHID_ERR_PARAM);
    aoahid_node* node = nullptr;
    const aoahid_node_options node_config = sim::node_options();
    AOAHID_CHECK(aoahid_node_open(parked, raw, &node_config, kControlTimeoutMs, &node) ==
                     AOAHID_ERR_PARAM &&
                 field_is("device"));
    AOAHID_CHECK(aoahid_channel_open(parked, &options, kDrainTimeoutMs, &channel) ==
                     AOAHID_ERR_PARAM &&
                 field_is("device"));
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_ERR_PARAM);
    // Once the transfer completes, a poll reaps the Device.
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(session.context, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    session.device = nullptr;
    session.node = nullptr;
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_spec_release(raw);
    aoahid_fake_libusb_reset();
}

void test_result_names() {
    struct Name {
        aoahid_result result;
        const char* name;
    };
    static constexpr std::array<Name, 16U> names{{
        {AOAHID_OK, "AOAHID_OK"},
        {AOAHID_ERR_PARAM, "AOAHID_ERR_PARAM"},
        {AOAHID_ERR_UNSET_FIELD, "AOAHID_ERR_UNSET_FIELD"},
        {AOAHID_ERR_UNSUPPORTED, "AOAHID_ERR_UNSUPPORTED"},
        {AOAHID_ERR_NOT_AOA, "AOAHID_ERR_NOT_AOA"},
        {AOAHID_ERR_ACCESS, "AOAHID_ERR_ACCESS"},
        {AOAHID_ERR_BUSY, "AOAHID_ERR_BUSY"},
        {AOAHID_ERR_NO_DEVICE, "AOAHID_ERR_NO_DEVICE"},
        {AOAHID_ERR_STALL, "AOAHID_ERR_STALL"},
        {AOAHID_ERR_TIMEOUT, "AOAHID_ERR_TIMEOUT"},
        {AOAHID_ERR_SHORT_TRANSFER, "AOAHID_ERR_SHORT_TRANSFER"},
        {AOAHID_ERR_DESCRIPTOR_REJECTED, "AOAHID_ERR_DESCRIPTOR_REJECTED"},
        {AOAHID_ERR_IO, "AOAHID_ERR_IO"},
        {AOAHID_ERR_OVERFLOW, "AOAHID_ERR_OVERFLOW"},
        {AOAHID_CLOSE_PENDING, "AOAHID_CLOSE_PENDING"},
        {AOAHID_ERR_INTERNAL, "AOAHID_ERR_INTERNAL"},
    }};
    for (const Name& entry : names) {
        AOAHID_CHECK(std::strcmp(aoahid_result_name(entry.result), entry.name) == 0 &&
                     last_error_is(AOAHID_OK));
    }
    // 5 is a gap in the enum, kept so no value is reused.
    for (const int value : {5, -1, 17, 1000}) {
        AOAHID_CHECK(std::strcmp(aoahid_result_name(static_cast<aoahid_result>(value)),
                                 "AOAHID_RESULT_UNKNOWN") == 0 &&
                     last_error_is(AOAHID_ERR_PARAM) && field_is("result"));
    }
    AOAHID_CHECK(aoahid_version() == ((AOAHID_VERSION_MAJOR << 24U) |
                                      (AOAHID_VERSION_MINOR << 16U) | AOAHID_VERSION_PATCH));
    AOAHID_CHECK(last_error_is(AOAHID_OK));
}

} // namespace

void test_api_arguments() {
    test_context_arguments();
    test_runtime_failures();
    test_internal_event_pump_failure_reaches_the_next_call();
    test_discovery_arguments();
    test_device_selection_arguments();
    test_device_option_fields();
    test_accessory_option_fields();
    test_node_arguments();
    test_channel_arguments();
    test_channel_open_waits_for_reports();
    test_channel_close_pending();
    test_closed_device_handle_is_refused();
    test_result_names();
}
