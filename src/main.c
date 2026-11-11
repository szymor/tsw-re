#include "app.h"
#include "save.h"

#include <stdio.h>
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

static void print_usage(const char *argv0)
{
    printf("Usage: %s [options]\n", argv0);
    printf("  --data-dir <path>  Asset/data directory (auto-detected)\n");
    printf("  --save <path>      Load an original saveN.dat file\n");
    printf("  --font <path>      Override the UI font\n");
    printf("  --inspect-save <f> Parse an original save and print its state\n");
    printf("  --screenshot <png> Render one frame, save it, and exit\n");
    printf("  --menu             Open the F10 menu before the screenshot\n");
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
    const char *font_path = NULL;
    const char *screenshot_path = NULL;
    const char *inspect_save = NULL;
    bool open_menu = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir_arg = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save_path = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font_path = argv[++i];
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--inspect-save") == 0 && i + 1 < argc) {
            inspect_save = argv[++i];
        } else if (strcmp(argv[i], "--menu") == 0) {
            open_menu = true;
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

    char data_buf[APP_PATH_LEN];
    const char *data_dir = resolve_data_dir(data_dir_arg, data_buf,
                                            sizeof(data_buf));

    App app;
    char err[512] = "";
    if (!app_init(&app, data_dir, save_path, font_path, err, sizeof(err))) {
        fprintf(stderr, "tsw-re: %s\n", err);
        return 1;
    }
    if (open_menu)
        app.ui.menu_open = true;

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
