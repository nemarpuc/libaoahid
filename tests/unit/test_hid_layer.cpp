// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The rules for joining the descriptors of Nodes that share a registration: Report IDs, one
 * battery, clean globals, and the limits of the joined descriptor. No USB transport is involved. */
#include "api/internal.hpp"
#include "profiles/hid_layer.hpp"
#include "test.hpp"

#include <string_view>
#include <vector>

namespace {

using aoa::detail::JoinedDescriptor;
using aoa::detail::JoinRefusal;

aoahid_spec* keyboard_with_id(const std::uint8_t id, const bool battery = false,
                              const std::uint8_t battery_id = 0U) {
    aoahid_keyboard_options options;
    aoahid_keyboard_options_init(&options);
    options.usage_minimum = 0x04U;
    options.usage_maximum = 0x65U;
    if (id != 0U) {
        options.report_id = aoahid_report_id_option{1U, id, {}};
    }
    if (battery) {
        options.enable_battery = 1U;
        options.battery.report_id = aoahid_report_id_option{1U, battery_id, {}};
        options.battery.strength = aoahid_integer_field{0, 100, 8U, {}};
    }
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    return spec;
}

aoahid_spec* mouse_with_id(const std::uint8_t id) {
    aoahid_mouse_options options;
    aoahid_mouse_options_init(&options);
    if (id != 0U) {
        options.report_id = aoahid_report_id_option{1U, id, {}};
    }
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_mouse(&options, &spec) == AOAHID_OK);
    return spec;
}

aoahid_result join(const std::vector<aoahid_spec*>& specs, JoinedDescriptor* joined,
                   JoinRefusal* refusal) {
    return aoa::detail::join_descriptors(
        std::vector<const aoahid_spec*>(specs.begin(), specs.end()), joined, refusal);
}

void release_all(const std::vector<aoahid_spec*>& specs) {
    for (aoahid_spec* spec : specs) {
        aoahid_spec_release(spec);
    }
}

bool field_is(const JoinRefusal& refusal, const char* field) {
    return refusal.field != nullptr && std::string_view(refusal.field) == field;
}

void test_one_spec_is_joined_as_it_is() {
    aoahid_spec* keyboard = keyboard_with_id(0U);
    JoinedDescriptor joined;
    JoinRefusal refusal;
    // A single Spec needs no Report ID, as before registrations could be shared.
    AOAHID_CHECK(join({keyboard}, &joined, &refusal) == AOAHID_OK);
    AOAHID_CHECK(joined.bytes == keyboard->descriptor);
    release_all({keyboard});
}

void test_two_specs_are_concatenated_in_order() {
    aoahid_spec* keyboard = keyboard_with_id(1U);
    aoahid_spec* mouse = mouse_with_id(2U);
    JoinedDescriptor joined;
    JoinRefusal refusal;
    AOAHID_CHECK(join({keyboard, mouse}, &joined, &refusal) == AOAHID_OK);
    std::vector<std::uint8_t> expected = keyboard->descriptor;
    expected.insert(expected.end(), mouse->descriptor.begin(), mouse->descriptor.end());
    AOAHID_CHECK(joined.bytes == expected);
    // The requirements are the largest of the members, not their sum.
    AOAHID_CHECK(joined.requirements.maximum_usages >=
                 keyboard->requirements.maximum_usages);
    AOAHID_CHECK(joined.requirements.maximum_usages >= mouse->requirements.maximum_usages);
    release_all({keyboard, mouse});
}

void test_missing_and_duplicate_report_ids_are_refused() {
    aoahid_spec* without = keyboard_with_id(0U);
    aoahid_spec* mouse = mouse_with_id(2U);
    JoinedDescriptor joined;
    JoinRefusal refusal;
    AOAHID_CHECK(join({without, mouse}, &joined, &refusal) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(field_is(refusal, "node.report_id"));
    AOAHID_CHECK(joined.bytes.empty());
    release_all({without, mouse});

    aoahid_spec* first = keyboard_with_id(2U);
    aoahid_spec* second = mouse_with_id(2U);
    AOAHID_CHECK(join({first, second}, &joined, &refusal) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(field_is(refusal, "node.report_id") && refusal.report_id == 2U);
    release_all({first, second});
}

void test_a_battery_report_id_counts_as_taken() {
    aoahid_spec* first = keyboard_with_id(1U, true, 2U);
    aoahid_spec* second = mouse_with_id(2U);
    JoinedDescriptor joined;
    JoinRefusal refusal;
    AOAHID_CHECK(join({first, second}, &joined, &refusal) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(refusal.report_id == 2U);
    release_all({first, second});
}

void test_two_batteries_are_refused() {
    aoahid_spec* first = keyboard_with_id(1U, true, 2U);
    aoahid_spec* second = keyboard_with_id(3U, true, 4U);
    JoinedDescriptor joined;
    JoinRefusal refusal;
    AOAHID_CHECK(join({first, second}, &joined, &refusal) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(field_is(refusal, "node.battery"));
    release_all({first, second});
}

void test_unclean_globals_before_another_member_are_refused() {
    // Usage Page, Usage, Collection, Unit Exponent 1, End Collection: leaves Unit Exponent set.
    const std::vector<std::uint8_t> dirty{0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U,
                                          0x55U, 0x01U, 0xC0U};
    AOAHID_CHECK(!aoa::detail::ends_with_clean_globals(dirty));
    const std::vector<std::uint8_t> reset{0x05U, 0x01U, 0x55U, 0x01U, 0x55U, 0x00U};
    AOAHID_CHECK(aoa::detail::ends_with_clean_globals(reset));
    // A Push without its Pop leaves the stack open.
    const std::vector<std::uint8_t> pushed{0xA4U};
    AOAHID_CHECK(!aoa::detail::ends_with_clean_globals(pushed));
    // A Physical Maximum that is set and not reset.
    const std::vector<std::uint8_t> physical{0x46U, 0xE8U, 0x03U};
    AOAHID_CHECK(!aoa::detail::ends_with_clean_globals(physical));
    AOAHID_CHECK(aoa::detail::ends_with_clean_globals({}));
}

void test_every_generated_profile_ends_with_clean_globals() {
    // Any profile may be followed by another in a registration.
    aoahid_spec* keyboard = keyboard_with_id(1U);
    aoahid_spec* mouse = mouse_with_id(2U);
    AOAHID_CHECK(aoa::detail::ends_with_clean_globals(keyboard->descriptor));
    AOAHID_CHECK(aoa::detail::ends_with_clean_globals(mouse->descriptor));
    release_all({keyboard, mouse});
}

} // namespace

void test_hid_layer() {
    test_one_spec_is_joined_as_it_is();
    test_two_specs_are_concatenated_in_order();
    test_missing_and_duplicate_report_ids_are_refused();
    test_a_battery_report_id_counts_as_taken();
    test_two_batteries_are_refused();
    test_unclean_globals_before_another_member_are_refused();
    test_every_generated_profile_ends_with_clean_globals();
}
