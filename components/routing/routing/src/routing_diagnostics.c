#include "routing/routing.h"
#include "os/os.h"

#define SNAPSHOT_MS 5000u
#define TABLE_DUMP_MS 30000u

static uint32_t saturating_add(uint32_t value, uint32_t increment) {
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

static uint32_t fingerprint(const rt_routing_table_t *table) {
    /* Hash semantic fields, never padding that can differ across siblings. */
    uint32_t hash = (2166136261u ^ table->default_gateway) * 16777619u;
    hash = (hash ^ (uint32_t)table->count) * 16777619u;
    for (size_t i = 0; i < table->count; ++i) {
        const rt_routing_entry_t *entry = &table->entries[i];
        hash = (hash ^ entry->network) * 16777619u;
        hash = (hash ^ entry->mask) * 16777619u;
        hash = (hash ^ entry->output) * 16777619u;
    }
    return hash;
}

void rt_diagnostics_tick(routing_t *self, uint32_t dt_ms) {
    self->diagnostic_elapsed_ms = saturating_add(self->diagnostic_elapsed_ms, dt_ms);
    self->diagnostic_dump_elapsed_ms = saturating_add(self->diagnostic_dump_elapsed_ms, dt_ms);
    if (self->diagnostic_elapsed_ms < SNAPSHOT_MS) return;
    self->diagnostic_elapsed_ms = 0; /* A late tick never generates a burst. */

    rt_routing_table_t table;
    if (!mutex_try_lock(&self->node_state.m_lock)) {
        os_diag("routing", "event=snapshot_busy orientation=%u", self->orientation);
        return;
    }
    table = self->node_state.routing_table;
    mutex_unlock(&self->node_state.m_lock);
    if (table.count > MAX_ROUTING_ENTRIES) {
        os_diag("routing", "event=snapshot_invalid orientation=%u count=%lu",
                self->orientation, (unsigned long)table.count);
        return;
    }

    os_diag("routing", "event=route_snapshot orientation=%u default=%u count=%lu hash=%08lx rpi_out=%u internet_out=%u",
            self->orientation, table.default_gateway, (unsigned long)table.count,
            (unsigned long)fingerprint(&table),
            routing_table_route(&table, 0x0AFFFFFE),
            routing_table_route(&table, 0x01010101));

    if (mutex_try_lock(&self->queue_lock)) {
        unsigned peer = self->external_queue.count;
        unsigned sibling = self->internal_queue.count;
        bool waiting = self->token_waiting;
        mutex_unlock(&self->queue_lock);
        os_diag("routing", "event=queue_snapshot orientation=%u peer=%u sibling=%u waiting=%u",
                self->orientation, peer, sibling, waiting);
    } else {
        os_diag("routing", "event=queue_snapshot_busy orientation=%u", self->orientation);
    }

    if (!self->diagnostic_dump_started || self->diagnostic_dump_elapsed_ms >= TABLE_DUMP_MS) {
        self->diagnostic_dump_started = true;
        self->diagnostic_dump_elapsed_ms = 0;
        for (size_t i = 0; i < table.count; ++i) {
            const rt_routing_entry_t *entry = &table.entries[i];
            os_diag("routing", "event=route_entry orientation=%u hash=%08lx network=%08lx mask=%08lx output=%u",
                    self->orientation, (unsigned long)fingerprint(&table),
                    (unsigned long)entry->network, (unsigned long)entry->mask,
                    entry->output);
        }
    }
}
