// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The "transport" suite: runs the transport sim tests of the test_*.cpp files next to
 * this one in a fixed order. */
#include "sim/transport_support.hpp"

void test_transport() {
    transport_sim::test_runtime_initialization_options();
    transport_sim::test_registration_reservations_and_async_results();
    transport_sim::test_uncertain_register_attempts_unregister_and_preserves_error();
    transport_sim::test_descriptor_failure_unregisters_and_preserves_error();
    transport_sim::test_error_report_id_is_published_before_user_callback_returns();
    transport_sim::test_current_mode_open_and_explicit_claim();
    transport_sim::test_port_ownership_and_discovery_reuse();
    transport_sim::test_accessory_start_sends_51_52_53_and_closes();
    transport_sim::test_accessory_start_rejects_and_reports_failures();
    transport_sim::test_channel_shares_the_device_handle_internal_thread();
    transport_sim::test_channel_configures_an_unconfigured_device();
    transport_sim::test_channel_request_reads();
    transport_sim::test_hid_adb_and_accessory_bulk_run_together();
    transport_sim::test_channel_caller_poll_and_device_close();
    transport_sim::test_open_sends_no_aoa_request();
    transport_sim::test_required_device_policies_precede_usb_io();
    transport_sim::test_tuning_values_required_and_zero_reservation();
    transport_sim::test_initializers_open_a_working_session();
    transport_sim::test_cancel_and_device_isolation();
    transport_sim::test_blocking_event_wait_is_woken_by_cancel();
    transport_sim::test_public_touchpad_button_only_close();
    transport_sim::test_public_controller_close_neutralizes_dpad();
    transport_sim::test_descriptor_requirements_are_caller_policies();
    transport_sim::test_public_touch_scan_time_lifecycle();
    transport_sim::test_public_touch_scan_time_rejected_first_attempt_stays_zero();
    transport_sim::test_close_drains_touch_frame_before_neutral();
    transport_sim::test_battery_reports();
    transport_sim::test_battery_waits_for_the_touch_frame();
    transport_sim::test_battery_reports_internal_thread();
    transport_sim::test_public_error_contract_and_raw_padding();
    transport_sim::test_first_report_stall_is_returned_without_retry();
    transport_sim::test_submit_blocking_waits_for_a_free_slot_without_spinning();
    transport_sim::test_submit_rejection_retains_state_and_close_releases();
    transport_sim::test_node_open_host_policy_failure_precedes_request54();
    transport_sim::test_node_close_timeout_distinction_and_loss_freeze();
    transport_sim::test_node_close_retains_terminal_link_loss();
    transport_sim::test_node_close_with_report_in_flight_keeps_node();
    transport_sim::test_node_close_failed_unregister_consumes_node();
    transport_sim::test_caller_poll_hot_path_is_lock_and_allocation_free();
    transport_sim::test_channel_hot_path_is_lock_and_allocation_free();
    transport_sim::test_accepted_submit_defers_opportunistic_poll_error();
    transport_sim::test_context_destroy_aggregates_unregister_error();
    transport_sim::test_context_destroy_never_returns_terminal_pending();
    transport_sim::test_graveyard_publishes_unregister_and_cancel_errors();
    transport_sim::test_public_graveyard_delayed_cancel();
    transport_sim::test_public_graveyard_blocking_destroy();
    transport_sim::test_blocking_destroy_deadline_retains_context();
}
