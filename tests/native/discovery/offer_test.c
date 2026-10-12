#include <stdio.h>
#include "../../../components/integration/node/offer_policy.c"
#define CHECK(expr) do { if (!(expr)) { printf("offer_test.c:%d\n", __LINE__); return 1; } } while (0)
int main(void) {
    /* Initial allocation, with no preexisting node addresses. */
    CHECK(node_offer_allowed(0, UINT32_MAX, false, false, 0x0A200000, 0xFFE00000));
    /* A free E device is /14; compare against the node /11, not that device. */
    CHECK(node_offer_allowed(0x0A2C0000, 0xFFFC0000, true, false, 0x0A440000, 0xFFFC0000));
    CHECK(!node_offer_allowed(0x0A2C0000, 0xFFFC0000, true, false, 0x0A100000, 0xFFF00000));
    CHECK(!node_offer_allowed(0x0A2C0000, 0xFFFC0000, true, false, 0x0A400000, 0xFFE00000));
    /* Exact original uplink restores service, without relaxing new-link policy. */
    CHECK(node_offer_allowed(0x0A240000, 0xFFFC0000, true, true, 0x0A200000, 0xFFE00000));
    CHECK(!node_offer_allowed(0x0A240000, 0xFFFC0000, true, false, 0x0A200000, 0xFFE00000));
    CHECK(!node_offer_allowed(0, UINT32_MAX, false, false, 0x0A200001, 0xFFE00000));
    CHECK(!node_offer_allowed(0, UINT32_MAX, false, false, 0x0A200000, 0xFF00FF00));
    CHECK(!node_offer_allowed(0, UINT32_MAX, false, false, 0x0A200000, 0xFFFFFFF8));
    CHECK(!node_offer_allowed(0x0A240000, 0xFF00FF00, true, false, 0x0A440000, 0xFFFC0000));
    puts("test_offered_subnet_policy_and_original_uplink_restoration: OK");
    return 0;
}
