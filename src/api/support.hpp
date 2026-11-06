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

void refresh_discovery_views(aoahid_discovery* discovery) noexcept;

void park_in_graveyard(aoahid_context* context, aoahid_device* device) noexcept;

void reap_graveyard(aoahid_context* context) noexcept;

bool graveyard_empty(aoahid_context* context) noexcept;

aoahid_device* last_active_device(aoahid_context* context) noexcept;

std::size_t find_or_create_hid_id_domain(aoahid_context* context,
                                         const aoa::transport::Candidate& candidate);

aoahid_result allocate_hid_id(aoahid_device* device, std::uint16_t* hid_id) noexcept;

aoahid_result check_reservation(const aoahid_device* device,
                                const aoahid_node_options& options) noexcept;

aoahid_result check_spec_fits_device(const aoahid_device* device, const aoahid_spec* spec) noexcept;

// ---- node_ops.cpp: report state, completion and the drain and teardown sequence ----

std::uint16_t report_id(const aoahid_node* node) noexcept;

const char* completion_reason(aoahid_result result) noexcept;

aoahid_result consume_completion(aoahid_node* node, const char* field) noexcept;

aoahid_result initialize_node_state(aoahid_node* node);

bool has_non_neutral_state(const aoahid_node* node) noexcept;

aoahid_result poll_until_idle(aoahid_node* node, std::uint32_t timeout_ms,
                              bool drop_battery = false);

std::uint32_t remaining_milliseconds(std::chrono::steady_clock::time_point deadline) noexcept;

aoahid_result close_live_node_until(aoahid_node* node,
                                    std::chrono::steady_clock::time_point deadline,
                                    std::uint32_t control_timeout_ms);

bool graph_drained(const aoahid_device* device) noexcept;

bool wait_for_report_drain(aoahid_device* device, std::chrono::steady_clock::time_point deadline,
                           aoahid_result* poll_error) noexcept;

void release_channel_storage(aoahid_channel* channel) noexcept;

DeferredError destroy_drained_device(aoahid_device* device) noexcept;

aoahid_result device_close_impl(aoahid_device* device, bool* consumed,
                                // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                std::uint32_t control_timeout_ms, std::uint32_t drain_timeout_ms);

} // namespace aoa::detail
