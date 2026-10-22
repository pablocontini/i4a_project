#include "boot_button.h"

#define DEBOUNCE_US INT64_C(150000)
#define FACTORY_RESET_US INT64_C(6000000)

void boot_button_init(boot_button_t *button, bool pressed, int64_t now_us) {
    *button = (boot_button_t){ .raw_pressed = pressed, .raw_changed_at_us = now_us };
}

boot_button_event_t boot_button_sample(boot_button_t *button, bool pressed, int64_t now_us) {
    if (pressed != button->raw_pressed) {
        button->raw_pressed = pressed;
        button->raw_changed_at_us = now_us;
    }
    if (button->raw_pressed != button->stable_pressed &&
        now_us - button->raw_changed_at_us >= DEBOUNCE_US) {
        button->stable_pressed = button->raw_pressed;
        if (button->stable_pressed) {
            button->press_active = true;
            button->factory_reset_handled = false;
            button->pressed_at_us = now_us;
        } else if (button->press_active) {
            boot_button_event_t event = button->factory_reset_handled
                ? BOOT_BUTTON_FACTORY_RELEASE : BOOT_BUTTON_SHORT_RELEASE;
            button->press_active = false;
            button->factory_reset_handled = false;
            button->pressed_at_us = 0;
            return event;
        }
    }
    if (button->stable_pressed && button->press_active &&
        !button->factory_reset_handled && now_us - button->pressed_at_us >= FACTORY_RESET_US) {
        button->factory_reset_handled = true;
        return BOOT_BUTTON_FACTORY_RESET;
    }
    return BOOT_BUTTON_NONE;
}
