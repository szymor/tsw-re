#include "ui.h"

#include <stddef.h>

void ui_init(UiState *ui)
{
    if (!ui) return;
    ui->menu_open = false;
    ui->menu_selected = UI_MENU_CONTINUE;
}

void ui_toggle_menu(UiState *ui)
{
    if (!ui) return;
    ui->menu_open = !ui->menu_open;
    if (ui->menu_open)
        ui->menu_selected = UI_MENU_CONTINUE;
}

void ui_close_menu(UiState *ui)
{
    if (!ui) return;
    ui->menu_open = false;
}

void ui_move_selection(UiState *ui, int delta)
{
    if (!ui || !ui->menu_open) return;
    ui->menu_selected += delta;
    while (ui->menu_selected < 0)
        ui->menu_selected += UI_MENU_COUNT;
    while (ui->menu_selected >= UI_MENU_COUNT)
        ui->menu_selected -= UI_MENU_COUNT;
}

const char *ui_menu_label(int index)
{
    switch (index) {
    case UI_MENU_CONTINUE: return "Continue";
    case UI_MENU_QUIT: return "Quit";
    default: return "";
    }
}
