// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * The pen and digitizer profiles: their sample mutators.
 */

#include "profiles/state_support.hpp"

using namespace aoa::state_build;

extern "C" {

static aoahid_result stylus_update_impl(aoahid_node* node, const aoahid_profile_kind kind,
                                        const aoahid_pen_sample* sample) {
    aoa::detail::clear_error();
    const aoahid_result check =
        usable(node, kind, kind == AOAHID_PROFILE_PEN ? "pen.update" : "digitizer.update");
    if (check != AOAHID_OK)
        return check;
    if (sample == nullptr) {
        set_error(AOAHID_ERR_PARAM, kind == AOAHID_PROFILE_PEN ? "pen.sample" : "digitizer.sample",
                  "The pen sample pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    const auto* spec = config<aoa::detail::PenConfig>(node);
    const auto& options = spec->options;
    const std::uint32_t barrel_mask =
        options.barrel_usage_count == 32U
            ? std::numeric_limits<std::uint32_t>::max()
            : (options.barrel_usage_count == 0U ? 0U : (1U << options.barrel_usage_count) - 1U);
    if (!aoa::detail::valid_boolean(sample->in_range) || !aoa::detail::valid_boolean(sample->tip) ||
        !aoa::detail::valid_boolean(sample->eraser) ||
        (!options.enable_hover && sample->in_range != sample->tip) ||
        (sample->tip == 1U && sample->in_range == 0U) ||
        (options.enable_eraser == 0U && sample->eraser != 0U) ||
        (sample->barrel_buttons & ~barrel_mask) != 0U || !in_range(sample->x, options.x) ||
        !in_range(sample->y, options.y) ||
        (options.enable_pressure == 0U && sample->pressure != 0) ||
        (options.enable_tilt == 0U && (sample->tilt_x != 0 || sample->tilt_y != 0)) ||
        (options.enable_twist_target_specific == 0U && sample->twist != 0) ||
        (options.enable_pressure == 1U && !in_range(sample->pressure, options.pressure)) ||
        (options.enable_tilt == 1U && (!in_range(sample->tilt_x, options.tilt_x) ||
                                       !in_range(sample->tilt_y, options.tilt_y))) ||
        (options.enable_twist_target_specific == 1U && !in_range(sample->twist, options.twist))) {
        set_error(AOAHID_ERR_PARAM, kind == AOAHID_PROFILE_PEN ? "pen.sample" : "digitizer.sample",
                  "A flag, button, or value is outside the immutable pen spec.");
        return AOAHID_ERR_PARAM;
    }
    auto* pen = state<aoa::detail::PenState>(node);
    const bool changed =
        pen->desired.in_range != sample->in_range || pen->desired.tip != sample->tip ||
        pen->desired.eraser != sample->eraser ||
        pen->desired.barrel_buttons != sample->barrel_buttons || pen->desired.x != sample->x ||
        pen->desired.y != sample->y || pen->desired.pressure != sample->pressure ||
        pen->desired.tilt_x != sample->tilt_x || pen->desired.tilt_y != sample->tilt_y ||
        pen->desired.twist != sample->twist;
    if (pen_transition_conflicts(*pen, *sample)) {
        return pending_transition(kind == AOAHID_PROFILE_PEN ? "pen.update" : "digitizer.update");
    }
    record_pen_transitions(pen, *sample);
    pen->desired = *sample;
    pen->tool_switch_departure =
        pen->emitted.in_range == 1U && pen->emitted.eraser != pen->desired.eraser;
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}

aoahid_result AOAHID_CALL aoahid_pen_update(aoahid_node* node, const aoahid_pen_sample* sample) {
    return abi_state_result(node, "pen.update",
                            [&] { return stylus_update_impl(node, AOAHID_PROFILE_PEN, sample); });
}

aoahid_result AOAHID_CALL aoahid_digitizer_update(aoahid_node* node,
                                                  const aoahid_digitizer_sample* sample) {
    return abi_state_result(node, "digitizer.update", [&] {
        const aoahid_pen_sample values =
            sample == nullptr
                ? aoahid_pen_sample{}
                : aoahid_pen_sample{sample->in_range,       sample->tip,    sample->eraser,
                                    sample->barrel_buttons, sample->x,      sample->y,
                                    sample->pressure,       sample->tilt_x, sample->tilt_y,
                                    sample->twist};
        return stylus_update_impl(node, AOAHID_PROFILE_DIGITIZER,
                                  sample == nullptr ? nullptr : &values);
    });
}

} // extern "C"
