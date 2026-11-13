#ifndef TSW_RANKING_H
#define TSW_RANKING_H

/* The per-area ranking (Form2): optionN.dat (288 bytes) and the names in
 * optionN1.dat (TStrings text, one line per row).  docs/re/03-state-and-save.md
 * "Ranking files"; load/check FUN_00437f2c, insert FUN_00436c14, seal and
 * write FUN_00439ce0, the fairy's own check in yousei (0x4793ac). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RANKING_ROWS 10
#define RANKING_INTS 72          /* 10 x 6 rows, 6 column sums, 6 footer */
#define RANKING_NAME_LEN 64

enum {
    RANKING_OK = 0,
    RANKING_BAD_SUMS = 1,        /* sums or checks broken (ui.tsv 3) */
    RANKING_BAD_VERSION = 2,     /* footer[5] wrong (ui.tsv 4) */
    RANKING_MISSING = 3          /* the file could not be read */
};

typedef struct {
    int32_t v[RANKING_INTS];     /* row r: v[r*6 + 0..5] = score, life,
                                  * attack, defense, sum, check */
    char names[RANKING_ROWS][RANKING_NAME_LEN];
} Ranking;

/* The built-in temporary ranking of an area (1..5; 5 = 3's table),
 * names empty. */
void ranking_builtin(Ranking *r, int area);
/* The original's check: row sums and checks, column sums, footer checks
 * (columns 1..5), footer[5] = magic * 100 - area. */
int ranking_check(const Ranking *r, int area, int32_t magic);
/* Recompute the sums, checks and footer (FUN_00439ce0). */
void ranking_seal(Ranking *r, int area, int32_t magic);
/* Insert at rank 1..10: rows rank..9 move down one (score..defense only),
 * the new row's first four ints are set, and the name too (an empty name
 * keeps the old one there, as Form2 skips the shift for it). */
void ranking_insert(Ranking *r, int rank, int32_t score, int32_t life,
                    int32_t attack, int32_t defense, const char *name);
/* Load optionN.dat + optionN1.dat from dir: RANKING_OK, or the failure
 * with the built-in table in *r. */
int ranking_load(Ranking *r, const char *dir, int area, int32_t magic);
bool ranking_save(const Ranking *r, const char *dir, int area,
                  char *err, size_t errsz);

#endif
