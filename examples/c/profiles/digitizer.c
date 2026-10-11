// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Digitizer profile: the same stylus report shape as Pen (pressure, tilt,
 * hover, and the HUT Invert selector for the opposite end) under the
 * Digitizers / Digitizer Application Collection. Barrel controls are
 * individual Digitizers-page Usage IDs because HUT does not define them as a
 * range. */
#include "profile_example.h"

static const uint16_t k_barrel_usages[] = {0x44U, 0x5AU};

static const char* const k_send_sequence[] = {
    "aoahid_digitizer_update(node, &sample)   tip, pressure, and in-range stay consistent",
    "aoahid_node_submit(node)                 Away state is normalized on the wire",
    "aoahid_digitizer_depart(node)            explicit departure before a tool switch",
    "aoahid_node_submit(node)                 an in-range pen/eraser switch departs first", NULL};

int main(void) {
    aoahid_digitizer_options options = {0};
    const aoahid_integer_field coordinate = {0, 32767, 16U, {0, 0, 0, 0, 0}};
    const aoahid_integer_field tilt = {-90, 90, 8U, {0, 0, 0, 0, 0}};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.x = coordinate;
    options.y = coordinate;
    options.enable_pressure = 1U;
    options.pressure = (aoahid_integer_field){0, 4095, 12U, {0, 0, 0, 0, 0}};
    options.enable_tilt = 1U;
    options.tilt_x = tilt;
    options.tilt_y = tilt;
    options.barrel_usages = k_barrel_usages;
    options.barrel_usage_count = sizeof k_barrel_usages / sizeof k_barrel_usages[0];
    options.enable_eraser = 1U;
    options.enable_hover = 1U;

    result = aoahid_spec_create_digitizer(&options, &spec);
    return aoahid_example_run("digitizer", AOAHID_PROFILE_DIGITIZER, result, spec, k_send_sequence);
}
