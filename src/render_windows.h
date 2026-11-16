#ifndef TSW_RENDER_WINDOWS_H
#define TSW_RENDER_WINDOWS_H

struct App;

/* The event engine's windows: the message window (RichEdit1 with Button1,
 * Yes/No or the altar's buttons), the item and Orb of Flying buttons, the
 * Orb of Wisdom's buttons, MessageDlg/ShowMessage and Form2. */
void render_windows_draw(struct App *app);
/* The Orb of the Hero's window (img6work). */
void render_hero_orb(struct App *app);

#endif
