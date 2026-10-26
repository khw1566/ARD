#pragma once
#include <stdbool.h>
#include <stdint.h>

// Live setup must give DHCP its own budget after association, rather than
// subtracting the time spent finding/authenticating with the access point.
// The hard cap also bounds repeated disconnect/reconnect events.
#define ARD_JOIN_ASSOC_TIMEOUT_US (25 * 1000000LL)
#define ARD_JOIN_DHCP_TIMEOUT_US (30 * 1000000LL)
#define ARD_JOIN_TOTAL_TIMEOUT_US (90 * 1000000LL)

static inline bool ard_join_deadline_expired(int64_t now, int64_t started_at,
                                             bool associated, int64_t associated_at) {
    if (now - started_at >= ARD_JOIN_TOTAL_TIMEOUT_US) return true;
    if (associated) return now - associated_at >= ARD_JOIN_DHCP_TIMEOUT_US;
    return now - started_at >= ARD_JOIN_ASSOC_TIMEOUT_US;
}
