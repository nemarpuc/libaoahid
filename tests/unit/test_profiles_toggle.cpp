// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Toggle: each HID 1.11 usage-control semantic (one-shot, retrigger, on/off, selector,
 * momentary) and its lifecycle in reports. */
#include "unit/profile_support.hpp"

namespace profile_test {

void test_usage_control_semantics() {
    const std::array<std::uint16_t, 6U> usages{0x00CDU, 0x00E9U, 0x00E2U,
                                               0x0201U, 0x00B0U, 0x00BBU};
    const std::array<aoahid_usage_semantic, 6U> semantics{
        AOAHID_USAGE_ONE_SHOT,        AOAHID_USAGE_RETRIGGER,     AOAHID_USAGE_ON_OFF_MAINTAINED,
        AOAHID_USAGE_SELECTOR_BITMAP, AOAHID_USAGE_ON_OFF_TOGGLE, AOAHID_USAGE_MOMENTARY};
    // These are deliberately test markers, not claims about a target mapping.
    const std::array<const char*, 6U> event_types{"TEST_EVENT_TYPE", "TEST_EVENT_TYPE",
                                                  "TEST_EVENT_TYPE", "TEST_EVENT_TYPE",
                                                  "TEST_EVENT_TYPE", "TEST_EVENT_TYPE"};
    const std::array<const char*, 6U> event_codes{"TEST_EVENT_CODE", "TEST_EVENT_CODE",
                                                  "TEST_EVENT_CODE", "TEST_EVENT_CODE",
                                                  "TEST_EVENT_CODE", "TEST_EVENT_CODE"};

    aoahid_toggle_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.application_page = aoa::hid::usage::page_consumer;
    options.application_usage = aoa::hid::usage::consumer_control;
    options.field_page = aoa::hid::usage::page_consumer;
    options.allowed_usages = usages.data();
    options.allowed_usage_count = usages.size();
    options.usage_semantics = semantics.data();
    options.expected_linux_event_types = event_types.data();
    options.expected_linux_codes = event_codes.data();

    aoahid_spec* spec = nullptr;
    check_battery_variant(options, &aoahid_spec_create_toggle);
    AOAHID_CHECK(aoahid_spec_create_toggle(&options, &spec) == AOAHID_OK);
    if (spec != nullptr) {
        AOAHID_CHECK(count_item(spec->descriptor, 0x81U, 0x06U) == 2U);
        AOAHID_CHECK(count_item(spec->descriptor, 0x81U, 0x02U) == 3U);
        AOAHID_CHECK(count_item(spec->descriptor, 0x81U, 0x22U) == 1U);
        for (std::uint16_t index = 0U; index < usages.size(); ++index) {
            const auto* control = field(spec, aoa::hid::FieldSemantic::consumer_usage, index);
            AOAHID_CHECK(control != nullptr && control->bit_width == 1U &&
                         control->logical_minimum == 0 && control->logical_maximum == 1);
        }

        aoahid_node node{};
        node.spec = spec;
        node.state = consumer_state_for(spec);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00E9U, 1U) == AOAHID_OK);
        std::array<std::uint8_t, 8U> report{};
        std::size_t report_length = 0U;
        AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(),
                                                 &report_length) == AOAHID_OK);
        AOAHID_CHECK(report_length == 1U);
        for (std::uint16_t index = 0U; index < usages.size(); ++index) {
            const auto* control = field(spec, aoa::hid::FieldSemantic::consumer_usage, index);
            AOAHID_CHECK(control != nullptr &&
                         extract(report.data(), *control) == (index == 1U ? 1U : 0U));
        }
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00E9U, 0U) == AOAHID_ERR_BUSY);
        aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00E9U, 0U) == AOAHID_OK);
        report.fill(0xFFU);
        AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(),
                                                 &report_length) == AOAHID_OK);
        AOAHID_CHECK(report[0] == 0U);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00E0U, 1U) == AOAHID_ERR_PARAM);

        // Several Usages can be held at once, and each is released by name.
        // Usage 0 is not in the allow-list, so there is no release-all call.
        aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00CDU, 1U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00E9U, 1U) == AOAHID_OK);
        report.fill(0U);
        AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(),
                                                 &report_length) == AOAHID_OK);
        for (std::uint16_t index = 0U; index < usages.size(); ++index) {
            const auto* control = field(spec, aoa::hid::FieldSemantic::consumer_usage, index);
            const bool held = usages[index] == 0x00CDU || usages[index] == 0x00E9U;
            AOAHID_CHECK(control != nullptr &&
                         extract(report.data(), *control) == (held ? 1U : 0U));
        }
        aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0x00CDU, 0U) == AOAHID_OK);
        report.fill(0U);
        AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(),
                                                 &report_length) == AOAHID_OK);
        for (std::uint16_t index = 0U; index < usages.size(); ++index) {
            const auto* control = field(spec, aoa::hid::FieldSemantic::consumer_usage, index);
            AOAHID_CHECK(control != nullptr &&
                         extract(report.data(), *control) == (usages[index] == 0x00E9U ? 1U : 0U));
        }
        aoa::detail::transfer_complete(&node, AOAHID_OK, 0);
        AOAHID_CHECK(aoahid_toggle_usage(&node, 0U, 0U) == AOAHID_ERR_PARAM);

        // The state object enforces the same accepted 1->0 lifecycle for each
        // caller-declared HUT semantic. Semantic flags stay descriptor policy;
        // the caller never hand-crafts an impossible re-trigger sequence.
        for (std::size_t selected = 0U; selected < usages.size(); ++selected) {
            aoahid_node semantic_node{};
            semantic_node.spec = spec;
            semantic_node.state = consumer_state_for(spec);
            AOAHID_CHECK(aoahid_toggle_usage(&semantic_node, usages[selected], 1U) == AOAHID_OK);
            report.fill(0U);
            AOAHID_CHECK(aoa::detail::serialize_node(&semantic_node, report.data(), report.size(),
                                                     &report_length) == AOAHID_OK);
            for (std::size_t index = 0U; index < usages.size(); ++index) {
                const auto* control = field(spec, aoa::hid::FieldSemantic::consumer_usage,
                                            static_cast<std::uint16_t>(index));
                AOAHID_CHECK(control != nullptr &&
                             extract(report.data(), *control) == (index == selected ? 1U : 0U));
            }
            AOAHID_CHECK(aoahid_toggle_usage(&semantic_node, usages[selected], 0U) ==
                         AOAHID_ERR_BUSY);
            aoa::detail::transfer_complete(&semantic_node, AOAHID_OK, 0);
            AOAHID_CHECK(aoahid_toggle_usage(&semantic_node, usages[selected], 0U) == AOAHID_OK);
            report.fill(0xFFU);
            AOAHID_CHECK(aoa::detail::serialize_node(&semantic_node, report.data(), report.size(),
                                                     &report_length) == AOAHID_OK);
            AOAHID_CHECK(aoahid_toggle_usage(&semantic_node, usages[selected], 1U) ==
                         AOAHID_ERR_BUSY);
            aoa::detail::transfer_complete(&semantic_node, AOAHID_OK, 0);
            AOAHID_CHECK(aoahid_toggle_usage(&semantic_node, usages[selected], 1U) == AOAHID_OK);
        }
    }
    aoahid_spec_release(spec);

    const auto exercise_shared_control_state = [](aoahid_spec* control_spec,
                                                  const std::uint16_t usage) {
        if (control_spec == nullptr)
            return;
        aoahid_node control_node{};
        control_node.spec = control_spec;
        control_node.state = consumer_state_for(control_spec);
        AOAHID_CHECK(aoahid_toggle_usage(&control_node, usage, 1U) == AOAHID_OK);
        std::array<std::uint8_t, 16U> control_report{};
        std::size_t control_length = 0U;
        AOAHID_CHECK(aoa::detail::serialize_node(&control_node, control_report.data(),
                                                 control_report.size(),
                                                 &control_length) == AOAHID_OK);
        const auto* selected = field(control_spec, aoa::hid::FieldSemantic::consumer_usage, 0U);
        AOAHID_CHECK(selected != nullptr && extract(control_report.data(), *selected) == 1U);
        aoa::detail::transfer_complete(&control_node, AOAHID_OK, 0);
        AOAHID_CHECK(aoahid_toggle_usage(&control_node, usage, 0U) == AOAHID_OK);
        control_report.fill(0xFFU);
        AOAHID_CHECK(aoa::detail::serialize_node(&control_node, control_report.data(),
                                                 control_report.size(),
                                                 &control_length) == AOAHID_OK);
        AOAHID_CHECK(selected != nullptr && extract(control_report.data(), *selected) == 0U);
    };

    aoahid_toggle_options invalid = options;
    invalid.usage_semantics = nullptr;
    spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_UNSET_FIELD);

    std::array<std::uint16_t, 2U> duplicate_usages{0x00CDU, 0x00CDU};
    std::array<aoahid_usage_semantic, 2U> duplicate_semantics{AOAHID_USAGE_ONE_SHOT,
                                                              AOAHID_USAGE_ONE_SHOT};
    std::array<const char*, 2U> duplicate_types{"TEST_EVENT_TYPE", "TEST_EVENT_TYPE"};
    std::array<const char*, 2U> duplicate_codes{"TEST_EVENT_CODE", "TEST_EVENT_CODE"};
    invalid = {};
    invalid.struct_size = static_cast<std::uint32_t>(sizeof(invalid));
    invalid.application_page = aoa::hid::usage::page_consumer;
    invalid.application_usage = aoa::hid::usage::consumer_control;
    invalid.field_page = aoa::hid::usage::page_consumer;
    invalid.allowed_usages = duplicate_usages.data();
    invalid.allowed_usage_count = duplicate_usages.size();
    invalid.usage_semantics = duplicate_semantics.data();
    invalid.expected_linux_event_types = duplicate_types.data();
    invalid.expected_linux_codes = duplicate_codes.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_PARAM);

    const std::array<std::uint16_t, 1U> volume_usage{0x00E0U};
    const std::array<aoahid_usage_semantic, 1U> volume_semantic{AOAHID_USAGE_LINEAR};
    const std::array<const char*, 1U> volume_type{"TEST_EVENT_TYPE"};
    const std::array<const char*, 1U> volume_code{"TEST_EVENT_CODE"};
    invalid.allowed_usages = volume_usage.data();
    invalid.allowed_usage_count = volume_usage.size();
    invalid.usage_semantics = volume_semantic.data();
    invalid.expected_linux_event_types = volume_type.data();
    invalid.expected_linux_codes = volume_code.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_UNSUPPORTED);

    std::array<aoahid_usage_semantic, 1U> invalid_semantic{-1};
    invalid.usage_semantics = invalid_semantic.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_PARAM);

    const std::array<std::uint16_t, 1U> collection_usage{0x0001U};
    invalid_semantic[0] = AOAHID_USAGE_SELECTOR_BITMAP;
    invalid.allowed_usages = collection_usage.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_PARAM);

    const std::array<std::uint16_t, 2U> camera_usages{0x20U, 0x21U};
    std::array<aoahid_usage_semantic, 2U> camera_semantics{AOAHID_USAGE_ONE_SHOT,
                                                           AOAHID_USAGE_ONE_SHOT};
    const std::array<const char*, 2U> camera_types{"TEST_EVENT_TYPE", "TEST_EVENT_TYPE"};
    const std::array<const char*, 2U> camera_codes{"TEST_EVENT_CODE", "TEST_EVENT_CODE"};
    invalid.field_page = aoa::hid::usage::page_camera_control;
    invalid.allowed_usages = camera_usages.data();
    invalid.allowed_usage_count = camera_usages.size();
    invalid.usage_semantics = camera_semantics.data();
    invalid.expected_linux_event_types = camera_types.data();
    invalid.expected_linux_codes = camera_codes.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_OK);
    exercise_shared_control_state(spec, camera_usages[0]);
    aoahid_spec_release(spec);
    spec = nullptr;
    camera_semantics[0] = AOAHID_USAGE_SELECTOR_BITMAP;
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_ERR_UNSUPPORTED);

    const std::array<std::uint16_t, 4U> system_usages{0x81U, 0x8AU, 0x97U, 0x98U};
    const std::array<aoahid_usage_semantic, 4U> system_semantics{
        AOAHID_USAGE_ONE_SHOT, AOAHID_USAGE_RETRIGGER, AOAHID_USAGE_MOMENTARY,
        AOAHID_USAGE_ON_OFF_TOGGLE};
    const std::array<const char*, 4U> system_types{"TEST_EVENT_TYPE", "TEST_EVENT_TYPE",
                                                   "TEST_EVENT_TYPE", "TEST_EVENT_TYPE"};
    const std::array<const char*, 4U> system_codes{"TEST_EVENT_CODE", "TEST_EVENT_CODE",
                                                   "TEST_EVENT_CODE", "TEST_EVENT_CODE"};
    invalid.application_page = aoa::hid::usage::page_generic_desktop;
    invalid.application_usage = aoa::hid::usage::system_control;
    invalid.field_page = aoa::hid::usage::page_generic_desktop;
    invalid.allowed_usages = system_usages.data();
    invalid.allowed_usage_count = system_usages.size();
    invalid.usage_semantics = system_semantics.data();
    invalid.expected_linux_event_types = system_types.data();
    invalid.expected_linux_codes = system_codes.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_OK);
    exercise_shared_control_state(spec, system_usages[0]);
    aoahid_spec_release(spec);
    spec = nullptr;

    const std::array<std::uint16_t, 4U> phone_usages{0x20U, 0x21U, 0x24U, 0xB0U};
    const std::array<aoahid_usage_semantic, 4U> phone_semantics{
        AOAHID_USAGE_ON_OFF_MAINTAINED, AOAHID_USAGE_MOMENTARY, AOAHID_USAGE_ONE_SHOT,
        AOAHID_USAGE_SELECTOR_BITMAP};
    const std::array<const char*, 4U> phone_types{"TEST_EVENT_TYPE", "TEST_EVENT_TYPE",
                                                  "TEST_EVENT_TYPE", "TEST_EVENT_TYPE"};
    const std::array<const char*, 4U> phone_codes{"TEST_EVENT_CODE", "TEST_EVENT_CODE",
                                                  "TEST_EVENT_CODE", "TEST_EVENT_CODE"};
    invalid.application_page = aoa::hid::usage::page_telephony;
    invalid.application_usage = aoa::hid::usage::phone;
    invalid.field_page = aoa::hid::usage::page_telephony;
    invalid.allowed_usages = phone_usages.data();
    invalid.allowed_usage_count = phone_usages.size();
    invalid.usage_semantics = phone_semantics.data();
    invalid.expected_linux_event_types = phone_types.data();
    invalid.expected_linux_codes = phone_codes.data();
    AOAHID_CHECK(aoahid_spec_create_toggle(&invalid, &spec) == AOAHID_OK);
    exercise_shared_control_state(spec, phone_usages[0]);
    aoahid_spec_release(spec);
}

} // namespace profile_test
