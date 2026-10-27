// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Toggle profile: a sparse allow-list of one-bit selector controls on any HUT
 * page. application_page/application_usage pick the Application Collection
 * (here HUT 0x0C/0x01 Consumer Control); field_page picks the Usage Page each
 * allowed_usages entry is read from. The same three fields let one factory
 * reach System Control (0x01/0x80), Camera keys (0x90 nested under Consumer),
 * or Telephony keys (0x0B/0x01) instead -- see the commented alternatives
 * below. Every Usage still needs an explicit HUT semantic and the target
 * event the caller expects it to produce, because the library refuses to
 * guess either one. */
#include "profile_example.h"

static const uint16_t k_usages[] = {0x00CDU, 0x00E9U, 0x00E2U, 0x0201U};
static const aoahid_usage_semantic k_semantics[] = {AOAHID_USAGE_ONE_SHOT, AOAHID_USAGE_RETRIGGER,
                                                    AOAHID_USAGE_ON_OFF_MAINTAINED,
                                                    AOAHID_USAGE_SELECTOR_BITMAP};
/* Placeholders. A product must name the events it verified on its own target. */
static const char* const k_expected[] = {"TEST_EVENT_CODE", "TEST_EVENT_CODE", "TEST_EVENT_CODE",
                                         "TEST_EVENT_CODE"};

static const char* const k_send_sequence[] = {
    "aoahid_toggle_usage(node, 0x00CD, 1)   assert one allow-listed Usage",
    "aoahid_node_submit(node, send_ms)  sends the 1 edge",
    "aoahid_toggle_usage(node, 0x00CD, 0)   release it",
    "aoahid_node_submit(node, send_ms)  sends the 0 edge that completes the tap", NULL};

int main(void) {
    aoahid_toggle_options options = {0};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    /* Consumer Control. For System Control instead: 0x01/0x80/0x01.
     * For Camera keys: application 0x0C/0x01, field_page 0x90 (Auto-focus
     * 0x20 / Shutter 0x21 only). For Telephony keys: 0x0B/0x01/0x0B. */
    options.application_page = 0x0CU;
    options.application_usage = 0x01U;
    options.field_page = 0x0CU;
    options.allowed_usages = k_usages;
    options.allowed_usage_count = sizeof k_usages / sizeof k_usages[0];
    options.usage_semantics = k_semantics;
    options.expected_linux_event_types = k_expected;
    options.expected_linux_codes = k_expected;

    result = aoahid_spec_create_toggle(&options, &spec);
    return aoahid_example_run("toggle", AOAHID_PROFILE_TOGGLE, result, spec, k_send_sequence);
}
