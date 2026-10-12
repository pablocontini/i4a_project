#include <stdio.h>
#include "lowlevel_test.c"
int main(void) {
    int line = test_lowlevel_pauses_ip_and_preserves_internal_forwarding();
    if (line) { printf("maintenance test failed: %d\n", line); return 1; }
    line = test_lowlevel_diagnostics_distinguish_gate_drop_and_tx_failure();
    if (line) { printf("wire/gate diagnostics failed: %d\n", line); return 1; }
    printf("Lowlevel maintenance and wire/gate diagnostics: 2 cases OK\n");
    return 0;
}
