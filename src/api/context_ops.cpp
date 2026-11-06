// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Context-side internals shared by the C ABI entry points: deferred errors, option checks,
 * the graveyard of closed devices and HID ID allocation.
 */

#include "api/support.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

using aoa::detail::allocate_hid_id;
using aoa::detail::check_reservation;
using aoa::detail::check_spec_fits_device;
using aoa::detail::copy_candidate;
using aoa::detail::copy_config;
using aoa::detail::current_deferred_error;
using aoa::detail::DeferredErrorInput;
using aoa::detail::destroy_drained_device;
using aoa::detail::find_or_create_hid_id_domain;
using aoa::detail::finish_transport_result;
using aoa::detail::graph_drained;
using aoa::detail::graveyard_empty;
using aoa::detail::last_active_device;
using aoa::detail::latch_context_error;
using aoa::detail::latch_terminal_event_error;
using aoa::detail::make_deferred_error;
using aoa::detail::park_in_graveyard;
using aoa::detail::peek_context_error;
using aoa::detail::peek_terminal_event_error;
using aoa::detail::preflight_context;
using aoa::detail::prepare_device_options;
using aoa::detail::publish_deferred_error;
using aoa::detail::reap_graveyard;
using aoa::detail::refresh_discovery_views;
using aoa::detail::report_id;
using aoa::detail::request_event_thread_stop;
using aoa::detail::set_error;
using aoa::detail::take_context_error;
using aoa::detail::timeout_is_set;
using aoa::detail::validate_context_options;

namespace {

// The tuning values have no default: each is the caller's, and zero means none.
bool required_tuning_nonzero(const aoahid_device_options& options) noexcept {
    return options.descriptor_fragment_bytes != 0U && options.transfer_pool_slots != 0U &&
           options.maximum_report_bytes != 0U;
}

bool required_product_policies_nonzero(const aoahid_device_options& options) noexcept {
    return options.aoa_descriptor_wire_policy_bytes != 0U &&
           options.linux_hid.descriptor_bytes != 0U && options.linux_hid.fields_per_report != 0U &&
           options.linux_hid.global_stack_depth != 0U && options.linux_hid.usages != 0U &&
           options.linux_hid.report_data_bits != 0U && options.linux_hid.report_size_bits != 0U &&
           options.target_ep0_data_policy_bytes != 0U &&
           options.host_control_buffer_policy_bytes != 0U;
}

// A graveyard entry is parked only by an explicit close, so the fast path in
// reap_graveyard must observe the same emptiness the mutex-held view reports.
static_assert(sizeof(std::atomic<std::size_t>) >= sizeof(std::size_t),
              "graveyard_size must hold a container size");

std::uint32_t saturate_u32(const std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(value, std::numeric_limits<std::uint32_t>::max()));
}

} // namespace

