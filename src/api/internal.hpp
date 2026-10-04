// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/*
 * This file owns the private C-handle representations and shared invariants.
 * It contains no public ABI declarations beyond the opaque tag definitions.
 */

#include "aoahid.h"
#include "hid/report_layout.hpp"
#include "hid/validator.hpp"
#include "transport/transport.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace aoa::detail {

struct KeyboardConfig {
    aoahid_keyboard_options options{};
};

struct MouseConfig {
    aoahid_mouse_options options{};
};

struct ConsumerConfig {
    aoahid_toggle_options options{};
    std::vector<std::uint16_t> allowed_usages;
    std::vector<aoahid_usage_semantic> usage_semantics;
    std::vector<std::string> expected_linux_event_types;
    std::vector<std::string> expected_linux_codes;
};

struct GamepadAxisConfig {
    aoahid_gamepad_axis axis{};
    std::string expected_linux_code;
    std::string expected_android_axis;
};

struct GamepadConfig {
    aoahid_gamepad_options options{};
    std::vector<GamepadAxisConfig> axes;
};

/* Profile-agnostic contact-field representation shared by Touchscreen and
 * Touchpad. button_count is always here; Touchscreen always passes zero, so
 * the button state machine below needs no profile_kind branch. */
struct TouchFields {
    aoahid_report_id_option report_id{};
    std::uint32_t maximum_contacts{};
    std::uint32_t contacts_per_report{};
    aoahid_integer_field contact_identifier{};
    aoahid_integer_field x{};
    aoahid_integer_field y{};
    aoahid_integer_field contact_count{};
    std::uint32_t enable_pressure{};
    aoahid_integer_field pressure{};
    std::uint32_t enable_width{};
    aoahid_integer_field width{};
    std::uint32_t enable_height{};
    aoahid_integer_field height{};
    std::uint32_t enable_azimuth{};
    aoahid_integer_field azimuth{};
    std::uint32_t enable_scan_time{};
    aoahid_integer_field scan_time{};
    std::uint32_t scan_time_unit_100us{};
    std::uint32_t enable_contact_count_maximum_feature_declaration{};
    std::uint32_t enable_multi_packet_frames{};
    std::uint32_t button_count{};
};

struct TouchConfig {
    TouchFields fields{};
};

struct PenConfig {
    aoahid_pen_options options{};
    std::vector<std::uint16_t> barrel_usages;
};

struct BatteryConfig {
    aoahid_battery_options options{};
};

struct RawConfig {
    std::vector<aoahid_raw_report> reports;
    std::vector<std::uint8_t> trailing_valid_masks;
};

struct HidIdDomain {
    std::uint8_t bus{};
    std::vector<std::uint8_t> port_path;
    std::uint32_t next_hid_id{1U};
    bool unidentified{};
};

/* Fixed-size diagnostics may cross from the internal event thread to an
 * application thread and may outlive a consumed Device handle. All strings
 * stored here are static literals; this latch never allocates. */
struct DeferredError {
    aoa::transport::ErrorInfo transport{};
    const char* field{};
    const char* reason{};
    std::uint16_t report_id{};
    bool pending{};
};

using SpecConfig = std::variant<std::monostate, KeyboardConfig, MouseConfig, ConsumerConfig,
                                GamepadConfig, TouchConfig, PenConfig, BatteryConfig, RawConfig>;

enum class ContactPhase : std::uint8_t { none, down, up };

struct ContactState {
    ContactPhase phase{ContactPhase::none};
    aoahid_touch_contact sample{};
    /* A Down edge must reach one accepted frame before Up may replace it. */
    bool lifecycle_transition_pending{};
};

struct KeyboardState {
    std::vector<std::uint8_t> pressed_bitmap;
    std::vector<std::uint8_t> key_transitions;
    std::size_t pressed_bitmap_count{};
    std::uint8_t modifiers{};
    std::uint8_t modifier_transitions{};
};

struct MouseState {
    std::vector<std::uint8_t> buttons;
    std::vector<std::uint8_t> button_transitions;
    std::int64_t dx{};
    std::int64_t dy{};
    std::int64_t wheel{};
    std::int64_t pan{};
};

struct ConsumerState {
    std::uint16_t usage{};
    bool transition_pending{};
};

