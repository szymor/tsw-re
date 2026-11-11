#include "events.h"

#include <stdio.h>

static void log_callback(void *user, const char *message)
{
    int *count = user;
    (*count)++;
    printf("lua: %s\n", message);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_lua <script.lua>\n");
        return 2;
    }
    int count = 0;
    char err[512] = "";
    struct lua_State *lua = NULL;
    if (!events_init(&lua, argv[1], log_callback, &count, err, sizeof(err))) {
        fprintf(stderr, "events_init failed: %s\n", err);
        return 1;
    }
    events_shutdown(lua);
    if (count != 1) {
        fprintf(stderr, "expected one log message, got %d\n", count);
        return 1;
    }
    printf("test_lua: ok\n");
    return 0;
}