namespace aoa::detail {

aoahid_result finish_transport_result(const aoahid_result result, const char* field,
                                      const char* reason, const aoahid_node* node,
                                      const std::int32_t fallback_request,
                                      const std::uint32_t fallback_offset,
                                      const std::uint32_t fallback_length) noexcept {
    if (result == AOAHID_OK) {
        aoa::detail::clear_error();
        return AOAHID_OK;
    }
    aoa::transport::ErrorInfo transport_error = aoa::transport::last_error();
    if (transport_error.result != result) {
        transport_error = aoa::transport::ErrorInfo{
            result,           0,
            fallback_request, node == nullptr ? std::uint16_t{0} : node->hid_id,
            fallback_offset,  fallback_length,
            report_id(node)};
    }
    const std::uint16_t hid_id = transport_error.hid_id != 0U
                                     ? transport_error.hid_id
                                     : (node == nullptr ? std::uint16_t{0} : node->hid_id);
    const std::uint16_t transport_report_id =
        transport_error.aoa_request == 57 ? transport_error.report_id : report_id(node);
    set_error(result, field, reason, transport_error.native_status,
              transport_error.aoa_request != 0 ? transport_error.aoa_request : fallback_request,
              hid_id, transport_report_id,
              transport_error.offset != 0U ? transport_error.offset : fallback_offset,
              transport_error.length != 0U ? transport_error.length : fallback_length);
    return result;
}

aoa::detail::DeferredError make_deferred_error(DeferredErrorInput input) noexcept {
    aoa::detail::DeferredError error{};
    error.transport = input.transport;
    error.field = input.field;
    error.reason = input.reason;
    error.report_id = input.report_id;
    error.pending = input.transport.result != AOAHID_OK;
    return error;
}

aoa::detail::DeferredError current_deferred_error(const aoahid_result result, const char* field,
                                                  const char* reason, const aoahid_node* node,
                                                  const std::int32_t fallback_request,
                                                  const std::uint32_t fallback_offset,
                                                  const std::uint32_t fallback_length) noexcept {
    const aoahid_error_detail* detail = aoahid_last_error();
    if (detail != nullptr && detail->code == result) {
        return make_deferred_error(DeferredErrorInput{
            aoa::transport::ErrorInfo{result, detail->transport_status, detail->aoa_request,
                                      detail->node_id, detail->offset, detail->length,
                                      detail->report_id},
            detail->field == nullptr ? field : detail->field,
            detail->reason == nullptr ? reason : detail->reason, detail->report_id});
    }

    aoa::transport::ErrorInfo transport_error = aoa::transport::last_error();
    if (transport_error.result != result) {
        transport_error = aoa::transport::ErrorInfo{
            result,           0,
            fallback_request, node == nullptr ? std::uint16_t{0U} : node->hid_id,
            fallback_offset,  fallback_length,
            report_id(node)};
    }
    return make_deferred_error(DeferredErrorInput{
        aoa::transport::ErrorInfo{
            result, transport_error.native_status,
            transport_error.aoa_request != 0 ? transport_error.aoa_request : fallback_request,
            transport_error.hid_id != 0U ? transport_error.hid_id
                                         : (node == nullptr ? std::uint16_t{0U} : node->hid_id),
            transport_error.offset != 0U ? transport_error.offset : fallback_offset,
            transport_error.length != 0U ? transport_error.length : fallback_length,
            transport_error.aoa_request == 57 ? transport_error.report_id : report_id(node)},
        field, reason,
        transport_error.aoa_request == 57 ? transport_error.report_id : report_id(node)});
}

aoahid_result publish_deferred_error(const aoa::detail::DeferredError& error) noexcept {
    if (!error.pending || error.transport.result == AOAHID_OK) {
        aoa::detail::clear_error();
        return AOAHID_OK;
    }
    set_error(error.transport.result, error.field == nullptr ? "context" : error.field,
              error.reason == nullptr ? "A deferred Context operation failed." : error.reason,
              error.transport.native_status, error.transport.aoa_request, error.transport.hid_id,
              error.report_id, error.transport.offset, error.transport.length);
    return error.transport.result;
}

void latch_context_error(aoahid_context* context,
                         const aoa::detail::DeferredError& error) noexcept {
    if (context == nullptr || !error.pending || error.transport.result == AOAHID_OK) {
        return;
    }
    {
        const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
        if (!context->deferred_error.pending) {
            context->deferred_error = error;
        }
    }
    context->graveyard_cv.notify_all();
}

bool peek_context_error(aoahid_context* context, aoa::detail::DeferredError* output) noexcept {
    if (context == nullptr || output == nullptr)
        return false;
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    if (!context->deferred_error.pending)
        return false;
    *output = context->deferred_error;
    return true;
}

bool take_context_error(aoahid_context* context, aoa::detail::DeferredError* output) noexcept {
    if (context == nullptr || output == nullptr)
        return false;
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    if (!context->deferred_error.pending)
        return false;
    *output = context->deferred_error;
    context->deferred_error = aoa::detail::DeferredError{};
    return true;
}

void latch_terminal_event_error(aoahid_context* context,
                                const aoa::detail::DeferredError& error) noexcept {
    if (context == nullptr || !error.pending || error.transport.result == AOAHID_OK)
        return;
    {
        const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
        context->event_thread_error = error;
        if (!context->deferred_error.pending)
            context->deferred_error = error;
    }
    context->event_thread_failed.store(true, std::memory_order_release);
    context->graveyard_cv.notify_all();
}

void request_event_thread_stop(aoahid_context* context) noexcept {
    if (context == nullptr)
        return;
    context->stop_event_thread.store(true, std::memory_order_release);
    if (context->options.event_mode == AOAHID_EVENT_INTERNAL_THREAD &&
        context->runtime != nullptr &&
        !context->event_thread_exited.load(std::memory_order_acquire)) {
        context->runtime->interrupt_event_handler();
    }
    context->graveyard_cv.notify_all();
}

bool peek_terminal_event_error(aoahid_context* context,
                               aoa::detail::DeferredError* output) noexcept {
    if (context == nullptr || output == nullptr ||
        !context->event_thread_failed.load(std::memory_order_acquire)) {
        return false;
    }
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    if (!context->event_thread_error.pending)
        return false;
    *output = context->event_thread_error;
    return true;
}

aoahid_result preflight_context(aoahid_context* context) noexcept {
    aoa::detail::DeferredError error{};
    if (peek_terminal_event_error(context, &error))
        return publish_deferred_error(error);
    return take_context_error(context, &error) ? publish_deferred_error(error) : AOAHID_OK;
}

aoahid_result validate_context_options(const aoahid_context_options* options) noexcept {
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "context_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U ||
        (options->event_mode != AOAHID_EVENT_CALLER_POLL &&
         options->event_mode != AOAHID_EVENT_INTERNAL_THREAD) ||
        options->log_level < AOAHID_LOG_DISABLED || options->log_level > AOAHID_LOG_TRACE ||
        (options->log_level != AOAHID_LOG_DISABLED && options->log_sink == nullptr)) {
        set_error(AOAHID_ERR_UNSET_FIELD, "context_options",
                  "event_mode, log_level, and any enabled log sink must be explicitly valid; "
                  "reserved must be zero.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    return AOAHID_OK;
}

aoahid_result prepare_device_options(const aoahid_device_options* options,
                                     aoahid_device_options* effective) noexcept {
    if (options == nullptr || !aoa::detail::valid_struct(
                                  options, options->struct_size,
                                  static_cast<std::uint32_t>(sizeof(*options)), "device_options")) {
        return AOAHID_ERR_PARAM;
    }
    *effective = *options;
    if (effective->reserved != 0U || !required_tuning_nonzero(*effective) ||
        !required_product_policies_nonzero(*effective) ||
        !aoa::detail::valid_boolean(effective->validate_reports) ||
        (effective->interface_claim_policy != AOAHID_INTERFACE_CLAIM_NONE &&
         effective->interface_claim_policy != AOAHID_INTERFACE_CLAIM_EXPLICIT) ||
        (effective->interface_claim_policy == AOAHID_INTERFACE_CLAIM_NONE &&
         effective->interface_number != -1) ||
        (effective->interface_claim_policy == AOAHID_INTERFACE_CLAIM_EXPLICIT &&
         (effective->interface_number < 0 || effective->interface_number > 255))) {
        set_error(AOAHID_ERR_UNSET_FIELD, "device_options",
                  "Every tuning value and product policy must be nonzero, and every flag and "
                  "claim choice valid.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (effective->descriptor_fragment_bytes > std::numeric_limits<std::uint16_t>::max() ||
        effective->maximum_report_bytes > std::numeric_limits<std::uint16_t>::max() ||
        effective->aoa_descriptor_wire_policy_bytes > std::numeric_limits<std::uint16_t>::max() ||
        effective->descriptor_fragment_bytes > effective->target_ep0_data_policy_bytes ||
        effective->descriptor_fragment_bytes > effective->host_control_buffer_policy_bytes ||
        effective->maximum_report_bytes > effective->target_ep0_data_policy_bytes ||
        effective->maximum_report_bytes > effective->host_control_buffer_policy_bytes) {
        set_error(AOAHID_ERR_OVERFLOW, "device.size_policies",
                  "A caller policy is inconsistent with the AOA 16-bit wire fields or another "
                  "explicitly selected policy.");
        return AOAHID_ERR_OVERFLOW;
    }
    return AOAHID_OK;
}

aoa::transport::Candidate copy_candidate(const aoahid_device_info& info) {
    aoa::transport::Candidate candidate{};
    candidate.bus = info.bus_number;
    candidate.address = info.device_address;
    if (info.port_path != nullptr && info.port_path_length != 0U) {
        candidate.port_path.assign(info.port_path, info.port_path + info.port_path_length);
    }
    candidate.vendor_id = info.vendor_id;
    candidate.product_id = info.product_id;
    candidate.protocol_version = info.protocol_version;
    if (info.serial != nullptr)
        candidate.serial = info.serial;
    if (info.product != nullptr)
        candidate.product = info.product;
    return candidate;
}

aoa::transport::DeviceConfig copy_config(const aoahid_device_options& options,
                                         const aoahid_event_mode event_mode) noexcept {
    return aoa::transport::DeviceConfig{event_mode,
                                        options.descriptor_fragment_bytes,
                                        options.transfer_pool_slots,
                                        options.maximum_report_bytes,
                                        options.interface_claim_policy,
                                        options.interface_number};
}

// Every wait is the caller's, per call, and has no default: zero is an error.
bool timeout_is_set(const std::uint32_t value, const char* field) noexcept {
    if (value != 0U) {
        return true;
    }
    set_error(AOAHID_ERR_UNSET_FIELD, field, "This timeout must be set; it has no default.");
    return false;
}

void refresh_discovery_views(aoahid_discovery* discovery) noexcept {
    for (auto& entry : discovery->entries) {
        entry.public_info = aoahid_device_info{entry.candidate.bus,
                                               entry.candidate.address,
                                               entry.candidate.port_path.data(),
                                               entry.candidate.port_path.size(),
                                               entry.candidate.vendor_id,
                                               entry.candidate.product_id,
                                               entry.candidate.protocol_version,
                                               entry.candidate.serial.c_str(),
                                               entry.candidate.product.c_str()};
    }
}

void park_in_graveyard(aoahid_context* context, aoahid_device* device) noexcept {
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    context->graveyard.push_back(device);
    context->graveyard_size.store(context->graveyard.size(), std::memory_order_release);
}

void reap_graveyard(aoahid_context* context) noexcept {
    if (context == nullptr || context->graveyard_size.load(std::memory_order_acquire) == 0U) {
        return;
    }
    for (;;) {
        aoahid_device* ready = nullptr;
        {
            const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
            const auto found =
                std::find_if(context->graveyard.begin(), context->graveyard.end(),
                             [](const aoahid_device* device) { return graph_drained(device); });
            if (found == context->graveyard.end()) {
                return;
            }
            ready = *found;
            context->graveyard.erase(found);
            context->graveyard_size.store(context->graveyard.size(), std::memory_order_release);
        }
        const aoa::detail::DeferredError outcome = destroy_drained_device(ready);
        latch_context_error(context, outcome);
        context->graveyard_cv.notify_all();
    }
}

bool graveyard_empty(aoahid_context* context) noexcept {
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    return context->graveyard.empty();
}

aoahid_device* last_active_device(aoahid_context* context) noexcept {
    const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
    return context->devices.empty() ? nullptr : context->devices.back();
}

std::size_t find_or_create_hid_id_domain(aoahid_context* context,
                                         const aoa::transport::Candidate& candidate) {
    const bool unidentified = candidate.port_path.empty();
    const auto found = std::find_if(context->hid_id_domains.begin(), context->hid_id_domains.end(),
                                    [&](const aoa::detail::HidIdDomain& domain) {
                                        if (unidentified) {
                                            return domain.unidentified;
                                        }
                                        return !domain.unidentified &&
                                               domain.bus == candidate.bus &&
                                               domain.port_path == candidate.port_path;
                                    });
    if (found != context->hid_id_domains.end()) {
        return static_cast<std::size_t>(found - context->hid_id_domains.begin());
    }
    aoa::detail::HidIdDomain domain{};
    domain.bus = candidate.bus;
    domain.port_path = candidate.port_path;
    domain.unidentified = unidentified;
    context->hid_id_domains.push_back(std::move(domain));
    return context->hid_id_domains.size() - 1U;
}

aoahid_result allocate_hid_id(aoahid_device* device, std::uint16_t* hid_id) noexcept {
    if (device == nullptr || device->context == nullptr || hid_id == nullptr) {
        set_error(AOAHID_ERR_PARAM, "device.hid_id",
                  "A live device and HID ID output pointer are required.");
        return AOAHID_ERR_PARAM;
    }
    const aoa::detail::ModeMutexGuard guard(device->context->active_state_mutex);
    if (device->hid_id_domain_index >= device->context->hid_id_domains.size()) {
        set_error(AOAHID_ERR_INTERNAL, "device.hid_id",
                  "The device has no valid physical-identity allocation domain.");
        return AOAHID_ERR_INTERNAL;
    }
    auto& domain = device->context->hid_id_domains[device->hid_id_domain_index];
    if (domain.next_hid_id > std::numeric_limits<std::uint16_t>::max()) {
        set_error(AOAHID_ERR_OVERFLOW, "device.hid_id",
                  "The monotonic AOA HID ID space for this physical-identity domain is exhausted.");
        return AOAHID_ERR_OVERFLOW;
    }
    *hid_id = static_cast<std::uint16_t>(domain.next_hid_id++);
    return AOAHID_OK;
}

aoahid_result check_reservation(const aoahid_device* device,
                                const aoahid_node_options& options) noexcept {
    const bool no_reservation = options.has_reserved_slots == 0U && options.reserved_slots == 0U;
    if (options.reserved != 0U || (!no_reservation && options.has_reserved_slots != 1U) ||
        options.reserved_slots > 1U) {
        set_error(AOAHID_ERR_UNSET_FIELD, "node.reserved_slots",
                  "Reserve nothing with 0/0, or one slot with has_reserved_slots 1 and "
                  "reserved_slots 0 or 1.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    std::uint64_t reserved = options.reserved_slots;
    for (const auto* sibling : device->nodes)
        reserved += sibling->reserved_slots;
    if (reserved >= device->config.pool_slots) {
        set_error(AOAHID_ERR_PARAM, "node.reserved_slots",
                  "The reservations of all Nodes must leave at least one shared pool slot.");
        return AOAHID_ERR_PARAM;
    }
    return AOAHID_OK;
}

// Every limit is checked before request 54, so a Spec the target would refuse never registers.
// A refused limit reports the Device's value in offset and the Spec's in length.
aoahid_result check_spec_fits_device(const aoahid_device* device,
                                     const aoahid_spec* spec) noexcept {
    if (spec->descriptor.empty() || spec->descriptor.size() > device->aoa_descriptor_policy_bytes) {
        set_error(AOAHID_ERR_OVERFLOW, "spec.descriptor.aoa_wire_policy",
                  "The descriptor exceeds aoa_descriptor_wire_policy_bytes.");
        return AOAHID_ERR_OVERFLOW;
    }
    struct Limit {
        const char* field;
        const char* reason;
        std::uint64_t allowed;
        std::uint64_t needed;
    };
    const aoahid_linux_hid_policy& linux_hid = device->linux_hid;
    const aoa::hid::DescriptorRequirements& needs = spec->requirements;
    const std::array<Limit, 6> limits{{
        {"spec.descriptor.linux_policy", "The descriptor exceeds linux_hid.descriptor_bytes.",
         linux_hid.descriptor_bytes, spec->descriptor.size()},
        {"spec.requirements.maximum_fields_per_report",
         "A report has more fields than linux_hid.fields_per_report.", linux_hid.fields_per_report,
         needs.maximum_fields_per_report},
        {"spec.requirements.maximum_global_stack_depth",
         "The descriptor pushes deeper than linux_hid.global_stack_depth.",
         linux_hid.global_stack_depth, needs.maximum_global_stack_depth},
        {"spec.requirements.maximum_report_size_bits",
         "A Report Size is larger than linux_hid.report_size_bits.", linux_hid.report_size_bits,
         needs.maximum_report_size_bits},
        {"spec.requirements.maximum_usages",
         "The descriptor has more Usages than linux_hid.usages.", linux_hid.usages,
         needs.maximum_usages},
        {"spec.requirements.maximum_report_data_bits",
         "A report is larger than linux_hid.report_data_bits.", linux_hid.report_data_bits,
         needs.maximum_report_data_bits},
    }};
    for (const Limit& limit : limits) {
        if (limit.needed > limit.allowed) {
            set_error(AOAHID_ERR_OVERFLOW, limit.field, limit.reason, 0, 0, 0, 0,
                      saturate_u32(limit.allowed), saturate_u32(limit.needed));
            return AOAHID_ERR_OVERFLOW;
        }
    }
    for (const auto& report : spec->report_capabilities) {
        if (report.wire_length > device->config.maximum_report_bytes ||
            report.wire_length > device->target_ep0_policy_bytes ||
            report.wire_length > device->host_control_policy_bytes) {
            set_error(AOAHID_ERR_OVERFLOW, "spec.report",
                      "A report is larger than maximum_report_bytes, "
                      "target_ep0_data_policy_bytes or host_control_buffer_policy_bytes.");
            return AOAHID_ERR_OVERFLOW;
        }
    }
    return AOAHID_OK;
}

} // namespace aoa::detail

#if defined(AOAHID_CHECK_SERIALIZATION)
namespace aoa::detail {
void serialization_violation(const char* api) noexcept {
    // Not through the log sink: it belongs to the caller and may itself be mid-call.
    std::fprintf(stderr,
                 "libaoahid: %s was called while another thread is inside a call on the same "
                 "Context; the caller must serialize them.\n",
                 api);
    std::fflush(stderr);
    std::abort();
}
} // namespace aoa::detail
#endif
