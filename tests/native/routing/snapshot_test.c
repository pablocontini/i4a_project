#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "routing/routing.h"

#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static bool lock_busy;
static unsigned locks_held, lines_count, panic_count;
static char lines[64][220];

bool mutex_try_lock(mutex_t *lock) {
    (void)lock;
    if (lock_busy) return false;
    ++locks_held;
    return true;
}
bool mutex_unlock(mutex_t *lock) { (void)lock; --locks_held; return true; }
void os_diag(const char *tag, const char *fmt, ...) {
    (void)tag;
    if (locks_held) ++panic_count; /* Formatting/enqueue must occur unlocked. */
    va_list args;
    va_start(args, fmt);
    if (lines_count < 64) vsnprintf(lines[lines_count++], sizeof(lines[0]), fmt, args);
    va_end(args);
}
void os_panic(const char *fmt, ...) { (void)fmt; ++panic_count; }
void os_log(os_log_level_t level, const char *tag, const char *fmt, ...) {
    (void)level; (void)tag; (void)fmt;
}
unsigned mask_size(uint32_t value) {
    unsigned bits = 0;
    while (value) { bits += value & 1; value >>= 1; }
    return bits;
}
static void reset(void) {
    lock_busy = false; locks_held = lines_count = panic_count = 0;
    memset(lines, 0, sizeof(lines));
}
static bool contains(const char *text) {
    for (unsigned i = 0; i < lines_count; ++i)
        if (strstr(lines[i], text)) return true;
    return false;
}

static int test_periodic_snapshot_keeps_work_and_table_unchanged(void) {
    reset();
    routing_t router = {0};
    router.orientation = ORIENTATION_SOUTH;
    router.token_waiting = true;
    router.external_queue.count = 2;
    router.internal_queue.count = 1;
    router.pending_tick_ms = 1000;
    routing_table_init(&router.node_state.routing_table, ORIENTATION_WEST);
    routing_table_add(&router.node_state.routing_table, 0x0A940000, 0xFFFC0000, ORIENTATION_CENTER);
    rt_routing_table_t before = router.node_state.routing_table;
    rt_diagnostics_tick(&router, 4999);
    CHECK(!lines_count);
    rt_diagnostics_tick(&router, 1);
    CHECK(contains("event=route_snapshot orientation=2 default=4 count=1"));
    CHECK(contains("rpi_out=4 internet_out=4"));
    CHECK(contains("peer=2 sibling=1 waiting=1"));
    CHECK(contains("network=0a940000 mask=fffc0000 output=5"));
    CHECK(!memcmp(&before, &router.node_state.routing_table, sizeof(before)));
    CHECK(router.external_queue.count == 2 && router.internal_queue.count == 1);
    CHECK(router.token_waiting && router.pending_tick_ms == 1000);
    CHECK(!locks_held && !panic_count);
    return 0;
}

static int test_busy_mutex_skips_without_waiting_or_reading(void) {
    reset();
    routing_t router = {0};
    router.orientation = ORIENTATION_NORTH;
    lock_busy = true;
    rt_diagnostics_tick(&router, 5000);
    CHECK(lines_count == 1 && contains("event=snapshot_busy orientation=1"));
    CHECK(!contains("event=route_snapshot"));
    CHECK(!locks_held && !panic_count);
    return 0;
}

static int test_late_tick_has_one_snapshot_and_invalid_count_is_safe(void) {
    reset();
    routing_t router = {0};
    router.orientation = ORIENTATION_EAST;
    routing_table_init(&router.node_state.routing_table, ROUTING_OUTPUT_NONE);
    rt_diagnostics_tick(&router, UINT32_MAX);
    CHECK(lines_count == 2);
    CHECK(contains("default=255 count=0") && contains("rpi_out=255 internet_out=255"));
    rt_diagnostics_tick(&router, 1);
    CHECK(lines_count == 2);
    router.node_state.routing_table.count = MAX_ROUTING_ENTRIES + 1;
    rt_diagnostics_tick(&router, 4999);
    CHECK(lines_count == 3 && contains("event=snapshot_invalid"));
    CHECK(!locks_held && !panic_count);
    return 0;
}

#define RUN(test) do { int line = test(); if (line) { printf("%s: FAIL line %d\n", #test, line); return 1; } printf("%s: OK\n", #test); } while (0)
int main(void) {
    RUN(test_periodic_snapshot_keeps_work_and_table_unchanged);
    RUN(test_busy_mutex_skips_without_waiting_or_reading);
    RUN(test_late_tick_has_one_snapshot_and_invalid_count_is_safe);
    return 0;
}
