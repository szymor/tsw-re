#include "ranking.h"

#include "fileio.h"

#include <stdio.h>
#include <string.h>

/* FUN_00437f2c's built-in tables: per row score, life, attack (= defense),
 * the sums and checks left 0. */
static const int32_t BUILTIN[4][RANKING_ROWS][3] = {
    { {100, 500, 25}, {90, 450, 24}, {80, 400, 23}, {70, 350, 22},
      {60, 300, 21}, {50, 250, 20}, {40, 200, 19}, {30, 150, 18},
      {20, 100, 17}, {10, 50, 16} },
    { {100, 1000, 50}, {90, 900, 45}, {80, 800, 40}, {70, 700, 35},
      {60, 600, 30}, {50, 500, 25}, {40, 400, 20}, {30, 300, 15},
      {20, 200, 10}, {10, 100, 5} },
    { {100, 10000, 500}, {90, 9000, 450}, {80, 8000, 400}, {70, 7000, 350},
      {60, 6000, 300}, {50, 5000, 250}, {40, 4000, 200}, {30, 3000, 150},
      {20, 2000, 100}, {10, 1000, 50} },
    { {100, 5000, 200}, {90, 4500, 190}, {80, 4000, 180}, {70, 3500, 170},
      {60, 3000, 160}, {50, 2500, 150}, {40, 2000, 140}, {30, 1500, 130},
      {20, 1000, 120}, {10, 500, 110} },
};

void ranking_builtin(Ranking *r, int area)
{
    int t = area == 2 ? 1 : area == 3 || area == 5 ? 2 : area == 4 ? 3 : 0;
    memset(r->v, 0, sizeof(r->v));
    memset(r->names, 0, sizeof(r->names));
    for (int i = 0; i < RANKING_ROWS; i++) {
        r->v[i * 6 + 0] = BUILTIN[t][i][0];
        r->v[i * 6 + 1] = BUILTIN[t][i][1];
        r->v[i * 6 + 2] = BUILTIN[t][i][2];
        r->v[i * 6 + 3] = BUILTIN[t][i][2];
    }
}

/* Two's-complement sums, as the original's 32-bit adds wrap. */
static int32_t wrap(int64_t x) { return (int32_t)(uint32_t)(uint64_t)x; }

static int32_t row_sum(const int32_t *row)
{
    int64_t s = 0;
    for (int i = 0; i < 4; i++) s += row[i];
    return wrap(s);
}

static int32_t row_check(const int32_t *row)
{
    return wrap((int64_t)row[0] - row[1] + row[2] - row[3] + row[4]);
}

static int32_t col_sum(const int32_t *v, int c)
{
    int64_t s = 0;
    for (int i = 0; i < RANKING_ROWS; i++) s += v[i * 6 + c];
    return wrap(s);
}

/* +r1 -r2 ... -r10 + column sum */
static int32_t col_check(const int32_t *v, int c)
{
    int64_t s = 0;
    for (int i = 0; i < RANKING_ROWS; i++)
        s += (i % 2 == 0) ? v[i * 6 + c] : -(int64_t)v[i * 6 + c];
    return wrap(s + v[60 + c]);
}

int ranking_check(const Ranking *r, int area, int32_t magic)
{
    const int32_t *v = r->v;
    int bad = RANKING_OK;
    for (int i = 0; i < RANKING_ROWS; i++) {
        if (row_sum(&v[i * 6]) != v[i * 6 + 4]) bad = RANKING_BAD_SUMS;
        if (row_check(&v[i * 6]) != v[i * 6 + 5]) bad = RANKING_BAD_SUMS;
    }
    for (int c = 0; c < 6; c++) {
        if (col_sum(v, c) != v[60 + c]) bad = RANKING_BAD_SUMS;
        if (c != 5 && col_check(v, c) != v[66 + c]) bad = RANKING_BAD_SUMS;
    }
    if (bad == RANKING_OK && v[71] / 100 != magic)
        bad = RANKING_BAD_VERSION;
    if (bad == RANKING_OK && wrap((int64_t)magic * -100 + v[71]) != -area)
        bad = RANKING_BAD_VERSION;
    return bad;
}

