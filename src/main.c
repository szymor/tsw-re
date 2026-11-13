#include "app.h"
#include "replay.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef TSW_VERSION
#define TSW_VERSION "0.1.0"
#endif

static bool dir_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool executable_dir(char *out, size_t outsz)
{
#if defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
    if (n <= 0 || (size_t)n >= outsz)
        return false;
    out[n] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash)
        return false;
    *slash = '\0';
    return true;
#else
    (void)out;
    (void)outsz;
    return false;
#endif
}

static const char *resolve_data_dir(const char *requested,
                                    char *buf, size_t bufsz)
{
    if (requested && *requested)
        return requested;
    if (dir_exists("data"))
        return "data";

    char exe[APP_PATH_LEN];
    if (executable_dir(exe, sizeof(exe))) {
        snprintf(buf, bufsz, "%s/../share/tsw", exe);
        if (dir_exists(buf))
            return buf;
        snprintf(buf, bufsz, "%s/data", exe);
        if (dir_exists(buf))
            return buf;
    }

    const char *fallbacks[] = { "../share/tsw", "share/tsw", NULL };
    for (int i = 0; fallbacks[i]; i++) {
        if (dir_exists(fallbacks[i]))
            return fallbacks[i];
    }
    return "data";
}

static const char *resolve_save_dir(const char *requested,
                                    char *buf, size_t bufsz)
{
    if (requested && *requested)
        return requested;

    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/') {
        int n = snprintf(buf, bufsz, "%s/tsw", xdg);
        if (n < 0 || (size_t)n >= bufsz)
            return NULL;
        return buf;
    }
    const char *home = getenv("HOME");
    if (home && home[0] == '/') {
        int n = snprintf(buf, bufsz, "%s/.local/share/tsw", home);
        if (n < 0 || (size_t)n >= bufsz)
            return NULL;
        return buf;
    }
    return "save";
}

static void print_usage(const char *argv0)
{
    printf("Usage: %s [options]\n", argv0);
    printf("  --data-dir <path>  Asset/data directory (auto-detected)\n");
    printf("  --save <path>      Load an original saveN.dat file\n");
    printf("  --save-dir <path>  Save-slot directory (default: XDG data dir)\n");
    printf("  --font <path>      Override the UI font\n");
    printf("  --inspect-save <f> Parse an original save and print its state\n");
    printf("  --replay <script>  Run a deterministic move/assert script\n");
    printf("  --screenshot <png> Render one frame, save it, and exit\n");
    printf("  --menu             Open the F10 menu before the screenshot\n");
    printf("  --menu-view <v>    Debug: open menu, save or load view\n");
    printf("  --inventory        Debug: open the inventory before screenshot\n");
    printf("  --game-over        Debug: show the game-over overlay\n");
    printf("  --no-intro         Skip the open0.bmp launch intro\n");
    printf("  --event <f:i:raw>  Debug: trigger a map event\n");
    printf("  --event-ms <n>     Debug: run the event's timeline n ms first\n");
    printf("  --version          Print version and exit\n");
    printf("  --help             Show this help\n");
    printf("\n");
    printf("Controls: arrows move the debug player, F10 opens the menu,\n");
    printf("Enter is reserved for item use/dialogue confirmation.\n");
}

