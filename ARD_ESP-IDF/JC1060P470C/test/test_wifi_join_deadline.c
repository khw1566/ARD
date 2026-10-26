#include <assert.h>
#include <stdio.h>
#include "../components/ard_net/include/wifi_join_deadline.h"

#define SECOND 1000000LL

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const int64_t start = 100 * SECOND;

    // Reproduce the observed sequence: five seconds to associate. The old
    // fixed 25 s deadline left only 20 s for DHCP and disconnected too soon.
    assert(!ard_join_deadline_expired(start + 25 * SECOND, start, true, start + 5 * SECOND));
    assert(!ard_join_deadline_expired(start + 34 * SECOND, start, true, start + 5 * SECOND));
    assert(ard_join_deadline_expired(start + 35 * SECOND, start, true, start + 5 * SECOND));

    // Even a late association receives the full DHCP budget.
    assert(!ard_join_deadline_expired(start + 53 * SECOND, start, true, start + 24 * SECOND));
    assert(ard_join_deadline_expired(start + 54 * SECOND, start, true, start + 24 * SECOND));

    // Without a radio link, setup remains bounded and does not wait for DHCP.
    assert(!ard_join_deadline_expired(start + 24 * SECOND, start, false, 0));
    assert(ard_join_deadline_expired(start + 25 * SECOND, start, false, 0));

    // A fresh association resets the DHCP phase after a transient disconnect.
    assert(!ard_join_deadline_expired(start + 49 * SECOND, start, true, start + 20 * SECOND));
    assert(ard_join_deadline_expired(start + 50 * SECOND, start, true, start + 20 * SECOND));
    // An actual disconnect at that point ends the no-association fallback.
    assert(ard_join_deadline_expired(start + 49 * SECOND, start, false, start + 20 * SECOND));

    // Repeated association events must not extend the whole operation forever.
    assert(!ard_join_deadline_expired(start + 89 * SECOND, start, true, start + 85 * SECOND));
    assert(ard_join_deadline_expired(start + 90 * SECOND, start, true, start + 85 * SECOND));
    assert(ard_join_deadline_expired(start + 91 * SECOND, start, true, start + 90 * SECOND));
    puts("Wi-Fi manual join deadline regression checks passed.");
    return 0;
}
