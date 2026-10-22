#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOOT_BUTTON_NONE,
    BOOT_BUTTON_SHORT_RELEASE,
    BOOT_BUTTON_FACTORY_RESET,
    BOOT_BUTTON_FACTORY_RELEASE,
} boot_button_event_t;

typedef struct {
    bool raw_pressed, stable_pressed, press_active, factory_reset_handled;
    int64_t raw_changed_at_us, pressed_at_us;
} boot_button_t;

void boot_button_init(boot_button_t *button, bool pressed, int64_t now_us);
boot_button_event_t boot_button_sample(boot_button_t *button, bool pressed, int64_t now_us);
