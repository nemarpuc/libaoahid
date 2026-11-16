// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Prints, for every option structure, the bytes the static inline aoahid_*_init function leaves.
 * The foreign-binding tests compare their own initial values with these (pointer fields excepted).
 */
#include "aoahid.h"

#include <stdio.h>

#define DUMP(name)                                                                                 \
    do {                                                                                           \
        aoahid_##name value;                                                                       \
        const unsigned char* bytes = (const unsigned char*)&value;                                 \
        aoahid_##name##_init(&value);                                                              \
        printf("%s=", #name);                                                                      \
        for (size_t i = 0; i < sizeof(value); ++i)                                                 \
            printf("%02x", (unsigned)bytes[i]);                                                    \
        printf("\n");                                                                              \
    } while (0)

int main(void) {
    DUMP(context_options);
    DUMP(device_options);
    DUMP(accessory_options);
    DUMP(channel_options);
    DUMP(node_options);
    DUMP(keyboard_options);
    DUMP(mouse_options);
    DUMP(toggle_options);
    DUMP(gamepad_options);
    DUMP(touchscreen_options);
    DUMP(touchpad_options);
    DUMP(pen_options);
    DUMP(digitizer_options);
    DUMP(raw_options);

    /* What the pointer fields of the gamepad, pen and accessory initializers point at. */
    {
        aoahid_gamepad_options gamepad;
        aoahid_pen_options pen;
        aoahid_accessory_options accessory;
        aoahid_gamepad_options_init(&gamepad);
        aoahid_pen_options_init(&pen);
        aoahid_accessory_options_init(&accessory);
        for (size_t i = 0; i < gamepad.axis_count; ++i) {
            const aoahid_gamepad_axis_option* axis = &gamepad.axes[i];
            printf("gamepad_axis%zu=%d,%u,%u,%d,%d,%u,%d,%s,%s\n", i, (int)axis->role,
                   (unsigned)axis->usage_page, (unsigned)axis->usage,
                   (int)axis->value.logical_minimum, (int)axis->value.logical_maximum,
                   (unsigned)axis->value.bit_width, (int)axis->neutral_value,
                   axis->expected_linux_code, axis->expected_android_axis);
        }
        printf("pen_barrel=%zu,%u\n", pen.barrel_usage_count, (unsigned)pen.barrel_usages[0]);
        printf("accessory=%s,%s\n", accessory.strings.manufacturer, accessory.strings.model);
    }
    return 0;
}
