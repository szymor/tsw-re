#include "game.h"
#include "map.h"
#include "replay.h"
#include "save.h"
#include "state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    MapBlock map;
    memset(&map, 0, sizeof(map));
    map.bytes[0] = 0x00;
    map.bytes[1] = 0x01;
    int off = map_floor_offset(1);
    memset(map.bytes + off, 0x06, MAP_TILES);

    Game game;
    game_new(&game, &map);
    state_set(&game.state, ST_SPECIAL_FIRST + 12, 1); /* Magic Key */
    game.map.bytes[map_floor_offset(1)] = 0x01;       /* yellow door */
    SaveData save;
    game_snapshot(&game, &save);

    char err[512] = "";
    CHECK(save_write_file(&save, "test_replay_save.dat", err, sizeof(err)));

    FILE *f = fopen("test_replay.txt", "w");
    CHECK(f != NULL);
    fputs("load test_replay_save.dat\n"
          "expect floor 1\n"
          "expect x 5\n"
          "expect y 10\n"
          "move up\n"
          "expect y 9\n"
          "expect x 5\n"
          "use 12\n"
          "expect slot12 0\n"
          "expect tile 1 0 6\n"
          "save test_replay_out.dat\n", f);
    fclose(f);

    CHECK(replay_run(".", "test_replay.txt", err, sizeof(err)));

    SaveData out;
    CHECK(save_load_file("test_replay_out.dat", &out, err, sizeof(err)));
    CHECK(state_get(&out.state, ST_FLOOR) == 1);
    CHECK(state_get(&out.state, ST_X) == 5);
    CHECK(state_get(&out.state, ST_Y) == 9);

    FILE *bad = fopen("test_replay_bad.txt", "w");
    CHECK(bad != NULL);
    fputs("load test_replay_save.dat\n"
          "expect tile 99 999 nonsense\n", bad);
    fclose(bad);
    err[0] = '\0';
    CHECK(!replay_run(".", "test_replay_bad.txt", err, sizeof(err)));

    bad = fopen("test_replay_bad.txt", "w");
    CHECK(bad != NULL);
    fputs("load test_replay_save.dat\n"
          "expect tile 1 0 262\n", bad);
    fclose(bad);
    err[0] = '\0';
    CHECK(!replay_run(".", "test_replay_bad.txt", err, sizeof(err)));

    bad = fopen("test_replay_bad.txt", "w");
    CHECK(bad != NULL);
    fputs("load test_replay_save.dat\n"
          "expect tile 999999999999999999999 0 6\n", bad);
    fclose(bad);
    err[0] = '\0';
    CHECK(!replay_run(".", "test_replay_bad.txt", err, sizeof(err)));

    bad = fopen("test_replay_bad.txt", "w");
    CHECK(bad != NULL);
    fputs("load test_replay_save.dat\n"
          "expect tile 1 999999999999999999999 6\n", bad);
    fclose(bad);
    err[0] = '\0';
    CHECK(!replay_run(".", "test_replay_bad.txt", err, sizeof(err)));

    bad = fopen("test_replay_bad.txt", "w");
    CHECK(bad != NULL);
    fputs("load test_replay_save.dat\n"
          "expect slot 0\n", bad);
    fclose(bad);
    err[0] = '\0';
    CHECK(!replay_run(".", "test_replay_bad.txt", err, sizeof(err)));

    /* Opening a directory succeeds on Linux; fgets then fails and must be
     * reported as a replay error rather than clean EOF. */
    err[0] = '\0';
    CHECK(!replay_run(".", ".", err, sizeof(err)));

    remove("test_replay_save.dat");
    remove("test_replay_out.dat");
    remove("test_replay.txt");
    remove("test_replay_bad.txt");
    printf("test_replay: ok\n");
    return 0;
}
