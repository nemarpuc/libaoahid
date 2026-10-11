// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Types a short message repeatedly, so a person watching the phone can
 * confirm real key presses actually arrive as text.
 *
 * Before running: open a text field on the phone (Notes app, a search box,
 * anything with a blinking text cursor). This program waits a few seconds
 * after connecting so you have time to tap it into focus.
 *
 * See docs/QUICKSTART.md for the full setup this assumes (data-capable
 * cable, udev/driver permissions, etc.). Ctrl+C to stop early. */
#include "verify_common.h"

/* HUT 1.7 section 10, Keyboard/Keypad Page 0x07. Zero means "not mapped by
 * this small demo", not "not a valid HID Usage". */
static uint16_t usage_for_char(char c) {
    if (c >= 'a' && c <= 'z')
        return (uint16_t)(0x04 + (c - 'a'));
    if (c == ' ')
        return 0x2C;
    if (c == '\n')
        return 0x28; /* Enter */
    if (c >= '1' && c <= '9')
        return (uint16_t)(0x1E + (c - '1'));
    if (c == '0')
        return 0x27;
    return 0;
}

static int type_char(aoahid_node* node, char c) {
    const uint16_t usage = usage_for_char(c);
    if (usage == 0)
        return 0; /* Silently skip characters this small demo does not map. */
    aoahid_result result = aoahid_keyboard_key(node, usage, 1U);
    if (result != AOAHID_OK)
        return verify_fail("key down", result);
    result = verify_submit(node);
    if (result != AOAHID_OK)
        return verify_fail("submit key down", result);
    verify_sleep_ms(60);
    result = aoahid_keyboard_key(node, usage, 0U);
    if (result != AOAHID_OK)
        return verify_fail("key up", result);
    result = verify_submit(node);
    if (result != AOAHID_OK)
        return verify_fail("submit key up", result);
    verify_sleep_ms(120);
    return 0;
}

int main(void) {
    aoahid_context* context = NULL;
    aoahid_device* device = NULL;
    if (verify_open_first_device(&context, &device) != 0)
        return 1;

    aoahid_keyboard_options options = {0};
    options.struct_size = (uint32_t)sizeof options;
    options.usage_minimum = 0x04U;
    options.usage_maximum = 0x65U;
    aoahid_spec* spec = NULL;
    aoahid_result result = aoahid_spec_create_keyboard(&options, &spec);
    if (result != AOAHID_OK) {
        aoahid_device_close(device);
        aoahid_context_destroy(context);
        return verify_fail("keyboard spec create", result);
    }

    const aoahid_node_options node_options = {(uint32_t)sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = NULL;
    result = aoahid_node_open(device, spec, &node_options, &node);
    aoahid_spec_release(spec);
    if (result != AOAHID_OK) {
        aoahid_device_close(device);
        aoahid_context_destroy(context);
        return verify_fail("keyboard node open", result);
    }

    printf("Open a text field on the phone now.\n");
    printf("Typing in 3 seconds...\n");
    verify_sleep_ms(3000);

    const char* message = "hello from libaoahid\n";
    for (int round = 0; round < 3; ++round) {
        for (const char* p = message; *p != '\0'; ++p) {
            if (type_char(node, *p) != 0) {
                aoahid_node_close(node);
                aoahid_device_close(device);
                aoahid_context_destroy(context);
                return 1;
            }
        }
    }

    printf("done. Expected result: \"%s\" typed 3 times.\n", "hello from libaoahid");
    aoahid_node_close(node);
    aoahid_device_close(device);
    aoahid_context_destroy(context);
    return 0;
}