int main(int argc, char **argv)
{
    const char *data_dir_arg = NULL;
    const char *save_path = NULL;
    const char *save_dir_arg = NULL;
    const char *font_path = NULL;
    const char *screenshot_path = NULL;
    const char *inspect_save = NULL;
    const char *replay_path = NULL;
    const char *menu_view = NULL;
    bool open_menu = false;
    bool open_inventory = false;
    bool show_game_over = false;
    bool no_intro = false;
    const char *event_spec = NULL;
    long event_ms = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir_arg = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save_path = argv[++i];
        } else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) {
            save_dir_arg = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font_path = argv[++i];
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--inspect-save") == 0 && i + 1 < argc) {
            inspect_save = argv[++i];
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (strcmp(argv[i], "--menu") == 0) {
            open_menu = true;
        } else if (strcmp(argv[i], "--menu-view") == 0 && i + 1 < argc) {
            menu_view = argv[++i];
        } else if (strcmp(argv[i], "--inventory") == 0) {
            open_inventory = true;
        } else if (strcmp(argv[i], "--game-over") == 0) {
            show_game_over = true;
        } else if (strcmp(argv[i], "--no-intro") == 0) {
            no_intro = true;
        } else if (strcmp(argv[i], "--event") == 0 && i + 1 < argc) {
            event_spec = argv[++i];
        } else if (strcmp(argv[i], "--event-ms") == 0 && i + 1 < argc) {
            event_ms = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("tsw-re %s\n", TSW_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (inspect_save) {
        SaveData save;
        char err[512] = "";
        if (!save_load_file(inspect_save, &save, err, sizeof(err))) {
            fprintf(stderr, "tsw-re: %s\n", err);
            return 1;
        }
        printf("file: %s\n", inspect_save);
        printf("status: %s\n", state_status_string(save.status));
        printf("floor: %d  position: (%d,%d)\n",
               state_get(&save.state, ST_FLOOR),
               state_get(&save.state, ST_X),
               state_get(&save.state, ST_Y));
        printf("life: %d  attack: %d  defense: %d  gold: %d\n",
               state_get(&save.state, ST_LIFE),
               state_get(&save.state, ST_ATTACK),
               state_get(&save.state, ST_DEFENSE),
               state_get(&save.state, ST_GOLD));
        printf("keys: yellow=%d red=%d blue=%d\n",
               state_get(&save.state, ST_KEY_YELLOW),
               state_get(&save.state, ST_KEY_RED),
               state_get(&save.state, ST_KEY_BLUE));
        int floor = state_get(&save.state, ST_FLOOR);
        printf("stair table floor %d: up=%d down=%d\n", floor,
               map_stair_up(&save.map, floor),
               map_stair_down(&save.map, floor));
        return 0;
    }

    char data_buf[APP_PATH_BUF];
    char save_buf[APP_PATH_BUF];
    const char *data_dir = resolve_data_dir(data_dir_arg, data_buf,
                                            sizeof(data_buf));
    const char *save_dir = resolve_save_dir(save_dir_arg, save_buf,
                                            sizeof(save_buf));
    if (!save_dir) {
        fprintf(stderr, "tsw-re: save directory path is too long\n");
        return 2;
    }

    char err[512] = "";
    if (replay_path) {
        if (!replay_run(data_dir, replay_path, err, sizeof(err))) {
            fprintf(stderr, "tsw-re: %s\n", err);
            return 1;
        }
        return 0;
    }

    App app;
    /* Debug views operate on the playfield; the launch intro would hide
     * them, so skip it unless the caller explicitly wants the intro frame. */
    if (event_spec || open_menu || open_inventory || show_game_over)
        no_intro = true;
    if (!app_init(&app, data_dir, save_path, save_dir, font_path,
                  no_intro, err, sizeof(err))) {
        fprintf(stderr, "tsw-re: %s\n", err);
        return 1;
    }
    if (open_menu)
        ui_toggle_menu(&app.ui);
    if (menu_view) {
        if (strcmp(menu_view, "save") == 0)
            ui_open_save(&app.ui);
        else if (strcmp(menu_view, "load") == 0)
            ui_open_load(&app.ui);
        else
            ui_toggle_menu(&app.ui);
    }
    if (open_inventory)
        ui_open_inventory(&app.ui);
    if (show_game_over)
        ui_open_game_over(&app.ui);
    if (event_spec) {
        int f = 0, idx = 0, raw = 0;
        if (sscanf(event_spec, "%d:%d:%d", &f, &idx, &raw) != 3) {
            fprintf(stderr, "tsw-re: --event expects floor:index:raw\n");
            app_shutdown(&app);
            return 2;
        }
        events_trigger(&app.events, f, idx, (uint8_t)raw);
        /* Debug snapshot: show the post-event tile immediately. */
        app.step_active = false;
        /* --event-ms: let the timeline run that long first. */
        for (long t = 1; t <= event_ms; t++)
            events_update(&app.events, 1000 + (uint64_t)t);
    }

    if (screenshot_path) {
        render_draw(&app);
        if (!render_save_png(&app.render, screenshot_path, err, sizeof(err))) {
            fprintf(stderr, "tsw-re: %s\n", err);
            app_shutdown(&app);
            return 1;
        }
        render_present(&app.render);
        app_shutdown(&app);
        return 0;
    }

    app_run(&app);
    app_shutdown(&app);
    return 0;
}
