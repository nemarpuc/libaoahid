// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Gamepad profile: two declared axes, twelve buttons, and a Hat Switch.
 * The Android portable-candidate Hat requires an explicit logical 0..7 in four
 * bits, so those values are stated rather than inferred. */
#include "profile_example.h"

static const aoahid_integer_field k_stick = {-32767, 32767, 16U, {0, 0, 0, 0, 0}};

static const char* const k_send_sequence[] = {
    "aoahid_gamepad_axis(node, 0, 0)   axis index 0, caller-declared range",
    "aoahid_gamepad_dpad(node, 1, 0, 0, 0)         booleans become a canonical Hat value",
    "aoahid_gamepad_button(node, 0, 1)     button index 0 is Button 1",
    "aoahid_node_submit(node)              opposite D-pad pairs are rejected", NULL};

int main(void) {
    const aoahid_gamepad_axis_option axes[] = {
        {AOAHID_AXIS_X, 0x01U, 0x30U, k_stick, 0, "ABS_X", "AXIS_X"},
        {AOAHID_AXIS_Y, 0x01U, 0x31U, k_stick, 0, "ABS_Y", "AXIS_Y"}};
    aoahid_gamepad_options options = {0};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.axes = axes;
    options.axis_count = sizeof axes / sizeof axes[0];
    options.button_count = 12U;
    options.button_usage_minimum = 1U;
    options.dpad_representation = AOAHID_DPAD_HAT;
    options.hat_logical_minimum = 0;
    options.hat_logical_maximum = 7;
    options.hat_bit_width = 4U;

    result = aoahid_spec_create_gamepad(&options, &spec);
    return aoahid_example_run("gamepad", AOAHID_PROFILE_GAMEPAD, result, spec, k_send_sequence);
}
