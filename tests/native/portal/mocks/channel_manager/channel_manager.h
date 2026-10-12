#ifndef NATIVE_PORTAL_CHANNEL_MANAGER_CHANNEL_MANAGER_H
#define NATIVE_PORTAL_CHANNEL_MANAGER_CHANNEL_MANAGER_H
#include <stdbool.h>
bool cm_is_blocked_uuid(const char *);
void cm_block_full_ap(const char *);

#endif