struct GamepadState {
    std::vector<std::int32_t> axes;
    std::vector<std::uint8_t> buttons;
    std::vector<std::uint8_t> button_transitions;
    bool hat_has_direction{};
    std::int32_t hat{};
    bool hat_transition_pending{};
};

struct TouchState {
    std::array<ContactState, 16> contacts{};
    std::vector<std::uint8_t> buttons;
    std::vector<std::uint8_t> button_transitions;
    std::uint32_t scan_time{};
    std::size_t packet_cursor{};
    std::chrono::steady_clock::time_point scan_epoch{};
    bool scan_epoch_active{};
};

struct PenState {
    aoahid_pen_sample desired{};
    aoahid_pen_sample emitted{};
    std::uint32_t barrel_transitions{};
    bool tool_switch_departure{};
    bool in_range_transition_pending{};
    bool tip_transition_pending{};
    bool eraser_transition_pending{};
};

struct BatteryState {
    bool has_value{};
    std::int32_t strength{};
};

struct RawState {
    const std::uint8_t* pending{};
    std::size_t pending_length{};
};

using NodeState = std::variant<std::monostate, KeyboardState, MouseState, ConsumerState,
                               GamepadState, TouchState, PenState, BatteryState, RawState>;

void set_error(aoahid_result result, const char* field, const char* reason,
               std::int32_t native_status = 0, std::int32_t aoa_request = 0,
               std::uint16_t hid_id = 0, std::uint16_t report_id = 0, std::uint32_t offset = 0,
               std::uint32_t length = 0) noexcept;
void clear_error() noexcept;
aoahid_result finish_result(aoahid_result result, const char* field, const char* reason,
                            std::int32_t native_status = 0, std::int32_t aoa_request = 0,
                            std::uint16_t hid_id = 0, std::uint16_t report_id = 0,
                            std::uint32_t offset = 0, std::uint32_t length = 0) noexcept;
const char* result_name(aoahid_result result) noexcept;
bool retain_spec(struct aoahid_spec* spec) noexcept;
bool release_spec(struct aoahid_spec* spec) noexcept;
void emit_log(const struct aoahid_context* context, aoahid_log_level level,
              const char* message) noexcept;

struct LogEventStatus {
    aoahid_result result{AOAHID_OK};
    std::int32_t native_status{};
    std::int32_t aoa_request{};
    std::uint32_t offset{};
    std::uint32_t length{};
};

void log_event(const struct aoahid_context* context, aoahid_log_level level, const char* event,
               const struct aoahid_device* device, const struct aoahid_node* node,
               LogEventStatus status = {}) noexcept;

/* The event mode is fixed before a Context or Node becomes observable. A null
 * mutex therefore selects the caller-serialized fast path without acquiring a
 * lock; internal-thread mode stores the corresponding mutex here. */
class ModeMutexGuard final {
  public:
    explicit ModeMutexGuard(std::mutex* mutex) : mutex_(mutex) {
        if (mutex_ != nullptr) {
            mutex_->lock();
        }
    }

    ~ModeMutexGuard() noexcept {
        if (mutex_ != nullptr) {
            mutex_->unlock();
        }
    }

    ModeMutexGuard(const ModeMutexGuard&) = delete;
    ModeMutexGuard& operator=(const ModeMutexGuard&) = delete;

  private:
    std::mutex* mutex_{};
};

bool valid_boolean(std::uint32_t value) noexcept;
bool valid_struct(const void* value, std::uint32_t actual_size, std::uint32_t expected_size,
                  const char* field) noexcept;

aoahid_result serialize_node(struct aoahid_node* node, std::uint8_t* report, std::size_t capacity,
                             std::size_t* report_length) noexcept;
aoahid_result validate_serialized_report(const struct aoahid_spec* spec, const std::uint8_t* report,
                                         std::size_t report_length) noexcept;
void transfer_complete(void* user, aoahid_result result, std::int32_t native_status) noexcept;

} // namespace aoa::detail

struct aoahid_spec {
    std::atomic<std::uint32_t> references{1U};
    aoahid_profile_kind kind{};
    aoahid_android_status android_status{};
    std::vector<std::uint8_t> descriptor;
    aoa::hid::ReportLayout layout;
    aoa::hid::DescriptorRequirements requirements;
    std::vector<aoahid_report_capability> report_capabilities;
    aoa::detail::SpecConfig config;
};

