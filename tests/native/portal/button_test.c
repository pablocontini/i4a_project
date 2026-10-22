#include <stdio.h>
#include "../../../components/integration/config_portal/src/boot_button.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static int debounce_and_short_release(void) {
    boot_button_t b;
    boot_button_init(&b, false, 0);
    CHECK(boot_button_sample(&b, true, 1000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 50000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 300000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 400000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 549999) == BOOT_BUTTON_NONE);
    CHECK(!b.press_active);
    CHECK(boot_button_sample(&b, true, 550000) == BOOT_BUTTON_NONE && b.press_active);
    CHECK(boot_button_sample(&b, false, 700000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 849999) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 850000) == BOOT_BUTTON_SHORT_RELEASE);
    CHECK(boot_button_sample(&b, false, 1000000) == BOOT_BUTTON_NONE);
    return 0;
}

static int long_hold_resets_once_and_release_is_distinct(void) {
    boot_button_t b;
    boot_button_init(&b, true, 0);
    CHECK(boot_button_sample(&b, true, 150000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 6149999) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 6150000) == BOOT_BUTTON_FACTORY_RESET);
    CHECK(boot_button_sample(&b, true, 9000000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 9100000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 9250000) == BOOT_BUTTON_FACTORY_RELEASE);
    CHECK(boot_button_sample(&b, false, 9300000) == BOOT_BUTTON_NONE);
    return 0;
}

static int ignored_maintenance_press_never_replays_on_release(void) {
    boot_button_t b;
    boot_button_init(&b, false, 0);
    /* The runtime adapter reinitializes the observer during OTA/boot gates. */
    boot_button_init(&b, true, 1000000);
    boot_button_init(&b, true, 9000000);
    CHECK(boot_button_sample(&b, false, 9010000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 9160000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 9200000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, true, 9350000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 9400000) == BOOT_BUTTON_NONE);
    CHECK(boot_button_sample(&b, false, 9550000) == BOOT_BUTTON_SHORT_RELEASE);
    return 0;
}

#define RUN(f) do { int line = f(); if (line) { printf("%s failed at button_test.c:%d\n", #f, line); return 1; } printf("%s: OK\n", #f); } while (0)
int main(void) {
    RUN(debounce_and_short_release);
    RUN(long_hold_resets_once_and_release_is_distinct);
    RUN(ignored_maintenance_press_never_replays_on_release);
    return 0;
}
