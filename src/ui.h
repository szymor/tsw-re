#ifndef TSW_UI_H
#define TSW_UI_H

#include <stdbool.h>

enum {
    UI_MENU_CONTINUE = 0,
    UI_MENU_QUIT = 1,
    UI_MENU_COUNT = 2
};

typedef struct {
    bool menu_open;
    int menu_selected;
} UiState;

void ui_init(UiState *ui);
void ui_toggle_menu(UiState *ui);
void ui_close_menu(UiState *ui);
void ui_move_selection(UiState *ui, int delta);
const char *ui_menu_label(int index);

#endif
