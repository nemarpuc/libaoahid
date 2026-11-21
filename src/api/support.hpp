// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/*
 * Helpers shared by the translation units of the C ABI, and the macros that keep exceptions
 * from crossing it.
 */

#include "api/internal.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <new>

#define AOAHID_C_RESULT_CATCH(api_name)                                                            \
    catch (const std::bad_alloc&) {                                                                \
        ::aoa::detail::set_error(AOAHID_ERR_INTERNAL, api_name,                                    \
                                 "Memory allocation failed inside the public C ABI call.");        \
        return AOAHID_ERR_INTERNAL;                                                                \
    }                                                                                              \
    catch (...) {                                                                                  \
        ::aoa::detail::set_error(                                                                  \
            AOAHID_ERR_INTERNAL, api_name,                                                         \
            "An internal C++ operation failed inside the public C ABI call.");                     \
        return AOAHID_ERR_INTERNAL;                                                                \
    }

#define AOAHID_C_VALUE_CATCH(api_name, fallback)                                                   \
    catch (const std::bad_alloc&) {                                                                \
        ::aoa::detail::set_error(AOAHID_ERR_INTERNAL, api_name,                                    \
                                 "Memory allocation failed inside the public C ABI call.");        \
        return fallback;                                                                           \
    }                                                                                              \
    catch (...) {                                                                                  \
        ::aoa::detail::set_error(                                                                  \
            AOAHID_ERR_INTERNAL, api_name,                                                         \
            "An internal C++ operation failed inside the public C ABI call.");                     \
        return fallback;                                                                           \
    }

#define AOAHID_C_VOID_CATCH(api_name)                                                              \
    catch (const std::bad_alloc&) {                                                                \
        ::aoa::detail::set_error(AOAHID_ERR_INTERNAL, api_name,                                    \
                                 "Memory allocation failed inside the public C ABI call.");        \
    }                                                                                              \
    catch (...) {                                                                                  \
        ::aoa::detail::set_error(                                                                  \
            AOAHID_ERR_INTERNAL, api_name,                                                         \
            "An internal C++ operation failed inside the public C ABI call.");                     \
    }

