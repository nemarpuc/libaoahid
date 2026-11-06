// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: discovery, accessory start and Device lifetime.
 */

#include "api/support.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

using aoa::detail::completion_reason;
using aoa::detail::copy_candidate;
using aoa::detail::copy_config;
using aoa::detail::device_close_impl;
using aoa::detail::find_or_create_hid_id_domain;
using aoa::detail::finish_transport_result;
using aoa::detail::preflight_context;
using aoa::detail::prepare_device_options;
using aoa::detail::refresh_discovery_views;
using aoa::detail::report_id;
using aoa::detail::set_error;
using aoa::detail::timeout_is_set;

namespace {

constexpr std::size_t kMaximumLibusbPortDepth = 7U;

// AOA 1.0 SEND_STRING: at most 256 bytes per string, NUL included.
constexpr std::size_t kMaximumAccessoryStringBytes = 256U;

} // namespace

extern "C" {

aoahid_result AOAHID_CALL aoahid_discover(aoahid_context* context,
                                          const std::uint32_t control_timeout_ms,
                                          aoahid_discovery** out_discovery) try {
    AOAHID_SERIALIZED(context, "aoahid_discover");
    aoa::detail::clear_error();
    if (out_discovery != nullptr)
        *out_discovery = nullptr;
    if (context == nullptr || out_discovery == nullptr) {
        set_error(AOAHID_ERR_PARAM, "discover", "A Context and an output pointer are required.");
        return AOAHID_ERR_PARAM;
    }
    if (!timeout_is_set(control_timeout_ms, "control_timeout_ms"))
        return AOAHID_ERR_UNSET_FIELD;
    const aoahid_result deferred = preflight_context(context);
    if (deferred != AOAHID_OK)
        return deferred;
    auto discovery = std::unique_ptr<aoahid_discovery>(new (std::nothrow) aoahid_discovery());
    if (discovery == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "discovery",
                  "Memory allocation for the discovery result failed.");
        return AOAHID_ERR_INTERNAL;
    }
    std::vector<aoa::transport::Candidate> candidates;
    const aoahid_result result = context->runtime->discover(control_timeout_ms, &candidates);
    if (result != AOAHID_OK) {
        return finish_transport_result(result, "discovery", "USB enumeration failed.", nullptr, 51,
                                       0U, 2U);
    }
    discovery->entries.reserve(candidates.size());
    for (auto& candidate : candidates) {
        aoahid_discovery_entry entry{};
        entry.candidate = std::move(candidate);
        discovery->entries.push_back(std::move(entry));
    }
    refresh_discovery_views(discovery.get());
    *out_discovery = discovery.release();
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("discover")

std::size_t AOAHID_CALL aoahid_discovery_count(const aoahid_discovery* discovery) try {
    aoa::detail::clear_error();
    if (discovery == nullptr) {
        set_error(AOAHID_ERR_PARAM, "discovery",
                  "The discovery handle is null; zero is the failure sentinel.");
        return 0U;
    }
    return discovery->entries.size();
}
AOAHID_C_VALUE_CATCH("discovery.count", 0U)

const aoahid_device_info* AOAHID_CALL aoahid_discovery_get(const aoahid_discovery* discovery,
                                                           const std::size_t index) try {
    aoa::detail::clear_error();
    if (discovery == nullptr || index >= discovery->entries.size()) {
        set_error(AOAHID_ERR_PARAM, discovery == nullptr ? "discovery" : "discovery.index",
                  discovery == nullptr ? "The discovery handle is null."
                                       : "The discovery index is outside the owned result list.",
                  0, 0, 0, 0,
                  static_cast<std::uint32_t>(
                      std::min<std::size_t>(index, std::numeric_limits<std::uint32_t>::max())),
                  static_cast<std::uint32_t>(
                      std::min<std::size_t>(discovery == nullptr ? 0U : discovery->entries.size(),
                                            std::numeric_limits<std::uint32_t>::max())));
        return nullptr;
    }
    return &discovery->entries[index].public_info;
}
AOAHID_C_VALUE_CATCH("discovery.get", nullptr)

void AOAHID_CALL aoahid_discovery_destroy(aoahid_discovery* discovery) try {
    aoa::detail::clear_error();
    if (discovery == nullptr) {
        set_error(AOAHID_ERR_PARAM, "discovery", "The discovery handle is null.");
        return;
    }
    delete discovery;
}
AOAHID_C_VOID_CATCH("discovery.destroy")

aoahid_result AOAHID_CALL aoahid_accessory_start(aoahid_context* context,
                                                 const aoahid_device_info* selected,
                                                 const aoahid_accessory_options* options,
                                                 const std::uint32_t control_timeout_ms) try {
    AOAHID_SERIALIZED(context, "aoahid_accessory_start");
    aoa::detail::clear_error();
    if (!timeout_is_set(control_timeout_ms, "control_timeout_ms"))
        return AOAHID_ERR_UNSET_FIELD;
    if (context == nullptr || selected == nullptr) {
        set_error(AOAHID_ERR_PARAM, "accessory.start", "Context and selection are required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result deferred = preflight_context(context);
    if (deferred != AOAHID_OK)
        return deferred;
    if ((selected->port_path_length != 0U && selected->port_path == nullptr) ||
        selected->port_path_length > kMaximumLibusbPortDepth) {
        set_error(selected->port_path_length > kMaximumLibusbPortDepth ? AOAHID_ERR_OVERFLOW
                                                                       : AOAHID_ERR_PARAM,
                  "device.port_path",
                  "A nonempty libusb physical path needs a pointer and cannot exceed seven ports.");
        return selected->port_path_length > kMaximumLibusbPortDepth ? AOAHID_ERR_OVERFLOW
                                                                    : AOAHID_ERR_PARAM;
    }
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "accessory_options")) {
        return AOAHID_ERR_PARAM;
    }
    const aoahid_aoa_strings& strings = options->strings;
    if (options->reserved != 0U || strings.manufacturer == nullptr ||
        strings.manufacturer[0] == '\0' || strings.model == nullptr || strings.model[0] == '\0') {
        set_error(AOAHID_ERR_UNSET_FIELD, "accessory_options",
                  "A nonempty manufacturer and model are required, and reserved must be zero.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    const std::array<const char*, 6U> values{strings.manufacturer, strings.model,
                                             strings.description,  strings.version,
                                             strings.uri,          strings.serial};
    // AOA 1.0 limits each request-52 string to 256 bytes including its NUL;
    // check all of them before any request is sent.
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (values[index] != nullptr &&
            std::char_traits<char>::length(values[index]) + 1U > kMaximumAccessoryStringBytes) {
            set_error(AOAHID_ERR_OVERFLOW, "accessory_options.strings",
                      "An AOA identification string exceeds 256 bytes including its NUL.", 0, 52,
                      0U, 0U, static_cast<std::uint32_t>(index));
            return AOAHID_ERR_OVERFLOW;
        }
    }
    const aoahid_result result = context->runtime->start_accessory(
        copy_candidate(*selected), values.data(), control_timeout_ms);
    if (result != AOAHID_OK) {
        return finish_transport_result(result, "accessory.start",
                                       "An AOA accessory-mode start request failed.");
    }
    aoa::detail::log_event(context, AOAHID_LOG_INFO, "accessory_start", nullptr, nullptr,
                           aoa::detail::LogEventStatus{AOAHID_OK, 0, 53});
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("accessory.start")

aoahid_result AOAHID_CALL aoahid_device_open(aoahid_context* context,
                                             const aoahid_device_info* selected,
                                             const aoahid_device_options* options,
                                             aoahid_device** out_device) try {
    AOAHID_SERIALIZED(context, "aoahid_device_open");
    aoa::detail::clear_error();
    if (out_device != nullptr)
        *out_device = nullptr;
    if (context == nullptr || selected == nullptr || out_device == nullptr) {
        set_error(AOAHID_ERR_PARAM, "device.open", "Context, selection, and output are required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result deferred = preflight_context(context);
    if (deferred != AOAHID_OK)
        return deferred;
    if ((selected->port_path_length != 0U && selected->port_path == nullptr) ||
        selected->port_path_length > kMaximumLibusbPortDepth) {
        set_error(selected->port_path_length > kMaximumLibusbPortDepth ? AOAHID_ERR_OVERFLOW
                                                                       : AOAHID_ERR_PARAM,
                  "device.port_path",
                  "A nonempty libusb physical path needs a pointer and cannot exceed seven ports.");
        return selected->port_path_length > kMaximumLibusbPortDepth ? AOAHID_ERR_OVERFLOW
                                                                    : AOAHID_ERR_PARAM;
    }
    aoahid_device_options effective_options{};
    const aoahid_result validation = prepare_device_options(options, &effective_options);
    if (validation != AOAHID_OK)
        return validation;
    auto device = std::unique_ptr<aoahid_device>(new (std::nothrow) aoahid_device());
    if (device == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "device", "Memory allocation for the Device failed.");
        return AOAHID_ERR_INTERNAL;
    }
    try {
        device->context = context;
        device->config = copy_config(effective_options, context->options.event_mode);
        device->aoa_descriptor_policy_bytes = effective_options.aoa_descriptor_wire_policy_bytes;
        device->linux_hid = effective_options.linux_hid;
        device->target_ep0_policy_bytes = effective_options.target_ep0_data_policy_bytes;
        device->host_control_policy_bytes = effective_options.host_control_buffer_policy_bytes;
        device->validate_reports = effective_options.validate_reports == 1U;
        const auto candidate = copy_candidate(*selected);
        device->bus = candidate.bus;
        device->port_path = candidate.port_path;
        device->vendor_id = candidate.vendor_id;
        device->product_id = candidate.product_id;
        const aoahid_result result = aoa::transport::Device::open(
            context->runtime, candidate, device->config, &device->transport);
        if (result != AOAHID_OK) {
            return finish_transport_result(result, "device.open",
                                           "The selected USB/AOA device could not be opened.");
        }
        {
            const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
            // Every live Device can move from devices to graveyard without an
            // allocation. This keeps close teardown recoverable even under OOM.
            const std::size_t live_capacity =
                context->devices.size() + context->graveyard.size() + 1U;
            context->devices.reserve(live_capacity);
            context->graveyard.reserve(live_capacity);
            device->hid_id_domain_index = find_or_create_hid_id_domain(context, candidate);
            context->devices.push_back(device.get());
        }
    } catch (...) {
        if (device->transport != nullptr) {
            delete device->transport;
            device->transport = nullptr;
        }
        throw;
    }
    *out_device = device.release();
    aoa::detail::log_event(context, AOAHID_LOG_INFO, "device_open", *out_device, nullptr);
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("device.open")

aoahid_result AOAHID_CALL aoahid_device_close(aoahid_device* device,
                                              const std::uint32_t control_timeout_ms,
                                              const std::uint32_t drain_timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(device), "aoahid_device_close");
    aoa::detail::clear_error();
    if (device != nullptr && (!timeout_is_set(control_timeout_ms, "control_timeout_ms") ||
                              !timeout_is_set(drain_timeout_ms, "drain_timeout_ms"))) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    bool consumed = false;
    return device_close_impl(device, &consumed, control_timeout_ms, drain_timeout_ms);
}
AOAHID_C_RESULT_CATCH("device.close")

aoahid_result AOAHID_CALL aoahid_device_latched_error(aoahid_device* device) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(device), "aoahid_device_latched_error");
    aoa::detail::clear_error();
    if (device == nullptr || device->closing.load(std::memory_order_acquire) ||
        device->transport == nullptr) {
        set_error(AOAHID_ERR_PARAM, "device.latched_error",
                  "A live, open Device handle is required.");
        return AOAHID_ERR_PARAM;
    }
    aoa::transport::ErrorInfo error{};
    const aoahid_result result = device->transport->latched_error(&error);
    if (result == AOAHID_OK) {
        return preflight_context(device->context);
    }
    set_error(result, "device.latched_error",
              error.aoa_request == 57 ? completion_reason(result)
                                      : "The Device retained a prior transport failure.",
              error.native_status, error.aoa_request, error.hid_id, error.report_id, error.offset,
              error.length);
    return result;
}
AOAHID_C_RESULT_CATCH("device.latched_error")

} // extern "C"
