#include "items.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    /* Map pickups are handan builders and item use runs img*work /
     * Button38Click (test_event_builders, test_event_engine).  This file
     * covers the inventory's names and icons. */
    CHECK(strcmp(special_item_name(0), "Orb of the Hero") == 0);
    CHECK(strcmp(special_item_name(11), "Snow Crystal") == 0);
    CHECK(strcmp(special_item_name(15), "") == 0);
    CHECK(strcmp(special_item_name(-1), "") == 0);
    CHECK(special_item_icon(0) == 0x23 && special_item_icon(14) == 0x31);
    CHECK(strcmp(sword_name(5), "Sacred Sword") == 0);
    CHECK(strcmp(shield_name(0), "-") == 0);
    printf("test_items: ok\n");
    return 0;
}