namespace aoa::detail {

// ---- context_ops.cpp: deferred errors, option checks, the graveyard and HID IDs ----

aoahid_result finish_transport_result(aoahid_result result, const char* field, const char* reason,
                                      const aoahid_node* node = nullptr,
                                      std::int32_t fallback_request = 0,
                                      std::uint32_t fallback_offset = 0U,
                                      std::uint32_t fallback_length = 0U) noexcept;

struct DeferredErrorInput {
    aoa::transport::ErrorInfo transport{};
    const char* field{};
    const char* reason{};
    std::uint16_t report_id{};
    std::uint16_t node_id{};
};

DeferredError make_deferred_error(DeferredErrorInput input) noexcept;

DeferredError current_deferred_error(aoahid_result result, const char* field, const char* reason,
                                     const aoahid_node* node = nullptr,
                                     std::int32_t fallback_request = 0,
                                     std::uint32_t fallback_offset = 0U,
                                     std::uint32_t fallback_length = 0U) noexcept;

aoahid_result publish_deferred_error(const DeferredError& error) noexcept;

void latch_context_error(aoahid_context* context, const DeferredError& error) noexcept;

bool peek_context_error(aoahid_context* context, DeferredError* output) noexcept;

bool take_context_error(aoahid_context* context, DeferredError* output) noexcept;

void latch_terminal_event_error(aoahid_context* context, const DeferredError& error) noexcept;

void request_event_thread_stop(aoahid_context* context) noexcept;

bool peek_terminal_event_error(aoahid_context* context, DeferredError* output) noexcept;

aoahid_result preflight_context(aoahid_context* context) noexcept;

aoahid_result validate_context_options(const aoahid_context_options* options) noexcept;

aoahid_result prepare_device_options(const aoahid_device_options* options,
                                     aoahid_device_options* effective) noexcept;

aoa::transport::Candidate copy_candidate(const aoahid_device_info& info);

aoa::transport::DeviceConfig copy_config(const aoahid_device_options& options,
                                         aoahid_event_mode event_mode) noexcept;

bool timeout_is_set(std::uint32_t value, const char* field) noexcept;

// A non-null, correctly sized aoahid_commit_timeouts with both waits set.
aoahid_result check_commit_timeouts(const aoahid_commit_timeouts* timeouts) noexcept;

void refresh_discovery_views(aoahid_discovery* discovery) noexcept;

void park_in_graveyard(aoahid_context* context, aoahid_device* device) noexcept;

void reap_graveyard(aoahid_context* context) noexcept;

bool graveyard_empty(aoahid_context* context) noexcept;

aoahid_device* last_active_device(aoahid_context* context) noexcept;

std::size_t find_or_create_hid_id_domain(aoahid_context* context,
                                         const aoa::transport::Candidate& candidate);

aoahid_result allocate_hid_id(aoahid_device* device, std::uint16_t* hid_id) noexcept;

aoahid_result check_reservation(const aoahid_device* device,
                                const aoahid_node_placement& placement) noexcept;

// The limits of the Device that a descriptor of this size and these requirements must meet. A
// joined descriptor is checked with it as a single Spec's is.
aoahid_result check_descriptor_fits_device(const aoahid_device* device, std::size_t descriptor_bytes,
                                           const aoa::hid::DescriptorRequirements& needs) noexcept;

aoahid_result check_spec_fits_device(const aoahid_device* device, const aoahid_spec* spec) noexcept;

// ---- node_ops.cpp: report state, completion and the drain and teardown sequence ----

std::uint16_t report_id(const aoahid_node* node) noexcept;

const char* completion_reason(aoahid_result result) noexcept;

aoahid_result consume_completion(aoahid_node* node, const char* field) noexcept;

aoahid_result initialize_node_state(aoahid_node* node);

// ---- profiles/neutral.cpp ----

// Puts the Node's state at rest and marks it dirty when a neutral report must still be sent.
void make_neutral(aoahid_node* node) noexcept;

bool has_non_neutral_state(const aoahid_node* node) noexcept;

aoahid_result poll_until_idle(aoahid_node* node, std::uint32_t timeout_ms,
                              bool drop_battery = false);

std::uint32_t remaining_milliseconds(std::chrono::steady_clock::time_point deadline) noexcept;

// The part of closing that leaves the Node's registration alone: finishes a started touch frame and
// sends the neutral report. AOAHID_CLOSE_PENDING while a report of the Node is still in flight at
// the deadline (the error detail says so); otherwise the first error of a step, which does not stop
// the next. Device teardown uses it for each Node and unregisters each registration once.
aoahid_result drain_node_for_close(aoahid_node* node,
                                   std::chrono::steady_clock::time_point deadline) noexcept;

aoahid_result close_live_node_until(aoahid_node* node,
                                    std::chrono::steady_clock::time_point deadline,
                                    const aoahid_commit_timeouts& timeouts);

// ---- registration_ops.cpp: making the phone's HID devices match the open Nodes ----

Registration* find_registration(aoahid_device* device, std::uint32_t number) noexcept;

// Records that the registration's members differ from what the phone has. May allocate.
void mark_stale(aoahid_device* device, Registration* registration);

// The checks of a registration that need no I/O: its Specs may share a HID ID, and the joined
// descriptor fits the Device's policies. Sets the error detail on a refusal.
aoahid_result check_registration(const aoahid_device* device,
                                 const Registration* registration);

// Makes the phone's HID ID match the registration's members (the steps are in the file's header).
// A refusal or a failed step leaves it stale; the error detail names the step.
aoahid_result apply_registration(aoahid_device* device, Registration* registration,
                                 const aoahid_commit_timeouts& timeouts);

// Applies every stale registration in the order of its first change; the first error is returned
// and a failed registration does not stop the next one.
aoahid_result commit_device(aoahid_device* device, const aoahid_commit_timeouts& timeouts);

// Drops registrations with no member and nothing on the phone.
void forget_empty_registrations(aoahid_device* device) noexcept;

// ---- device_ops.cpp: Device teardown, Node storage and the first-error latch of a close ----

void remember_close_error(aoahid_device* device, const DeferredError& error) noexcept;

void remember_current_close_error(aoahid_device* device, aoahid_result result, const char* field,
                                  const char* reason, const aoahid_node* node = nullptr,
                                  std::int32_t fallback_request = 0,
                                  std::uint32_t fallback_offset = 0U,
                                  std::uint32_t fallback_length = 0U) noexcept;

void release_node_storage(aoahid_node* node) noexcept;

void remember_terminal_completion(aoahid_device* device, aoahid_node* node) noexcept;

bool graph_drained(const aoahid_device* device) noexcept;

bool wait_for_report_drain(aoahid_device* device, std::chrono::steady_clock::time_point deadline,
                           aoahid_result* poll_error) noexcept;

void release_channel_storage(aoahid_channel* channel) noexcept;

DeferredError destroy_drained_device(aoahid_device* device) noexcept;

aoahid_result device_close_impl(aoahid_device* device, bool* consumed,
                                // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                std::uint32_t control_timeout_ms, std::uint32_t drain_timeout_ms);

} // namespace aoa::detail
