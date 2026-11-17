#include "items.h"
#include "itext.h"

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
    CHECK(strcmp(sword_name(3), "Knight's Sword") == 0);
    CHECK(strcmp(special_item_name(6), "Ball of Destruction") == 0);

    /* interface.tsv over the built-in English (itext.h). */
    CHECK(strcmp(tx(ITX_MENU_NEW_GAME), "New game") == 0);
    CHECK(strcmp(itext_key(ITX_MENU_NEW_GAME), "menu.new_game") == 0);
    CHECK(itext_same_conversions("Etage %d", "Floor %d"));
    CHECK(itext_same_conversions("%s %d", "%s and %d"));
    CHECK(!itext_same_conversions("Etage", "Floor %d"));
    CHECK(!itext_same_conversions("%d %s", "%s %d"));
    CHECK(!itext_same_conversions("%n", "%n"));
    CHECK(itext_same_conversions("100%% %d", "%d"));
    FILE *f = fopen("test_items_interface.tsv", "w");
    CHECK(f != NULL);
    fputs("# a comment\n"
          "menu.new_game\tNeues Spiel\n"
          "controls.1\tPfeile\\tgehen\n"
          "fly.floor\tEtage %d\n", f);
    fclose(f);
    char err[256] = "";
    CHECK(itext_load("test_items_interface.tsv", err, sizeof(err)));
    CHECK(strcmp(tx(ITX_MENU_NEW_GAME), "Neues Spiel") == 0);
    CHECK(strcmp(tx(ITX_CONTROLS_1), "Pfeile\tgehen") == 0);
    CHECK(strcmp(tx(ITX_FLY_FLOOR), "Etage %d") == 0);
    CHECK(strcmp(tx(ITX_MENU_LOAD), "Load") == 0);       /* not in the file */
    f = fopen("test_items_interface.tsv", "w");
    CHECK(f != NULL);
    fputs("fly.floor\tEtage\n"                        /* lost its %d */
          "slot.number\tPlatz %s\n"                   /* %d became %s */
          "no.such.key\tx\n", f);
    fclose(f);
    CHECK(!itext_load("test_items_interface.tsv", err, sizeof(err)));
    CHECK(strstr(err, "fly.floor") != NULL);
    CHECK(strcmp(tx(ITX_FLY_FLOOR), "Etage %d") == 0);   /* kept */
    CHECK(strcmp(tx(ITX_SLOT_NUMBER), "Slot %d") == 0);
    CHECK(!itext_load("no-such-interface.tsv", err, sizeof(err)));
    itext_reset();
    CHECK(strcmp(tx(ITX_MENU_NEW_GAME), "New game") == 0);
    remove("test_items_interface.tsv");
    printf("test_items: ok\n");
    return 0;
}
