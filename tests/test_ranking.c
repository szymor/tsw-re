/* optionN.dat: the seal/check of FUN_00439ce0 / FUN_00437f2c, insertion,
 * and (with TSW_RANKING_DIR set to an original Savedat folder) the
 * original's own files. */
#include "ranking.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
    __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    Ranking r;
    ranking_builtin(&r, 3);
    CHECK(r.v[0] == 100 && r.v[1] == 10000 && r.v[2] == 500 && r.v[3] == 500);
    ranking_builtin(&r, 5);
    CHECK(r.v[1] == 10000);                       /* area 5 = area 3 */
    CHECK(ranking_check(&r, 3, -4) == RANKING_BAD_SUMS);

    ranking_seal(&r, 3, -4);
    CHECK(ranking_check(&r, 3, -4) == RANKING_OK);
    CHECK(r.v[71] == -403);
    CHECK(r.v[4] == 100 + 10000 + 500 + 500);
    CHECK(r.v[5] == 100 - 10000 + 500 - 500 + r.v[4]);
    CHECK(ranking_check(&r, 2, -4) == RANKING_BAD_VERSION);
    CHECK(ranking_check(&r, 3, 0) == RANKING_BAD_VERSION);
    r.v[13]++;
    CHECK(ranking_check(&r, 3, -4) == RANKING_BAD_SUMS);
    r.v[13]--;

    /* The documented row of option3.dat. */
    int32_t row[4] = { 214, 2595, 387, 465 };
    ranking_insert(&r, 1, row[0], row[1], row[2], row[3], "A");
    ranking_seal(&r, 3, -4);
    CHECK(r.v[4] == 3661 && r.v[5] == 1202);
    CHECK(r.v[6] == 100 && strcmp(r.names[0], "A") == 0);
    ranking_insert(&r, 10, 1, 2, 3, 4, "Z");
    CHECK(r.v[54] == 1 && strcmp(r.names[9], "Z") == 0);
    ranking_insert(&r, 2, 5, 6, 7, 8, "");
    CHECK(r.v[6] == 5 && strcmp(r.names[1], "") == 0 &&
          strcmp(r.names[0], "A") == 0);

    CHECK(ranking_save(&r, ".", 3, NULL, 0));
    Ranking back;
    ranking_seal(&r, 3, -4);
    CHECK(ranking_save(&r, ".", 3, NULL, 0));
    CHECK(ranking_load(&back, ".", 3, -4) == RANKING_OK);
    CHECK(memcmp(back.v, r.v, sizeof(r.v)) == 0);
    CHECK(strcmp(back.names[0], "A") == 0 && strcmp(back.names[9], "Z") == 0);
    CHECK(ranking_load(&back, "./no-such-dir", 3, -4) == RANKING_MISSING);
    CHECK(back.v[0] == 100);                      /* the temporary ranking */

    const char *dir = getenv("TSW_RANKING_DIR");
    for (int area = 1; dir && area <= 5; area++)
        CHECK(ranking_load(&back, dir, area, -4) == RANKING_OK);

    if (failures) {
        fprintf(stderr, "test_ranking: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_ranking: ok\n");
    return 0;
}
