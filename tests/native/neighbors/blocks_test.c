#include "neighbor_manager/blocks.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static const nm_network_t root = {0x0A000000, 0xFF000000}, empty = {0};

EXPORT int test_blocks_identity_survives_mobility_and_new_neighbor(void) {
    nm_blocks_t book;
    nm_blocks_init(&book, "000000000000");
    nm_network_t grillo, moved, ramona;
    CHECK(nm_blocks_offer(&book, "142B2FC05E7D", root, empty, 3, &grillo));
    CHECK(grillo.addr == 0x0A600000 && grillo.mask == 0xFFE00000);
    nm_blocks_t before = book;
    CHECK(nm_blocks_offer(&book, "142B2FC05E7D", root, grillo, 2, &moved));
    CHECK(!memcmp(&moved, &grillo, sizeof(moved)) && !memcmp(&book, &before, sizeof(book)));
    CHECK(nm_blocks_offer(&book, "20E7C85AA245", root, empty, 3, &ramona));
    CHECK(ramona.addr == 0x0A200000 && ramona.mask == grillo.mask);
    CHECK(nm_blocks_offer(&book, "142B2FC05E7D", root, empty, 4, &moved));
    CHECK(!memcmp(&moved, &grillo, sizeof(moved)));
    CHECK(nm_blocks_valid(&book, "000000000000"));
    return 0;
}

EXPORT int test_blocks_conflicting_owner_and_invalid_networks_are_rejected(void) {
    nm_blocks_t book;
    nm_blocks_init(&book, "000000000000");
    nm_network_t offer;
    CHECK(nm_blocks_offer(&book, "A00000000000", root, empty, 3, &offer));
    nm_network_t allocated = offer;
    nm_blocks_t before = book;
    CHECK(!nm_blocks_offer(&book, "B00000000000", root, allocated, 3, &offer));
    CHECK(!offer.addr && !offer.mask && !memcmp(&book, &before, sizeof(book)));
    CHECK(!nm_blocks_offer(&book, "B00000000000", root, root, 3, &offer));
    nm_network_t bad[] = {{0, 0xFF000000}, {0x0A000001, 0xFF000000},
        {0x0A000000, 0xFF00FF00}, {0x0A000000, 0xFFFFFFF0}, {0x0A000000, 0}};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        CHECK(!nm_blocks_offer(&book, "B00000000000", root, bad[i], 3, &offer));
        CHECK(!nm_blocks_offer(&book, "B00000000000", bad[i], empty, 3, &offer));
    }
    CHECK(!nm_blocks_offer(&book, "000000000000", root, empty, 3, &offer));
    CHECK(!nm_blocks_offer(&book, "B00000000000", root, empty, 5, &offer));
    CHECK(!memcmp(&book, &before, sizeof(book)));
    return 0;
}

EXPORT int test_blocks_imports_existing_owner_and_preserves_descendant_routes(void) {
    nm_blocks_t book;
    nm_blocks_init(&book, "000000000000");
    nm_network_t offer, grillo = {0x0A600000, 0xFFE00000};
    CHECK(nm_blocks_offer(&book, "A00000000000", root, grillo, 2, &offer));
    CHECK(book.leases[0].network.addr == grillo.addr);
    nm_blocks_t before = book;
    nm_network_t descendant = {0x0A640000, 0xFFFC0000};
    CHECK(nm_blocks_offer(&book, "B00000000000", root, descendant, 4, &offer));
    CHECK(!memcmp(&book, &before, sizeof(book)) && offer.addr == descendant.addr);
    CHECK(nm_blocks_offer(&book, "C00000000000", root, empty, 3, &offer));
    CHECK(offer.addr != grillo.addr);
    return 0;
}

EXPORT int test_blocks_exhaustion_never_reuses_a_live_owner_or_client_sector(void) {
    nm_blocks_t book;
    nm_blocks_init(&book, "000000000000");
    nm_network_t offers[6], offer;
    char uuid[] = "A00000000000";
    for (unsigned i=0; i<6; ++i) {
        uuid[11] = '0' + i;
        CHECK(nm_blocks_offer(&book, uuid, root, empty, 3, &offers[i]));
        CHECK(offers[i].addr != 0x0AA00000 && offers[i].addr != 0x0A000000);
        for (unsigned j=0; j<i; ++j) CHECK(offers[i].addr != offers[j].addr);
    }
    nm_blocks_t before = book;
    CHECK(!nm_blocks_offer(&book, "B00000000000", root, empty, 1, &offer));
    CHECK(!memcmp(&book, &before, sizeof(book)));
    CHECK(nm_blocks_offer(&book, "A00000000000", root, empty, 4, &offer));
    CHECK(offer.addr == offers[0].addr);
    nm_blocks_init(&book, "000000000000");
    for (unsigned i=0; i<NM_BLOCK_LEASES; ++i) {
        book.leases[i].network = (nm_network_t){0x0B000000+(i<<8), 0xFFFFFF00};
        memcpy(book.leases[i].uuid, "A00000000000", NM_UUID_LEN+1);
    }
    CHECK(nm_blocks_valid(&book, "000000000000"));
    CHECK(!nm_blocks_offer(&book, "B00000000000", root, empty, 1, &offer));
    return 0;
}

EXPORT int test_blocks_changed_parent_cannot_delegate_its_own_network_or_clients(void) {
    nm_blocks_t book;
    nm_blocks_init(&book,"000000000000");
    nm_network_t offer, child;
    CHECK(nm_blocks_offer(&book,"A00000000000",root,empty,3,&child));
    nm_blocks_t before=book;
    CHECK(!nm_blocks_offer(&book,"A00000000000",child,empty,1,&offer));
    nm_network_t new_parent={0x0A400000,0xFFC00000};
    CHECK(!nm_blocks_offer(&book,"A00000000000",new_parent,empty,1,&offer));
    nm_network_t forbidden[]={{0x0AA40000,0xFFFC0000},{0x0A800000,0xFF800000}};
    for (unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i)
        CHECK(!nm_blocks_offer(&book,"B00000000000",root,forbidden[i],1,&offer));
    CHECK(!memcmp(&book,&before,sizeof(book)));
    return 0;
}
