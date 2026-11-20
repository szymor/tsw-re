#include "ui.h"

#include "itext.h"

#include <stddef.h>

void ui_init(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_GAME;
    ui->menu_tab = UI_TAB_GAME;
    ui->menu_selected = 0;
    ui->slot_selected = 0;
    ui->load_checkpoint = false;
    ui->overwrite_slot = -1;
    ui->inventory_selected = 0;
    ui->new_confirm_back = UI_VIEW_GAME;
}

void ui_toggle_menu(UiState *ui)
{
    if (!ui) return;
    if (ui->view == UI_VIEW_GAME) {
        ui->view = UI_VIEW_MENU;
        ui->menu_tab = UI_TAB_GAME;
        ui->menu_selected = 0;
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
    case UI_VIEW_INVENTORY:
    case UI_VIEW_FORECAST:
        ui->view = UI_VIEW_GAME;
        break;
    case UI_VIEW_OVERWRITE:
        ui->view = UI_VIEW_SAVE;
        ui->overwrite_slot = -1;
        break;
    case UI_VIEW_SAVE:
    case UI_VIEW_LOAD:
    case UI_VIEW_CONTROLS:
    case UI_VIEW_ABOUT:
        ui->view = UI_VIEW_MENU;     /* the menu keeps its tab and row */
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

void ui_open_load(UiState *ui, bool checkpoint, int slot)
{
    if (!ui) return;
    ui->view = UI_VIEW_LOAD;
    ui->load_checkpoint = checkpoint;
    int row = checkpoint ? slot : slot - 1;
    ui->slot_selected = row >= 0 && row < ui_load_rows(ui) ? row : 0;
    ui->overwrite_slot = -1;
}

void ui_begin_overwrite(UiState *ui, int slot)
{
    if (!ui) return;
    ui->view = UI_VIEW_OVERWRITE;
    ui->overwrite_slot = slot;
    ui->confirm_yes = false;
}

void ui_open_new_confirm(UiState *ui)
{
    if (!ui || ui->view == UI_VIEW_NEW_CONFIRM)
        return;
    if (ui->view == UI_VIEW_GAME)
        ui->new_confirm_back = UI_VIEW_GAME;
    else
        ui->new_confirm_back = UI_VIEW_MENU;
    ui->view = UI_VIEW_NEW_CONFIRM;
    ui->confirm_yes = false;
}

void ui_open_inventory(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_INVENTORY;
    ui->inventory_selected = 0;
}

void ui_open_forecast(UiState *ui)
{
    if (!ui) return;
    ui->view = UI_VIEW_FORECAST;
}

void ui_open_view(UiState *ui, UiView view)
{
    if (!ui) return;
    ui->view = view;
}

void ui_inventory_move(UiState *ui, int dx, int dy)
{
    if (!ui || ui->view != UI_VIEW_INVENTORY)
        return;
    int col = ui->inventory_selected % 3;
    int row = ui->inventory_selected / 3;
    col = (col + dx + 3) % 3;
    row = (row + dy + 5) % 5;
    ui->inventory_selected = row * 3 + col;
}

int ui_tab_count(int tab)
{
    switch (tab) {
    case UI_TAB_GAME: return UI_GAME_COUNT;
    case UI_TAB_OPTIONS: return UI_OPT_COUNT;
    case UI_TAB_HELP: return UI_HELP_COUNT;
    default: return 0;
    }
}

int ui_load_rows(const UiState *ui)
{
    return UI_SLOT_COUNT + (ui && ui->load_checkpoint ? 1 : 0);
}

int ui_load_row_slot(const UiState *ui, int row)
{
    return ui && ui->load_checkpoint ? row : row + 1;
}

static void wrap(int *v, int count)
{
    if (count <= 0) {
        *v = 0;
        return;
    }
    *v %= count;
    if (*v < 0)
        *v += count;
}

void ui_move_selection(UiState *ui, int delta)
{
    if (!ui) return;
    if (ui->view == UI_VIEW_MENU) {
        ui->menu_selected += delta;
        wrap(&ui->menu_selected, ui_tab_count(ui->menu_tab));
    } else if (ui->view == UI_VIEW_SAVE) {
        ui->slot_selected += delta;
        wrap(&ui->slot_selected, UI_SLOT_COUNT);
    } else if (ui->view == UI_VIEW_LOAD) {
        ui->slot_selected += delta;
        wrap(&ui->slot_selected, ui_load_rows(ui));
    }
}

void ui_move_tab(UiState *ui, int delta)
{
    if (!ui || ui->view != UI_VIEW_MENU)
        return;
    ui->menu_tab += delta;
    wrap(&ui->menu_tab, UI_TAB_COUNT);
    ui->menu_selected = 0;
}

const char *ui_tab_label(int tab)
{
    switch (tab) {
    case UI_TAB_GAME: return tx(ITX_MENU_TAB_GAME);
    case UI_TAB_OPTIONS: return tx(ITX_MENU_TAB_OPTIONS);
    case UI_TAB_HELP: return tx(ITX_MENU_TAB_HELP);
    default: return "";
    }
}

const char *ui_menu_label(int tab, int index)
{
    static const ITextId GAME[UI_GAME_COUNT] = {
        ITX_MENU_NEW_GAME, ITX_MENU_LOAD, ITX_MENU_SAVE, ITX_MENU_RANKING,
        ITX_MENU_QUIT
    };
    static const ITextId OPTS[UI_OPT_COUNT] = {
        ITX_MENU_SOUND, ITX_MENU_MUSIC, ITX_MENU_SPEED, ITX_MENU_TEXT_SIZE,
        ITX_MENU_LANGUAGE
    };
    static const ITextId HELP[UI_HELP_COUNT] = {
        ITX_MENU_CONTROLS, ITX_MENU_ABOUT
    };
    if (index < 0 || index >= ui_tab_count(tab))
        return "";
    return tx(tab == UI_TAB_GAME ? GAME[index]
              : tab == UI_TAB_OPTIONS ? OPTS[index] : HELP[index]);
}

const char *ui_view_title(UiView view)
{
    switch (view) {
    case UI_VIEW_MENU: return tx(ITX_TITLE_MENU);
    case UI_VIEW_SAVE: return tx(ITX_TITLE_SAVE);
    case UI_VIEW_LOAD: return tx(ITX_TITLE_LOAD);
    case UI_VIEW_OVERWRITE: return tx(ITX_TITLE_OVERWRITE);
    case UI_VIEW_NEW_CONFIRM: return tx(ITX_TITLE_NEW_GAME);
    case UI_VIEW_INVENTORY: return tx(ITX_TITLE_INVENTORY);
    case UI_VIEW_FORECAST: return tx(ITX_TITLE_FORECAST);
    case UI_VIEW_CONTROLS: return tx(ITX_TITLE_CONTROLS);
    case UI_VIEW_ABOUT: return tx(ITX_TITLE_ABOUT);
    default: return "";
    }
}
