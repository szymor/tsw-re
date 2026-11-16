#ifndef TSW_UI_H
#define TSW_UI_H

#include <stdbool.h>

#define UI_SLOT_COUNT 8

typedef enum {
    UI_VIEW_GAME = 0,
    UI_VIEW_MENU,
    UI_VIEW_SAVE,
    UI_VIEW_LOAD,
    UI_VIEW_OVERWRITE,
    UI_VIEW_NEW_CONFIRM,
    UI_VIEW_INVENTORY,
    UI_VIEW_GAME_OVER,
    UI_VIEW_FORECAST,
    UI_VIEW_CONTROLS,
    UI_VIEW_ABOUT
} UiView;

/* The menu bar's menus (the original's Game / Options / Help). */
enum { UI_TAB_GAME = 0, UI_TAB_OPTIONS, UI_TAB_HELP, UI_TAB_COUNT };

enum {                              /* Game */
    UI_GAME_NEW = 0,
    UI_GAME_LOAD,
    UI_GAME_SAVE,
    UI_GAME_RANKING,
    UI_GAME_QUIT,
    UI_GAME_COUNT
};
enum {                              /* Options */
    UI_OPT_SOUND = 0,
    UI_OPT_MUSIC,
    UI_OPT_SPEED,
    UI_OPT_TEXT,
    UI_OPT_COUNT
};
enum {                              /* Help */
    UI_HELP_CONTROLS = 0,
    UI_HELP_ABOUT,
    UI_HELP_COUNT
};

typedef struct {
    UiView view;
    int menu_tab;
    int menu_selected;
    /* Load: row 0 is the fairy's checkpoint (save0.dat) when it exists,
     * then slots 1..8; Save: slots 1..8. */
    int slot_selected;
    bool load_checkpoint;
    int overwrite_slot;
    int inventory_selected;
    UiView new_confirm_back;
    /* New game? / Overwrite?: Yes highlighted (opens on No). */
    bool confirm_yes;
} UiState;

void ui_init(UiState *ui);
/* Open the menu bar on the Game menu, or close it. */
void ui_toggle_menu(UiState *ui);
void ui_close_menu(UiState *ui);
void ui_back(UiState *ui);
void ui_open_save(UiState *ui);
void ui_open_load(UiState *ui, bool checkpoint);
void ui_begin_overwrite(UiState *ui, int slot);
void ui_open_new_confirm(UiState *ui);
void ui_open_inventory(UiState *ui);
void ui_open_game_over(UiState *ui);
void ui_open_forecast(UiState *ui);
void ui_open_view(UiState *ui, UiView view);
void ui_inventory_move(UiState *ui, int dx, int dy);
void ui_move_selection(UiState *ui, int delta);
/* Left/Right on the menu bar. */
void ui_move_tab(UiState *ui, int delta);
int ui_tab_count(int tab);
const char *ui_tab_label(int tab);
/* An entry's name (options get their value from the caller). */
const char *ui_menu_label(int tab, int index);
/* Rows of the Load list. */
int ui_load_rows(const UiState *ui);
/* The save slot (0 = the checkpoint, 1..8) a Load row stands for. */
int ui_load_row_slot(const UiState *ui, int row);
const char *ui_view_title(UiView view);

#endif
