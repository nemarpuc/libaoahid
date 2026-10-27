// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Mouse profile: five buttons with signed relative X/Y plus Wheel and AC Pan.
 * Every range and width below is a caller choice, not a library default. */
#include "profile_example.h"

static const char* const k_send_sequence[] = {
    "aoahid_mouse_move(node, 10, -5)   pending totals are 64-bit and signed",
    "aoahid_mouse_scroll(node, 1, 0)   Wheel and AC Pan are independent",
    "aoahid_mouse_button(node, 0, 1)   button index 0 is Button 1",
    "aoahid_node_submit(node, send_ms)  only the submitted fragment is consumed", NULL};

int main(void) {
    aoahid_mouse_options options = {0};
    const aoahid_integer_field axis = {-32767, 32767, 16U, {0, 0, 0, 0, 0}};
    const aoahid_integer_field detent = {-127, 127, 8U, {0, 0, 0, 0, 0}};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.button_count = 5U;
    options.x = axis;
    options.y = axis;
    options.enable_wheel = 1U;
    options.wheel = detent;
    options.enable_pan = 1U;
    options.pan = detent;

    result = aoahid_spec_create_mouse(&options, &spec);
    return aoahid_example_run("mouse", AOAHID_PROFILE_MOUSE, result, spec, k_send_sequence);
}
