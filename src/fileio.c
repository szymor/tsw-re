#include "fileio.h"

#include <SDL3/SDL.h>

#include <string.h>

char *file_load(const char *path, size_t *len)
{
    size_t n = 0;
    char *data = path && *path ? SDL_LoadFile(path, &n) : NULL;
    if (len)
        *len = data ? n : 0;
    return data;
}

void file_free(void *data)
{
    SDL_free(data);
}

bool file_save(const char *path, const void *data, size_t len)
{
    if (!path || !*path)
        return false;
    size_t n = strlen(path) + 48;
    char *tmp = SDL_malloc(n);
    if (!tmp)
        return false;
    /* A name beside the file that is not in use. */
    static unsigned counter;
    bool named = false;
    for (int tries = 0; tries < 64 && !named; tries++) {
        SDL_snprintf(tmp, n, "%s.tmp%llx-%x", path,
                     (unsigned long long)SDL_GetTicksNS(), counter++);
        named = !SDL_GetPathInfo(tmp, NULL);
    }
    bool ok = false;
    SDL_IOStream *io = named ? SDL_IOFromFile(tmp, "wb") : NULL;
    if (io) {
        ok = SDL_WriteIO(io, data, len) == len;
        ok = SDL_CloseIO(io) && ok;
        ok = ok && SDL_RenamePath(tmp, path);
        if (!ok)
            SDL_RemovePath(tmp);
    }
    SDL_free(tmp);
    return ok;
}

bool path_make_parent(const char *path)
{
    if (!path)
        return false;
    char *dir = SDL_strdup(path);
    if (!dir)
        return false;
    char *slash = NULL;
    for (char *p = dir; *p; p++)
        if (*p == '/' || *p == '\\')
            slash = p;
    bool ok = true;
    if (slash && slash != dir) {
        *slash = '\0';
        ok = SDL_CreateDirectory(dir);
    }
    SDL_free(dir);
    return ok;
}

bool path_is_file(const char *path)
{
    SDL_PathInfo info;
    return path && *path && SDL_GetPathInfo(path, &info) &&
           info.type == SDL_PATHTYPE_FILE;
}

bool path_is_dir(const char *path)
{
    SDL_PathInfo info;
    return path && *path && SDL_GetPathInfo(path, &info) &&
           info.type == SDL_PATHTYPE_DIRECTORY;
}

bool text_next_line(const char **cursor, const char *end, char *line,
                    size_t size)
{
    const char *p = *cursor;
    if (!p || p >= end || size < 2)
        return false;
    size_t n = 0;
    while (p < end && n < size - 1) {
        char c = *p++;
        line[n++] = c;
        if (c == '\n')
            break;
    }
    line[n] = '\0';
    *cursor = p;
    return true;
}

/* `s` without its trailing separators, into `out`. */
static bool copy_dir(char *out, size_t outsz, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    while (n > 1 && (s[n - 1] == '/' || s[n - 1] == '\\'))
        n--;
    if (n == 0 || n >= outsz)
        return false;
    memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

bool path_exe_dir(char *out, size_t outsz)
{
    return copy_dir(out, outsz, SDL_GetBasePath());
}

bool path_user_dir(char *out, size_t outsz)
{
    char *pref = SDL_GetPrefPath(NULL, "tsw");
    bool ok = copy_dir(out, outsz, pref);
    SDL_free(pref);
    return ok;
}
