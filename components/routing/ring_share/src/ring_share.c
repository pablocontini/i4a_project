#include "ring_share/ring_share.h"

#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "ring_link_lowlevel.h"

#include "os/os.h"
#include "siblings/siblings.h"

/* The center reboots first; its startup reset must reach peripherals that
 * still hold their selected OTA session and maintenance gate. */
static bool maintenance_control(component_id_t component) {
    return component == RS_NODE_OTA || component == RS_RESET_MANAGER;
}

static bool component_allowed(ring_share_t *self, component_id_t component) {
    bool allowed;
    WITH_LOCK(&self->maintenance_lock, {
        allowed = maintenance_control(component) ||
            (!self->maintenance && (!self->boot_quiet || component != RS_NEIGHBOR_MANAGER));
    });
    return allowed;
}

static void on_sibling_message(void *context, const uint8_t *msg, uint16_t len) {
    ring_share_t *self = context;

    if (len == 0) {
        os_panic("[bug] Got sibling message of length = 0");
        return;
    }

    uint8_t component_id = msg[0];
    if (component_id >= RS_MAX_COMPONENTS) {
        os_panic("[bug] Received a sibling message for a component out of range (%u)", msg[0]);
        return;
    }

    if (!component_allowed(self, component_id)) return;

    ring_callback_t *cb = &self->components[component_id];

    if (cb->callback)
        cb->callback(cb->context, msg + 1, len - 1);
}

bool rs_init(ring_share_t *self, siblings_t *sb) {
    memset(self, 0, sizeof(ring_share_t));

    if (!mutex_create(&self->broadcast_lock)) {
        os_panic("Could not create mutex");
        return false;
    }

    if (!mutex_create(&self->maintenance_lock)) {
        mutex_destroy(&self->broadcast_lock);
        return false;
    }

    self->boot_quiet = true;
    self->paused_since_us = esp_timer_get_time();
    ring_link_lowlevel_set_maintenance(true);
    self->siblings = sb;
    sb_register_callback(self->siblings, on_sibling_message, self);
    return true;
}

void rs_register_component(ring_share_t *self, component_id_t component, ring_callback_t callback) {
    self->components[component] = callback;
}

bool rs_broadcast(ring_share_t *self, component_id_t component, const void *msg, uint16_t len) {
    if (len > RS_MAX_BROADCAST_LEN) {
        os_panic("rs_broadcast message too long (%u)", len);
        return false;
    }

    /* A component callback must not queue behind an OTA broadcast waiting
     * for its ACK: that same callback task delivers the ACK. */
    if (!component_allowed(self, component)) return false;
    bool ret = false;

    WITH_LOCK(&self->broadcast_lock, {
        if (component_allowed(self, component)) {
            self->buffer[0] = component;
            memcpy(self->buffer + 1, msg, len);
            ret = sb_broadcast_to_siblings(self->siblings, self->buffer, len + 1);
        }
    });

    return ret;
}

bool rs_maintenance(ring_share_t *self) {
    bool active;
    WITH_LOCK(&self->maintenance_lock, { active = self->maintenance || self->boot_quiet; });
    return active;
}

uint64_t rs_lease_time_ms(ring_share_t *self) {
    int64_t time;
    WITH_LOCK(&self->maintenance_lock, {
        time = ((self->maintenance || self->boot_quiet) ? self->paused_since_us : esp_timer_get_time()) -
               self->paused_total_us;
    });
    return (uint64_t)time / 1000;
}

bool rs_boot_quiet(ring_share_t *self) {
    bool quiet;
    WITH_LOCK(&self->maintenance_lock, { quiet = self->boot_quiet; });
    return quiet;
}

static void set_gate(ring_share_t *self, bool active, bool boot) {
    WITH_LOCK(&self->broadcast_lock, {
        WITH_LOCK(&self->maintenance_lock, {
            bool before = self->maintenance || self->boot_quiet;
            if (boot) self->boot_quiet = active;
            else self->maintenance = active;
            bool after = self->maintenance || self->boot_quiet;
            int64_t now = esp_timer_get_time();
            if (!before && after) self->paused_since_us = now;
            if (before && !after) self->paused_total_us += now - self->paused_since_us;
        });
        ring_link_lowlevel_set_maintenance(rs_maintenance(self));
    });
}

void rs_set_maintenance(ring_share_t *self, bool active) { set_gate(self, active, false); }
void rs_set_boot_quiet(ring_share_t *self, bool active) { set_gate(self, active, true); }

void rs_shutdown(ring_share_t *self) {
    rs_set_boot_quiet(self, false);
    rs_set_maintenance(self, false);
    mutex_destroy(&self->maintenance_lock);
    mutex_destroy(&self->broadcast_lock);
    memset(self, 0, sizeof(ring_share_t));
}