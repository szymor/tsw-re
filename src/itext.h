#ifndef TSW_ITEXT_H
#define TSW_ITEXT_H

/* The remake's own interface text: menus, window titles, hints, item
 * names, the HUD and the console's log lines.  The game's original text
 * stays in messages.tsv / ui.tsv (messages.h); this is everything the
 * remake added around it.  English is built in below and
 * data/text/<lang>/interface.tsv ("<key>\t<text>", messages.tsv's
 * escapes) replaces it entry by entry.  An entry with printf
 * conversions (%d, %s) must keep them, in the same order, or it is
 * rejected and the English one stays.  SDL-free. */

#include <stdbool.h>
#include <stddef.h>

/* X(id, key, English) */
#define ITEXT_LIST(X) \
    /* The menu bar, its entries and option values. */                     \
    X(MENU_TAB_GAME,      "menu.tab.game",      "Game")                     \
    X(MENU_TAB_OPTIONS,   "menu.tab.options",   "Options")                  \
    X(MENU_TAB_HELP,      "menu.tab.help",      "Help")                     \
    X(MENU_NEW_GAME,      "menu.new_game",      "New game")                 \
    X(MENU_LOAD,          "menu.load",          "Load")                     \
    X(MENU_SAVE,          "menu.save",          "Save")                     \
    X(MENU_RANKING,       "menu.ranking",       "Ranking")                  \
    X(MENU_QUIT,          "menu.quit",          "Quit")                     \
    X(MENU_SOUND,         "menu.sound",         "Sound effects")            \
    X(MENU_MUSIC,         "menu.music",         "Music")                    \
    X(MENU_SPEED,         "menu.speed",         "Speed")                    \
    X(MENU_TEXT_SIZE,     "menu.text_size",     "Message text")             \
    X(MENU_LANGUAGE,      "menu.language",      "Language")                 \
    X(LANGUAGE_NAME,      "language.name",      "English")                  \
    X(MENU_CONTROLS,      "menu.controls",      "Controls")                 \
    X(MENU_ABOUT,         "menu.about",         "About")                    \
    X(VALUE_ON,           "value.on",           "On")                       \
    X(VALUE_OFF,          "value.off",          "Off")                      \
    X(SPEED_HIGH,         "speed.high",         "High")                     \
    X(SPEED_MIDDLE,       "speed.middle",       "Middle")                   \
    X(SPEED_LOW,          "speed.low",          "Low")                      \
    X(TEXT_TINY,          "text_size.tiny",     "Tiny")                     \
    X(TEXT_SMALL,         "text_size.small",    "Small")                    \
    X(TEXT_NORMAL,        "text_size.normal",   "Normal")                   \
    X(TEXT_BIG,           "text_size.big",      "Big")                      \
    /* Window titles. */                                                    \
    X(TITLE_MENU,         "title.menu",         "Menu")                     \
    X(TITLE_SAVE,         "title.save",         "Save")                     \
    X(TITLE_LOAD,         "title.load",         "Load")                     \
    X(TITLE_OVERWRITE,    "title.overwrite",    "Overwrite?")               \
    X(TITLE_NEW_GAME,     "title.new_game",     "New game?")                \
    X(TITLE_INVENTORY,    "title.inventory",    "Inventory")                \
    X(TITLE_GAME_OVER,    "title.game_over",    "Game Over")                \
    X(TITLE_FORECAST,     "title.forecast",     "Monster Forecast")         \
    X(TITLE_CONTROLS,     "title.controls",     "Controls")                 \
    X(TITLE_ABOUT,        "title.about",        "About")                    \
    X(TITLE_INFORMATION,  "title.information",  "Information")              \
    /* Answers and key hints. */                                            \
    X(ANSWER_YES,         "answer.yes",         "Yes")                      \
    X(ANSWER_NO,          "answer.no",          "No")                       \
    X(ANSWER_OK,          "answer.ok",          "OK")                       \
    X(ANSWER_USE,         "answer.use",         "Use")                      \
    X(ANSWER_DONT_USE,    "answer.dont_use",    "Don't use")                \
    /* The keys that answer Yes / No: one letter or digit each. */          \
    X(KEY_YES,            "key.yes",            "y")                        \
    X(KEY_NO,             "key.no",             "n")                        \
    X(HINT_ENTER,         "hint.enter",         "Enter")                    \
    X(HINT_YES_NO,        "hint.yes_no",                                    \
      "\xe2\x86\x90\xe2\x86\x92 Enter   Y/N")                               \
    X(HINT_USE,           "hint.use",           "Enter   Y/N")              \
    X(HINT_FLY,           "hint.fly",                                       \
      "\xe2\x86\x91\xe2\x86\x93  Enter: land")                              \
    X(HINT_WISDOM,        "hint.wisdom",                                    \
      "\xe2\x86\x91\xe2\x86\x93  Enter: close")                             \
    X(HINT_RANKING,       "hint.ranking",                                   \
      "\xe2\x86\x90\xe2\x86\x92 area     Enter: close")                     \
    X(HINT_NAME,          "hint.name",          "Enter: OK     Esc: cancel")\
    X(HINT_CLOSE,         "hint.close",         "Enter: close")             \
    X(HINT_MENU_OPTIONS,  "hint.menu_options",                              \
      "\xe2\x86\x90\xe2\x86\x92 menus   \xe2\x86\x91\xe2\x86\x93   "         \
      "Enter: change   Esc: close")                                         \
    X(HINT_MENU,          "hint.menu",                                      \
      "\xe2\x86\x90\xe2\x86\x92 menus   \xe2\x86\x91\xe2\x86\x93   "         \
      "Enter   Esc: close")                                                 \
    X(HINT_SLOTS,         "hint.slots",                                     \
      "\xe2\x86\x91\xe2\x86\x93   Enter   Esc: back")                       \
    X(HINT_BACK,          "hint.back",          "Enter / Esc: back")        \
    X(HINT_INVENTORY,     "hint.inventory",     "Enter use   Esc close")    \
    X(HINT_GAME_OVER,     "hint.game_over",     "Enter new game   Esc quit")\
    /* The windows' own words. */                                           \
    X(FLY_FLOOR,          "fly.floor",          "Floor %d")                 \
    X(WISDOM_NONE,        "wisdom.none",        "No words heard yet.")      \
    /* The Orb of Wisdom's entries: syokidata0's hints, by floor. */        \
    X(WISDOM_OLD_MAN,     "wisdom.old_man",     "Old Man of %dF")           \
    X(WISDOM_MERCHANT,    "wisdom.merchant",    "Merchant of %dF")          \
    X(RANKING_TITLE,      "ranking.title",      "Top 10 ranking")           \
    X(RANKING_RANK,       "ranking.rank",       "Rank")                     \
    X(RANKING_POINTS,     "ranking.points",     "Points")                   \
    X(RANKING_NAME,       "ranking.name",       "Name")                     \
    X(RANKING_LIFE,       "ranking.life",       "Life")                     \
    X(RANKING_OFFENSE,    "ranking.offense",    "Offense")                  \
    X(RANKING_DEFENSE,    "ranking.defense",    "Defense")                  \
    X(SLOT_CHECKPOINT,    "slot.checkpoint",    "Checkpoint (fairy)")       \
    X(SLOT_NUMBER,        "slot.number",        "Slot %d")                  \
    X(SLOT_EMPTY,         "slot.empty",         "empty")                    \
    X(SLOT_INFO,          "slot.info",          "Floor %d   Life %d")       \
    X(CONFIRM_OVERWRITE,  "confirm.overwrite",                              \
      "Slot %d holds a game. Overwrite it?")                                \
    X(CONFIRM_NEW_GAME,   "confirm.new_game",                               \
      "Start a new game? This game will be lost.")                          \
    X(INVENTORY_EMPTY,    "inventory.empty",    "Empty")                    \
    X(GAME_OVER_TEXT,     "game_over.text",     "You died.")                \
    X(HUD_FLOOR,          "hud.floor",          "FLOOR %d")                 \
    X(HUD_LEVEL_1,        "hud.level1",         "Iron")                     \
    X(HUD_LEVEL_2,        "hud.level2",         "Silver")                   \
    X(HUD_LEVEL_3,        "hud.level3",         "Knight")                   \
    X(HUD_LEVEL_4,        "hud.level4",         "Holy")                     \
    X(HUD_LEVEL_5,        "hud.level5",         "Sacred")                   \
    /* Help > Controls: "<keys>\t<what they do>". */                        \
    X(CONTROLS_1,         "controls.1",         "Arrows\tmove; choose")     \
    X(CONTROLS_2,         "controls.2",         "Enter\tthe items; confirm")\
    X(CONTROLS_3,         "controls.3",                                     \
      "Esc\tthe menu; close, cancel")                                       \
    X(CONTROLS_4,         "controls.4",                                     \
      "Left / Right\tanswers, menus, areas")                                \
    X(CONTROLS_5,         "controls.5",         "Y / N\tanswer a question") \
    X(CONTROLS_6,         "controls.6",         "1 2 3\tthe altar's offers")\
    X(CONTROLS_7,         "controls.7",         "PgUp / PgDn\tscroll the log")\
    X(CONTROLS_8,         "controls.8",                                     \
      "F11 / Alt+Enter\tfullscreen")                                        \
    /* Help > About (the title and the commit hash are not translated). */  \
    X(ABOUT_COMMIT,       "about.commit",       "commit %s")                \
    X(ABOUT_REMAKE,       "about.remake",                                   \
      "A faithful remake of Tower of the Sorcerer 1.2")                     \
    X(ABOUT_AUTHOR,       "about.author",       "by vamastah, 2026")        \
    X(ABOUT_TEXT_REVIEW,  "about.text_review",                              \
      "English text reviewed by Claude Opus 5.5")                           \
    X(ABOUT_TRANSLATIONS, "about.translations",                             \
      "Translations by Claude Opus 5.5:")                                   \
    X(ABOUT_TRANSLATION_LANGS, "about.translation_langs",                   \
      "Japanese, Polish, Russian, Vietnamese")                              \
    X(ABOUT_ORIGINAL,     "about.original",                                 \
      "Original game: Mah\xc5\x8d no T\xc5\x8d by NAO (PC-98, 1996)")        \
    X(ABOUT_WINDOWS,      "about.windows",                                  \
      "Windows version: Oz & Kenichi")                                      \
    X(ABOUT_TEXT_CHECK,   "about.text_check",                               \
      "Original English text checked by Tween and Miki Olsz")               \
    X(ABOUT_LEGACY_1,     "about.legacy1",                                  \
      "Since then, the tower has inspired thousands")                       \
    X(ABOUT_LEGACY_2,     "about.legacy2",                                  \
      "of fan-made towers around the world.")                               \
    X(ABOUT_FAN_WORKS,    "about.fan_works",                                \
      "NAO kindly allows fan works based on the game.")                     \
    /* Written over the intro's and ending's pictures (pictures.c): a       \
     * verse's lines are one \n apart, its stanzas two. */                  \
    X(PICTURE_INTRO,      "picture.intro_verse",                            \
      "Now is the time to act.\n"                                           \
      "With the power of omniscience\n"                                     \
      "I shall break the sacred sword\n"                                    \
      "and receive great wisdom.")                                          \
    X(PICTURE_ENDING,     "picture.ending_verse",                           \
      "The long war has come to an end,\n"                                  \
      "and the tower of vanity has fallen.\n"                               \
      "\n"                                                                  \
      "The gods have returned to the heavens,\n"                            \
      "the soldiers to the earth.\n"                                        \
      "\n"                                                                  \
      "The present has turned into the past,\n"                             \
      "and this journey into a memory.\n"                                   \
      "\n"                                                                  \
      "You will look back for a moment,\n"                                  \
      "then take a step toward a new journey.")                             \
    X(PICTURE_THE_END,    "picture.the_end",    "The End")                  \
    X(PICTURE_SEE_YOU,    "picture.see_you_again", "See you again!")        \
    /* Items: the special slots 0..14, then swords and shields 1..5. */     \
    X(ITEM_0,             "item.0",             "Orb of the Hero")          \
    X(ITEM_1,             "item.1",             "Orb of Wisdom")            \
    X(ITEM_2,             "item.2",             "Orb of Flying")            \
    X(ITEM_3,             "item.3",             "Cross")                    \
    X(ITEM_4,             "item.4",             "Magic Elixir")             \
    X(ITEM_5,             "item.5",             "Magic Mattock")            \
    X(ITEM_6,             "item.6",             "Ball of Destruction")      \
    X(ITEM_7,             "item.7",             "Warp Staff")               \
    X(ITEM_8,             "item.8",             "Wings to Fly Up")          \
    X(ITEM_9,             "item.9",             "Wings to Fly Down")        \
    X(ITEM_10,            "item.10",            "Dragon Slayer")            \
    X(ITEM_11,            "item.11",            "Snow Crystal")             \
    X(ITEM_12,            "item.12",            "Magic Key")                \
    X(ITEM_13,            "item.13",            "Super Magic Mattock")      \
    X(ITEM_14,            "item.14",            "Lucky Gold")               \
    X(SWORD_1,            "sword.1",            "Iron Sword")               \
    X(SWORD_2,            "sword.2",            "Silver Sword")             \
    X(SWORD_3,            "sword.3",            "Knight's Sword")           \
    X(SWORD_4,            "sword.4",            "Holy Sword")               \
    X(SWORD_5,            "sword.5",            "Sacred Sword")             \
    X(SHIELD_1,           "shield.1",           "Iron Shield")              \
    X(SHIELD_2,           "shield.2",           "Silver Shield")            \
    X(SHIELD_3,           "shield.3",           "Knight's Shield")          \
    X(SHIELD_4,           "shield.4",           "Holy Shield")              \
    X(SHIELD_5,           "shield.5",           "Sacred Shield")            \
    /* The console's log lines. */                                          \
    X(LOG_HELP,           "log.help",           "Arrows move; Enter items; Esc menu") \
    X(LOG_NEW_GAME,       "log.new_game",                                   \
      "New game: floor 1, life %d, offense %d, defense %d")                 \
    X(LOG_TRY_AGAIN,      "log.try_again",      "Try again: floor 1, life %d") \
    X(LOG_SAVE_FILE,      "log.save_file",      "Save: %s (%s)")            \
    X(LOG_SAVE_STATS,     "log.save_stats",                                 \
      "Floor %d, life %d, offense %d, defense %d, Gold %d")                 \
    X(LOG_SAVE_REJECTED,  "log.save_rejected",  "Save rejected: %s")        \
    X(LOG_LOADED_FLOOR,   "log.loaded_floor",   "Loaded floor %d from the save.") \
    X(LOG_SAVED_SLOT,     "log.saved_slot",     "Saved the game to slot %d (%s).") \
    X(LOG_LOADED_SLOT,    "log.loaded_slot",                                \
      "Loaded slot %d (%s): floor %d, life %d")                             \
    X(LOG_CHECKPOINT,     "log.checkpoint",     "Checkpoint saved to %s.")  \
    X(LOG_SAVE_FAILED,    "log.save_failed",    "Saving to slot %d failed: %s") \
    X(LOG_LOAD_FAILED,    "log.load_failed",    "Loading slot %d failed: %s") \
    X(LOG_NEW_GAME_FAILED, "log.new_game_failed", "New game failed: %s")    \
    X(LOG_OPTIONS_FAILED, "log.options_failed", "Cannot write %s")          \
    X(LOG_ITEM_PASSIVE,   "log.item_passive",   "The %s works by itself.")  \
    X(LOG_INVENTORY,      "log.inventory",      "Inventory opened.")

typedef enum {
#define ITEXT_ENUM(id, key, en) ITX_##id,
    ITEXT_LIST(ITEXT_ENUM)
#undef ITEXT_ENUM
    ITX_COUNT
} ITextId;

/* The entry's text: the loaded one, else the English. */
const char *tx(ITextId id);
/* The entry's key ("menu.new_game") and English text. */
const char *itext_key(ITextId id);
const char *itext_english(ITextId id);
/* Back to the built-in English. */
void itext_reset(void);
/* Load "<key>\t<text>" lines over the current text.  False when the file
 * is missing, or has an unknown key, a malformed line or an entry whose
 * conversions differ from the English (that entry keeps its old text);
 * `err` names the first problem. */
bool itext_load(const char *path, char *err, size_t errsz);
/* True when `text` has the same printf conversions as `english`, in the
 * same order (what itext_load checks). */
bool itext_same_conversions(const char *text, const char *english);
/* The key a key.* entry names, as an SDL keycode (lower-case ASCII). */
int itext_keycode(ITextId id);

#endif