struct aoahid_context {
    aoa::transport::Runtime* runtime{};
    aoahid_context_options options{};
    std::vector<struct aoahid_device*> devices;
    std::vector<struct aoahid_device*> graveyard;
    // Read without the Context mutex so an idle poll never locks just to find
    // the graveyard empty, which is the state of every ordinary send cycle.
    std::atomic<std::size_t> graveyard_size{0U};
    std::atomic<bool> stop_event_thread{false};
    std::atomic<bool> event_thread_failed{false};
    std::atomic<bool> event_thread_exited{false};
    std::thread event_thread;
    std::mutex mutex;
    std::mutex* active_state_mutex{};
    std::condition_variable graveyard_cv;
    std::vector<aoa::detail::HidIdDomain> hid_id_domains;
    aoa::detail::DeferredError deferred_error;
    // Unlike ordinary first-error delivery, an event-thread termination is a
    // persistent fatal state: accepting more asynchronous work would leave it
    // without an event pump.
    aoa::detail::DeferredError event_thread_error;
};

struct aoahid_discovery_entry {
    aoa::transport::Candidate candidate;
    aoahid_device_info public_info{};
};

struct aoahid_discovery {
    std::vector<aoahid_discovery_entry> entries;
};

struct aoahid_device {
    aoahid_context* context{};
    aoa::transport::Device* transport{};
    aoa::transport::DeviceConfig config{};
    std::uint32_t close_drain_timeout_ms{};
    std::uint32_t aoa_descriptor_policy_bytes{};
    std::uint32_t linux_descriptor_policy_bytes{};
    std::uint32_t linux_hid_fields_per_report_policy{};
    std::uint32_t linux_hid_global_stack_depth_policy{};
    std::uint64_t linux_hid_usages_policy{};
    std::uint64_t linux_hid_report_data_bits_policy{};
    std::uint32_t linux_hid_report_size_bits_policy{};
    std::uint32_t target_ep0_policy_bytes{};
    std::uint32_t host_control_policy_bytes{};
    std::size_t hid_id_domain_index{};
    bool validate_reports{};
    std::uint8_t bus{};
    std::vector<std::uint8_t> port_path;
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::vector<struct aoahid_node*> nodes;
    std::vector<struct aoahid_channel*> channels;
    std::atomic<bool> closing{false};
    aoahid_result close_result{AOAHID_OK};
    aoa::detail::DeferredError close_error;
    // Pool-slot release signal. Every report completion bumps the generation
    // (its slot is already free by then); a submit_blocking caller that found
    // every slot taken waits for the next bump instead of polling.
    std::mutex slot_mutex;
    std::condition_variable slot_cv;
    std::atomic<std::uint32_t> slot_waiters{0U};
    std::atomic<std::uint64_t> slot_generation{0U};
};

struct aoahid_channel {
    aoahid_device* device{};
    aoa::transport::Channel* transport{};
};

struct aoahid_node {
    aoahid_device* device{};
    aoahid_spec* spec{};
    aoa::detail::NodeState state;
    std::uint16_t hid_id{};
    std::uint32_t reserved_slots{};
    aoa::transport::ReservationToken reservation{};
    std::atomic<bool> transfer_inflight{false};
    std::atomic<std::uint32_t> completion_waiters{0U};
    std::mutex completion_mutex;
    std::mutex* active_completion_mutex{};
    std::condition_variable completion_cv;
    std::atomic<aoahid_result> completion_result{AOAHID_OK};
    std::atomic<std::int32_t> completion_native_status{0};
    std::atomic<std::uint32_t> completion_report_length{0U};
    std::atomic<std::uint16_t> completion_report_id{0U};
    std::int64_t submitted_dx{};
    std::int64_t submitted_dy{};
    std::int64_t submitted_wheel{};
    std::int64_t submitted_pan{};
    std::size_t submitted_report_length{};
    std::uint16_t submitted_report_id{};
    std::size_t submitted_touch_count{};
    bool submitted_touch_final_packet{};
    bool submitted_non_neutral{};
    bool emitted_non_neutral{};
    // Registration alone sends nothing; only a real mutation marks the Node.
    bool dirty{false};
    bool closed{false};
};
