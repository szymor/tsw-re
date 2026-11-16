#include "options.h"

#include "event_vm.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TEXT_NAMES[OPT_TEXT_COUNT] = {
    "tiny", "small", "normal", "big"
};

void options_defaults(Options *o)
{
    o->sound = true;
    o->music = true;
    o->speed = EVM_SPEED_MIDDLE;
    o->text = OPT_TEXT_NORMAL;
}

bool options_path(char *out, size_t outsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;
    if (xdg && *xdg)
        n = snprintf(out, outsz, "%s/tsw/options.ini", xdg);
    else if (home && *home)
        n = snprintf(out, outsz, "%s/.config/tsw/options.ini", home);
    else
        return false;
    return n > 0 && (size_t)n < outsz;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

static int parse_bool(const char *v)
{
    if (!strcmp(v, "on") || !strcmp(v, "true") || !strcmp(v, "1"))
        return 1;
    if (!strcmp(v, "off") || !strcmp(v, "false") || !strcmp(v, "0"))
        return 0;
    return -1;
}

bool options_load(Options *o, const char *path)
{
    options_defaults(o);
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f)
        return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[')
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *k = trim(s), *v = trim(eq + 1);
        int b = parse_bool(v);
        if (!strcmp(k, "sound") && b >= 0) {
            o->sound = b;
        } else if (!strcmp(k, "music") && b >= 0) {
            o->music = b;
        } else if (!strcmp(k, "speed")) {
            if (!strcmp(v, "high")) o->speed = EVM_SPEED_HIGH;
            else if (!strcmp(v, "middle")) o->speed = EVM_SPEED_MIDDLE;
            else if (!strcmp(v, "low")) o->speed = EVM_SPEED_LOW;
        } else if (!strcmp(k, "text")) {
            for (int i = 0; i < OPT_TEXT_COUNT; i++)
                if (!strcmp(v, TEXT_NAMES[i]))
                    o->text = (OptTextSize)i;
        }
    }
    fclose(f);
    return true;
}

bool options_save(const Options *o, const char *path)
{
    if (!path || !*path)
        return false;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        SDL_CreateDirectory(dir);
    }
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "# Tower of the Sorcerer options\n");
    fprintf(f, "sound = %s\n", o->sound ? "on" : "off");
    fprintf(f, "music = %s\n", o->music ? "on" : "off");
    fprintf(f, "speed = %s\n", o->speed == EVM_SPEED_HIGH ? "high"
                               : o->speed == EVM_SPEED_LOW ? "low" : "middle");
    fprintf(f, "text = %s\n", TEXT_NAMES[o->text]);
    return fclose(f) == 0;
}

const char *options_speed_label(int speed)
{
    return speed == EVM_SPEED_HIGH ? "High"
         : speed == EVM_SPEED_LOW ? "Low" : "Middle";
}

const char *options_text_label(OptTextSize text)
{
    static const char *const L[OPT_TEXT_COUNT] = {
        "Tiny", "Small", "Normal", "Big"
    };
    return L[text < 0 || text >= OPT_TEXT_COUNT ? OPT_TEXT_NORMAL : text];
}