void ranking_seal(Ranking *r, int area, int32_t magic)
{
    int32_t *v = r->v;
    for (int i = 0; i < RANKING_ROWS; i++) {
        v[i * 6 + 4] = row_sum(&v[i * 6]);
        v[i * 6 + 5] = row_check(&v[i * 6]);
    }
    for (int c = 0; c < 6; c++) {
        v[60 + c] = col_sum(v, c);
        v[66 + c] = col_check(v, c);
    }
    v[71] = wrap((int64_t)magic * 100 - area);
}

void ranking_insert(Ranking *r, int rank, int32_t score, int32_t life,
                    int32_t attack, int32_t defense, const char *name)
{
    if (rank < 1 || rank > RANKING_ROWS)
        return;
    int32_t *v = r->v;
    for (int i = RANKING_ROWS - 1; i >= rank; i--)
        for (int k = 0; k < 4; k++)
            v[i * 6 + k] = v[(i - 1) * 6 + k];
    if (name && *name) {
        for (int i = RANKING_ROWS - 1; i >= rank; i--)
            memcpy(r->names[i], r->names[i - 1], RANKING_NAME_LEN);
        snprintf(r->names[rank - 1], RANKING_NAME_LEN, "%s", name);
    }
    v[(rank - 1) * 6 + 0] = score;
    v[(rank - 1) * 6 + 1] = life;
    v[(rank - 1) * 6 + 2] = attack;
    v[(rank - 1) * 6 + 3] = defense;
}

static void path(char *out, size_t n, const char *dir, int area, bool names)
{
    snprintf(out, n, "%s/option%d%s.dat", dir, area, names ? "1" : "");
}

int ranking_load(Ranking *r, const char *dir, int area, int32_t magic)
{
    char p[1024];
    memset(r, 0, sizeof(*r));
    path(p, sizeof(p), dir, area, false);
    size_t n = 0;
    char *data = file_load(p, &n);
    const uint8_t *raw = (const uint8_t *)data;
    int status = RANKING_MISSING;
    if (data) {
        if (n >= RANKING_INTS * 4) {
            for (int i = 0; i < RANKING_INTS; i++)
                r->v[i] = (int32_t)((uint32_t)raw[i * 4] |
                                    (uint32_t)raw[i * 4 + 1] << 8 |
                                    (uint32_t)raw[i * 4 + 2] << 16 |
                                    (uint32_t)raw[i * 4 + 3] << 24);
            status = ranking_check(r, area, magic);
        }
        file_free(data);
    }
    if (status != RANKING_OK)
        ranking_builtin(r, area);
    path(p, sizeof(p), dir, area, true);
    data = file_load(p, &n);
    if (data) {
        const char *cur = data;
        char line[256];
        for (int i = 0; i < RANKING_ROWS &&
                        text_next_line(&cur, data + n, line, sizeof(line));
             i++) {
            size_t len = strcspn(line, "\r\n");
            if (len >= RANKING_NAME_LEN)
                len = RANKING_NAME_LEN - 1;
            memcpy(r->names[i], line, len);
            r->names[i][len] = '\0';
        }
        file_free(data);
    }
    return status;
}

bool ranking_save(const Ranking *r, const char *dir, int area,
                  char *err, size_t errsz)
{
    char p[1024];
    path(p, sizeof(p), dir, area, false);
    uint8_t raw[RANKING_INTS * 4];
    for (int i = 0; i < RANKING_INTS; i++) {
        uint32_t u = (uint32_t)r->v[i];
        raw[i * 4] = (uint8_t)u;
        raw[i * 4 + 1] = (uint8_t)(u >> 8);
        raw[i * 4 + 2] = (uint8_t)(u >> 16);
        raw[i * 4 + 3] = (uint8_t)(u >> 24);
    }
    if (!file_save(p, raw, sizeof(raw))) {
        if (err && errsz) snprintf(err, errsz, "cannot write %s", p);
        return false;
    }
    char names[RANKING_ROWS * (RANKING_NAME_LEN + 2)];
    size_t len = 0;
    for (int i = 0; i < RANKING_ROWS; i++)          /* TStrings.SaveToFile */
        len += (size_t)snprintf(names + len, sizeof(names) - len, "%s\r\n",
                                r->names[i]);
    path(p, sizeof(p), dir, area, true);
    if (!file_save(p, names, len)) {
        if (err && errsz) snprintf(err, errsz, "cannot write %s", p);
        return false;
    }
    return true;
}
