/* Golden-vector reader shared by the parity tests (vectors exported by
 * docs/re/tools/export_event_vectors.py; see 16-event-pilot.md). */
#ifndef TSW_TEST_VECTORS_H
#define TSW_TEST_VECTORS_H

#include "event_builders.h"
#include "fileio.h"
#include "event_vm.h"
#include "map.h"
#include "state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char case_id[64];
    char handler[64];
    StateBlock state;
    MapBlock map;
    EventAux aux;
    EventCommand *cmds;
    int ncmds;
    int cap;
    StateBlock final_state;
    MapBlock final_map;
    EventAux final_aux;
    /* Execution-oracle drain (export_event_vectors.py drain_* lines). */
    bool has_drain;
    int dialogues[64];
    int ndialogues;
    int message_dlg;          /* "try again?" answer: 6 mrYes, 7 mrNo */
    StateBlock drain_state;
    MapBlock drain_map;
    /* The buffer before the handler runs (pre lines) and its count. */
    EventCommand pre[64];
    int npre;
    int precount;
    /* What BlockRead returns for the ranking file (rankfile line). */
    bool has_rankfile;
    int32_t rankfile[72];
} Vector;

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* The next line of the text at `*p`, '\n' included, into `*line`. */
static bool next_line(const char **p, char **line, size_t *cap)
{
    const char *s = *p;
    if (!*s)
        return false;
    const char *e = strchr(s, '\n');
    size_t n = e ? (size_t)(e - s) + 1 : strlen(s);
    if (n + 1 > *cap) {
        *cap = n + 1;
        *line = realloc(*line, *cap);
    }
    memcpy(*line, s, n);
    (*line)[n] = '\0';
    *p = s + n;
    return true;
}

static bool load_vector(const char *path, Vector *v)
{
    char *text = file_load(path, NULL);
    if (!text)
        return false;
    const char *cur = text;
    memset(v, 0, sizeof(*v));
    v->message_dlg = 6;
    size_t cap = 0;
    char *line = NULL;
    bool ok = true, ended = false;
    while (next_line(&cur, &line, &cap)) {
        char key[32], name[64];
        long a, b2, c;
        if (sscanf(line, "%31s", key) != 1)
            continue;
        if (!strcmp(key, "case")) {
            sscanf(line, "case %63s", v->case_id);
        } else if (!strcmp(key, "handler")) {
            sscanf(line, "handler %63s", v->handler);
        } else if (!strcmp(key, "state") && sscanf(line, "state %ld %ld", &a, &b2) == 2) {
            if (a >= 0 && a < STATE_INT_COUNT) v->state.raw[a] = (int32_t)b2; else ok = false;
        } else if (!strcmp(key, "map")) {
            const char *h = line + 4;
            for (int i = 0; i < MAP_BLOCK_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
                if (hi < 0 || lo < 0) { ok = false; break; }
                v->map.bytes[i] = (uint8_t)(hi * 16 + lo);
            }
        } else if (!strcmp(key, "aux") && sscanf(line, "aux %63s %ld", name, &a) == 2) {
            int id = event_aux_find(name);
            if (id >= 0) v->aux.v[id] = (int32_t)a;
        } else if (!strcmp(key, "cmd") && sscanf(line, "cmd %ld %ld %ld", &a, &b2, &c) == 3) {
            if (v->ncmds == v->cap) {
                v->cap = v->cap ? v->cap * 2 : 64;
                v->cmds = realloc(v->cmds, (size_t)v->cap * sizeof(*v->cmds));
            }
            v->cmds[v->ncmds++] = (EventCommand){ (uint16_t)a, (uint16_t)b2, (uint16_t)c };
        } else if (!strcmp(key, "pre") && sscanf(line, "pre %ld %ld %ld", &a, &b2, &c) == 3) {
            if (v->npre < 64)
                v->pre[v->npre++] = (EventCommand){ (uint16_t)a, (uint16_t)b2, (uint16_t)c };
        } else if (!strcmp(key, "rankfile")) {
            const char *p = line + 8;
            int n = 0;
            while (n < 72) {
                char *end;
                long x = strtol(p, &end, 10);
                if (end == p) break;
                v->rankfile[n++] = (int32_t)x;
                p = end;
            }
            v->has_rankfile = n == 72;
        } else if (!strcmp(key, "precount") && sscanf(line, "precount %ld", &a) == 1) {
            v->precount = (int)a;
        } else if (!strcmp(key, "dstate") || !strcmp(key, "dmap") || !strcmp(key, "daux")) {
            /* applied after the initial snapshot is complete */
        } else if (!strcmp(key, "drain_dialog") && sscanf(line, "drain_dialog %ld", &a) == 1) {
            if (v->ndialogues < 64) v->dialogues[v->ndialogues++] = (int)a;
        } else if (!strcmp(key, "drain_message_dlg") &&
                   sscanf(line, "drain_message_dlg %ld", &a) == 1) {
            v->message_dlg = (int)a;
        } else if (!strcmp(key, "drain_end")) {
            v->has_drain = true;
        } else if (!strcmp(key, "end")) {
            ended = true;
        }
    }
    /* second pass: deltas on top of the snapshot */
    v->final_state = v->state;
    v->final_map = v->map;
    v->final_aux = v->aux;
    v->drain_state = v->state;
    v->drain_map = v->map;
    cur = text;
    while (next_line(&cur, &line, &cap)) {
        char name[64];
        long a, b2;
        if (sscanf(line, "dstate %ld %ld", &a, &b2) == 2 && a >= 0 && a < STATE_INT_COUNT)
            v->final_state.raw[a] = (int32_t)b2;
        else if (sscanf(line, "dmap %ld %ld", &a, &b2) == 2 && a >= 0 && a < MAP_BLOCK_SIZE)
            v->final_map.bytes[a] = (uint8_t)b2;
        else if (sscanf(line, "xstate %ld %ld", &a, &b2) == 2 && a >= 0 && a < STATE_INT_COUNT)
            v->drain_state.raw[a] = (int32_t)b2;
        else if (sscanf(line, "xmap %ld %ld", &a, &b2) == 2 && a >= 0 && a < MAP_BLOCK_SIZE)
            v->drain_map.bytes[a] = (uint8_t)b2;
        else if (sscanf(line, "daux %63s %ld", name, &a) == 2) {
            int id = event_aux_find(name);
            if (id >= 0) v->final_aux.v[id] = (int32_t)a;
        }
    }
    free(line);
    file_free(text);
    return ok && ended && v->case_id[0] && v->handler[0];
}


#endif
