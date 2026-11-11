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
    UI_VIEW_NEW_CONFIRM
} UiView;

enum {
    UI_MENU_CONTINUE = 0,
    UI_MENU_NEW_GAME = 1,
    UI_MENU_SAVE = 2,
    UI_MENU_LOAD = 3,
    UI_MENU_QUIT = 4,
    UI_MENU_COUNT = 5
};

typedef struct {
    UiView view;
    int menu_selected;
    int slot_selected;
    int overwrite_slot;
    UiView new_confirm_back;
} UiState;

void ui_init(UiState *ui);
void ui_toggle_menu(UiState *ui);
void ui_close_menu(UiState *ui);
void ui_back(UiState *ui);
void ui_open_save(UiState *ui);
void ui_open_load(UiState *ui);
void ui_begin_overwrite(UiState *ui, int slot);
void ui_open_new_confirm(UiState *ui);
void ui_move_selection(UiState *ui, int delta);
const char *ui_menu_label(int index);
const char *ui_view_title(UiView view);

#endif
