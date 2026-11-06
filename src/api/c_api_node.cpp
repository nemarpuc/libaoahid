// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: opening and closing Nodes.
 */

#include "api/support.hpp"

#include <chrono>
#include <memory>
#include <new>

using aoa::detail::allocate_hid_id;
using aoa::detail::check_reservation;
using aoa::detail::check_spec_fits_device;
using aoa::detail::close_live_node_until;
using aoa::detail::finish_transport_result;
using aoa::detail::initialize_node_state;
using aoa::detail::preflight_context;
using aoa::detail::set_error;
using aoa::detail::timeout_is_set;

extern "C" {

aoahid_result AOAHID_CALL aoahid_node_open(aoahid_device* device, aoahid_spec* spec,
                                           const aoahid_node_options* options,
                                           const std::uint32_t control_timeout_ms,
                                           aoahid_node** out_node) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(device), "aoahid_node_open");
    aoa::detail::clear_error();
    if (out_node == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_node", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_node = nullptr;
    if (!timeout_is_set(control_timeout_ms, "control_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (device == nullptr || device->transport == nullptr || spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.open", "A live Device and immutable Spec are required.");
        return AOAHID_ERR_PARAM;
    }
    if (device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "device",
                  "The Device handle was consumed by close and cannot open a Node.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result deferred = preflight_context(device->context);
    if (deferred != AOAHID_OK)
        return deferred;
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)), "node_options")) {
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result reservation = check_reservation(device, *options);
    if (reservation != AOAHID_OK)
        return reservation;
    const aoahid_result fits = check_spec_fits_device(device, spec);
    if (fits != AOAHID_OK)
        return fits;
    auto node = std::unique_ptr<aoahid_node>(new (std::nothrow) aoahid_node());
    if (node == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "node", "Memory allocation for the Node failed.");
        return AOAHID_ERR_INTERNAL;
    }
    bool retained = false;
    bool reservation_installed = false;
    try {
        node->device = device;
        node->spec = spec;
        node->active_completion_mutex =
            device->context->options.event_mode == AOAHID_EVENT_INTERNAL_THREAD
                ? &node->completion_mutex
                : nullptr;
        const aoahid_result allocated = allocate_hid_id(device, &node->hid_id);
        if (allocated != AOAHID_OK) {
            return allocated;
        }
        node->reserved_slots = options->reserved_slots;
        const aoahid_result initialized = initialize_node_state(node.get());
        if (initialized != AOAHID_OK) {
            return initialized;
        }
        // Complete every potentially allocating host-side operation before
        // request 54. After Android accepts a registration, publishing the
        // Node into this pre-reserved vector cannot allocate or orphan it.
        device->nodes.reserve(device->nodes.size() + 1U);
        if (!aoa::detail::retain_spec(spec)) {
            set_error(AOAHID_ERR_OVERFLOW, "spec.references",
                      "The Spec reference count cannot be incremented for this Node.");
            return AOAHID_ERR_OVERFLOW;
        }
        retained = true;
        const aoahid_result reserved_slots = device->transport->set_reservation(
            node->hid_id, node->reserved_slots, &node->reservation);
        if (reserved_slots != AOAHID_OK) {
            static_cast<void>(finish_transport_result(
                reserved_slots, "node.reserved_slots",
                "The requested transfer-pool reservation could not be installed.", node.get()));
            static_cast<void>(aoa::detail::release_spec(spec));
            retained = false;
            return reserved_slots;
        }
        reservation_installed = true;
    } catch (...) {
        if (reservation_installed) {
            static_cast<void>(device->transport->clear_reservation(node->reservation));
        }
        if (retained) {
            static_cast<void>(aoa::detail::release_spec(spec));
        }
        throw;
    }

    const aoahid_result registered = device->transport->register_hid(
        node->hid_id, spec->descriptor.data(), spec->descriptor.size(), control_timeout_ms);
    if (registered != AOAHID_OK) {
        aoa::detail::log_event(
            device->context, AOAHID_LOG_ERROR, "node_register", device, node.get(),
            aoa::detail::LogEventStatus{registered, 0, 54, 0U,
                                        static_cast<std::uint32_t>(spec->descriptor.size())});
        static_cast<void>(finish_transport_result(
            registered, "node.register", "AOA HID registration or descriptor transfer failed.",
            node.get(), 54, 0U, static_cast<std::uint32_t>(spec->descriptor.size())));
        static_cast<void>(device->transport->clear_reservation(node->reservation));
        static_cast<void>(aoa::detail::release_spec(spec));
        return registered;
    }

    // Capacity was established before request 54 and the element is a raw
    // pointer, so this publication performs neither allocation nor user code.
    device->nodes.push_back(node.get());
    *out_node = node.release();
    aoa::detail::log_event(
        device->context, AOAHID_LOG_INFO, "node_register", device, *out_node,
        aoa::detail::LogEventStatus{AOAHID_OK, 0, 54, 0U,
                                    static_cast<std::uint32_t>(spec->descriptor.size())});
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("node.open")

std::uint16_t AOAHID_CALL aoahid_node_id(const aoahid_node* node) try {
    aoa::detail::clear_error();
    if (node == nullptr || node->closed) {
        set_error(AOAHID_ERR_PARAM, "node",
                  "The Node handle is null or closed; zero is the failure sentinel.");
        return 0U;
    }
    return node->hid_id;
}
AOAHID_C_VALUE_CATCH("node.id", 0U)

aoahid_result AOAHID_CALL aoahid_node_manifest(const aoahid_node* node,
                                               aoahid_capability_manifest* manifest) try {
    aoa::detail::clear_error();
    if (node == nullptr || node->closed || node->spec == nullptr) {
        set_error(AOAHID_ERR_PARAM, "node.manifest", "A live node is required.");
        return AOAHID_ERR_PARAM;
    }
    return aoahid_spec_manifest(node->spec, manifest);
}
AOAHID_C_RESULT_CATCH("node.manifest")

aoahid_result AOAHID_CALL aoahid_node_close(aoahid_node* node,
                                            const std::uint32_t control_timeout_ms,
                                            const std::uint32_t drain_timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(node), "aoahid_node_close");
    aoa::detail::clear_error();
    if (!timeout_is_set(control_timeout_ms, "control_timeout_ms") ||
        !timeout_is_set(drain_timeout_ms, "drain_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (node == nullptr || node->closed || node->device == nullptr ||
        node->device->transport == nullptr ||
        node->device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "node.close",
                  "The Node handle is null, closed, detached, or owned by a closing Device.");
        return AOAHID_ERR_PARAM;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(drain_timeout_ms);
    return close_live_node_until(node, deadline, control_timeout_ms);
}
AOAHID_C_RESULT_CATCH("node.close")

} // extern "C"
