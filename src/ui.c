#include "ui.h"

#include <stddef.h>

void ui_init(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_GAME;
    ui->menu_selected = UI_MENU_CONTINUE;
    ui->slot_selected = 0;
    ui->overwrite_slot = -1;
    ui->new_confirm_back = UI_VIEW_GAME;
}

void ui_toggle_menu(UiState *ui)
{
    if (!ui) return;
    if (ui->view == UI_VIEW_GAME) {
        ui->view = UI_VIEW_MENU;
        ui->menu_selected = UI_MENU_CONTINUE;
        ui->overwrite_slot = -1;
    } else {
        ui->view = UI_VIEW_GAME;
    }
}

void ui_close_menu(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_GAME;
    ui->overwrite_slot = -1;
}

void ui_back(UiState *ui)
{
    if (!ui) return;
    switch (ui->view) {
    case UI_VIEW_NEW_CONFIRM:
        ui->view = ui->new_confirm_back;
        break;
    case UI_VIEW_OVERWRITE:
        ui->view = UI_VIEW_SAVE;
        ui->overwrite_slot = -1;
        break;
    case UI_VIEW_SAVE:
    case UI_VIEW_LOAD:
        ui->view = UI_VIEW_MENU;
        ui->overwrite_slot = -1;
        break;
    case UI_VIEW_MENU:
    case UI_VIEW_GAME:
    default:
        ui->view = UI_VIEW_GAME;
        ui->overwrite_slot = -1;
        break;
    }
}

void ui_open_save(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_SAVE;
    ui->slot_selected = 0;
    ui->overwrite_slot = -1;
}

void ui_open_load(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_LOAD;
    ui->slot_selected = 0;
    ui->overwrite_slot = -1;
}

void ui_begin_overwrite(UiState *ui, int slot)
{
    if (!ui) return;
    ui->view = UI_VIEW_OVERWRITE;
    ui->overwrite_slot = slot;
}

void ui_open_new_confirm(UiState *ui)
{
    if (!ui || ui->view == UI_VIEW_NEW_CONFIRM)
        return;
    ui->new_confirm_back = (ui->view == UI_VIEW_GAME) ? UI_VIEW_GAME
                                                      : UI_VIEW_MENU;
    ui->view = UI_VIEW_NEW_CONFIRM;
}

void ui_move_selection(UiState *ui, int delta)
{
    if (!ui) return;
    int count = 0;
    int *selected = NULL;
    if (ui->view == UI_VIEW_MENU) {
        count = UI_MENU_COUNT;
        selected = &ui->menu_selected;
    } else if (ui->view == UI_VIEW_SAVE || ui->view == UI_VIEW_LOAD) {
        count = UI_SLOT_COUNT;
        selected = &ui->slot_selected;
    }
    if (!selected || count <= 0)
        return;
    *selected += delta;
    while (*selected < 0)
        *selected += count;
    while (*selected >= count)
        *selected -= count;
}

const char *ui_menu_label(int index)
{
    switch (index) {
    case UI_MENU_CONTINUE: return "Continue";
    case UI_MENU_NEW_GAME: return "New Game";
    case UI_MENU_SAVE: return "Save";
    case UI_MENU_LOAD: return "Load";
    case UI_MENU_QUIT: return "Quit";
    default: return "";
    }
}

const char *ui_view_title(UiView view)
{
    switch (view) {
    case UI_VIEW_MENU: return "Menu";
    case UI_VIEW_SAVE: return "Save Game";
    case UI_VIEW_LOAD: return "Load Game";
    case UI_VIEW_OVERWRITE: return "Overwrite?";
    case UI_VIEW_NEW_CONFIRM: return "New Game?";
    default: return "";
    }
}
