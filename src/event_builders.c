#include "event_builders.h"

#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle.h"
#include "ranking.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

static const char *const AUX_NAMES[EVAUX_COUNT] = {
    "target_index", "direction", "sprite", "ui_mode", "message_selector",
    "event_selector", "work_558", "work_560", "subevent_id",
    "work_564", "ui_selector", "current_monster", "monster_hp",
    "shop_price", "shop_flag", "shop_talk_flag", "choice_mode",
    "battle_turns", "confirm_case",
};

const char *event_aux_name(int id)
{
    return (id >= 0 && id < EVAUX_COUNT) ? AUX_NAMES[id] : NULL;
}

int event_aux_find(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < EVAUX_COUNT; i++)
        if (strcmp(name, AUX_NAMES[i]) == 0)
            return i;
    return -1;
}

/* The builder currently running.  Builders are synchronous and the game is
 * single-threaded, so one context is enough. */
typedef struct {
    EventBuilders *b;
    EventVm *vm;
    EventAux *aux;
} RunContext;

static RunContext g_ctx;

static EventVm *ctx_vm(lua_State *L)
{
    if (!g_ctx.vm)
        luaL_error(L, "builder API used outside a builder run");
    return g_ctx.vm;
}

static int l_emit(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    lua_Integer op = luaL_checkinteger(L, 1);
    lua_Integer a1 = luaL_optinteger(L, 2, 0);
    lua_Integer a2 = luaL_optinteger(L, 3, 0);
    if (op < 0 || op > 0xffff)
        return luaL_error(L, "emit: opcode %d out of range", (int)op);
    if (!event_vm_append(vm, (int)op, (int)(a1 & 0xffff), (int)(a2 & 0xffff)))
        return luaL_error(L, "emit: command buffer overflow");
    return 0;
}

static int check_state_index(lua_State *L, int arg)
{
    lua_Integer i = luaL_checkinteger(L, arg);
    if (i < 0 || i >= STATE_INT_COUNT)
        luaL_error(L, "state index %d out of range", (int)i);
    return (int)i;
}

static int l_get_state(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int i = check_state_index(L, 1);
    lua_pushinteger(L, vm->state->raw[i]);
    return 1;
}

static int l_set_state(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int i = check_state_index(L, 1);
    lua_Integer v = luaL_checkinteger(L, 2);
    vm->state->raw[i] = (int32_t)(uint32_t)(v & 0xffffffff);
    return 0;
}

static int tile_offset(lua_State *L, lua_Integer floor, lua_Integer index)
{
    int off = map_floor_offset((int)floor);
    if (off < 0 || index < 0 || off + index >= MAP_BLOCK_SIZE)
        luaL_error(L, "tile (%d,%d) out of range", (int)floor, (int)index);
    return off + (int)index;
}

static int l_get_tile_at(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int off = tile_offset(L, luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
    lua_pushinteger(L, vm->map->bytes[off]);
    return 1;
}

static int l_set_tile_at(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int off = tile_offset(L, luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
    vm->map->bytes[off] = (uint8_t)luaL_checkinteger(L, 3);
    return 0;
}

/* A byte of the whole map block (header + 51 floors); the stairs read
 * the arrival indexes at floor * 0x7b (+1), i.e. the previous floor's
 * cells 121/122, or the header for floor 0. */
static int l_get_map_byte(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    lua_Integer off = luaL_checkinteger(L, 1);
    if (off < 0 || off >= MAP_BLOCK_SIZE)
        return luaL_error(L, "map byte %d out of range", (int)off);
    lua_pushinteger(L, vm->map->bytes[off]);
    return 1;
}

static int l_get_tile(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int off = tile_offset(L, vm->state->raw[ST_FLOOR], luaL_checkinteger(L, 1));
    lua_pushinteger(L, vm->map->bytes[off]);
    return 1;
}

static int l_set_tile(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    int off = tile_offset(L, vm->state->raw[ST_FLOOR], luaL_checkinteger(L, 1));
    vm->map->bytes[off] = (uint8_t)luaL_checkinteger(L, 2);
    return 0;
}

void event_builders_bind_monster(EventVm *vm, int index)
{
    vm->monster_index = index;
    if (index < 0 || index >= MONSTER_COUNT) {
        vm->monster = (EventMonster){ 0, 0, 0, 0 };
        return;
    }
    const MonsterDef *m = &MONSTER_TABLE[index];
    vm->monster = (EventMonster){ m->hp, m->attack, m->defense, m->gold };
    if (index == 0x0f) {                /* the run-time Zeno entry */
        vm->monster.hp = vm->zeno[0];
        vm->monster.attack = vm->zeno[1];
        vm->monster.defense = vm->zeno[2];
    }
}

static int l_monster_stat(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    const char *field = luaL_checkstring(L, 2);
    if (index < 0 || index >= MONSTER_COUNT)
        return luaL_error(L, "monster index %d out of range", index);
    const MonsterDef *m = &MONSTER_TABLE[index];
    const int32_t *z = index == 0x0f ? ctx_vm(L)->zeno : NULL;
    if (strcmp(field, "hp") == 0)
        lua_pushinteger(L, z ? z[0] : m->hp);
    else if (strcmp(field, "attack") == 0)
        lua_pushinteger(L, z ? z[1] : m->attack);
    else if (strcmp(field, "defense") == 0)
        lua_pushinteger(L, z ? z[2] : m->defense);
    else if (strcmp(field, "gold") == 0)
        lua_pushinteger(L, m->gold);
    else
        return luaL_error(L, "unknown monster field '%s'", field);
    return 1;
}

/* ranking_table(area, magic) -> rows, status.  yousei's read and check of
 * optionN.dat (0x4793ac): status 0 with the ten {score, life, attack} rows,
 * else 1 (sums), 2 (version) or 3 (unreadable) and nil.  Without a file
 * provider BlockRead yields zeros (status 2), like the oracle. */
static int l_ranking_table(lua_State *L)
{
    int area = (int)luaL_checkinteger(L, 1);
    int32_t magic = (int32_t)luaL_checkinteger(L, 2);
    Ranking r;
    memset(&r, 0, sizeof(r));
    int status = RANKING_OK;
    if (g_ctx.b && g_ctx.b->ranking_fn) {
        if (!g_ctx.b->ranking_fn(g_ctx.b->ranking_user, area, r.v))
            status = RANKING_MISSING;
    }
    if (status == RANKING_OK)
        status = ranking_check(&r, area, magic);
    if (status != RANKING_OK) {
        lua_pushnil(L);
        lua_pushinteger(L, status);
        return 2;
    }
    lua_createtable(L, RANKING_ROWS, 0);
    for (int i = 0; i < RANKING_ROWS; i++) {
        lua_createtable(L, 3, 0);
        for (int k = 0; k < 3; k++) {
            lua_pushinteger(L, r.v[i * 6 + k]);
            lua_rawseti(L, -2, k + 1);
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, status);
    return 2;
}

/* set_zeno(hp, attack, defense): write g_monsters[0x0f]. */
static int l_set_zeno(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    for (int i = 0; i < 3; i++)
        vm->zeno[i] = (int32_t)luaL_checkinteger(L, i + 1);
    return 0;
}

static int l_aux_index(lua_State *L)
{
    const char *name = luaL_checkstring(L, 2);
    int id = event_aux_find(name);
    if (id < 0)
        return luaL_error(L, "unknown auxiliary '%s'", name);
    if (!g_ctx.aux)
        return luaL_error(L, "aux used outside a builder run");
    lua_pushinteger(L, g_ctx.aux->v[id]);
    return 1;
}

static int l_aux_newindex(lua_State *L)
{
    const char *name = luaL_checkstring(L, 2);
    int id = event_aux_find(name);
    if (id < 0)
        return luaL_error(L, "unknown auxiliary '%s'", name);
    if (!g_ctx.aux)
        return luaL_error(L, "aux used outside a builder run");
    lua_Integer v = luaL_checkinteger(L, 3);
    g_ctx.aux->v[id] = (int32_t)(uint32_t)(v & 0xffffffff);
    if (id == EVAUX_SPRITE && g_ctx.b)
        g_ctx.b->sprite_set = true;     /* a handler chose a status text */
    if ((id == EVAUX_MONSTER_HP || id == EVAUX_CURRENT_MONSTER) && g_ctx.b)
        g_ctx.b->monster_set = true;    /* taisen / taisen2 / ... */
    return 0;
}

static int l_present(lua_State *L)
{
    const char *kind = luaL_checkstring(L, 1);
    /* status_cat's longest call (the won fight's line) has 24. */
    int32_t args[32];
    int n = lua_gettop(L) - 1;
    if (n > 32)
        return luaL_error(L, "present(%s): %d arguments, at most 32", kind, n);
    for (int i = 0; i < n; i++)
        args[i] = (int32_t)luaL_optinteger(L, i + 2, 0);
    if (g_ctx.b && g_ctx.b->present_fn)
        g_ctx.b->present_fn(g_ctx.b->present_user, kind, args, n);
    return 0;
}

static int l_get_count(lua_State *L)
{
    lua_pushinteger(L, ctx_vm(L)->count);
    return 1;
}

/* Direct write of the command count (0x48c5ac), e.g. Button1Click case 13
 * clears the timeline.  Only shrinking is allowed. */
static int l_set_count(lua_State *L)
{
    EventVm *vm = ctx_vm(L);
    lua_Integer n = luaL_checkinteger(L, 1);
    /* Shrinking drops commands; growing by one exposes the slot above the
     * last command with whatever an earlier timeline left there, like
     * madoushi2's extra count increment (0x462560). */
    if (n < 0 || n > vm->count + 1 || n >= EVENT_VM_CAPACITY)
        return luaL_error(L, "set_count: %d outside 0..%d", (int)n,
                          vm->count + 1);
    vm->count = (int)n;
    return 0;
}

static int l_fail(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    return luaL_error(L, "not ported: %s", msg);
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "builder error", 1);
    return 1;
}

static void open_libs(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    /* No filesystem or dynamic code loading from builder scripts. */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");
}

/* luaL_loadfile through fileio (UTF-8 paths on every platform); text
 * chunks only. */
static int load_script(lua_State *L, const char *path)
{
    size_t len = 0;
    char *src = file_load(path, &len);
    if (!src) {
        lua_pushfstring(L, "cannot open %s", path);
        return LUA_ERRFILE;
    }
    lua_pushfstring(L, "@%s", path);
    int rc = luaL_loadbufferx(L, src, len, lua_tostring(L, -1), "t");
    file_free(src);
    lua_remove(L, -2);                  /* the chunk name */
    return rc;
}

/* include(name): run the module <script dir>/<name>.lua (names of
 * [a-z0-9_] only, so nothing outside the folder is reachable).  Upvalue 1
 * is the folder. */
static int l_include(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    for (const char *c = name; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
              *c == '_'))
            return luaL_error(L, "bad module name '%s'", name);
    const char *dir = lua_tostring(L, lua_upvalueindex(1));
    lua_pushfstring(L, "%s/%s.lua", dir ? dir : ".", name);
    const char *path = lua_tostring(L, -1);
    if (load_script(L, path) != LUA_OK)
        return lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}

static const char PRELUDE[] =
    "builders = {}\n"
    "function call(name, ...)\n"
    "  local f = builders[name]\n"
    "  if not f then fail('builder ' .. tostring(name)) end\n"
    "  return f(...)\n"
    "end\n"
    /* C integer division (truncates toward zero). */
    "function idiv(a, b)\n"
    "  local q = a // b\n"
    "  if q < 0 and q * b ~= a then q = q + 1 end\n"
    "  return q\n"
    "end\n";

bool event_builders_init(EventBuilders *b, const char *script_path,
                         char *err, size_t errsz)
{
    memset(b, 0, sizeof(*b));
    lua_State *L = luaL_newstate();
    if (!L) {
        if (err && errsz) snprintf(err, errsz, "cannot create Lua state");
        return false;
    }
    open_libs(L);
    lua_register(L, "emit", l_emit);
    lua_register(L, "get_state", l_get_state);
    lua_register(L, "set_state", l_set_state);
    lua_register(L, "get_tile", l_get_tile);
    lua_register(L, "set_tile", l_set_tile);
    lua_register(L, "get_tile_at", l_get_tile_at);
    lua_register(L, "set_tile_at", l_set_tile_at);
    lua_register(L, "get_map_byte", l_get_map_byte);
    lua_register(L, "monster_stat", l_monster_stat);
    lua_register(L, "set_zeno", l_set_zeno);
    lua_register(L, "ranking_table", l_ranking_table);
    lua_register(L, "present", l_present);
    lua_register(L, "fail", l_fail);
    lua_register(L, "get_count", l_get_count);
    lua_register(L, "set_count", l_set_count);
    lua_newtable(L);                 /* aux proxy */
    lua_newtable(L);
    lua_pushcfunction(L, l_aux_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_aux_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "aux");
    {
        /* The script's folder, for include(). */
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", script_path);
        char *slash = NULL;                  /* '/' or, on Windows, '\\' */
        for (char *c = dir; *c; c++)
            if (*c == '/' || *c == '\\')
                slash = c;
        if (slash)
            *slash = '\0';
        else
            snprintf(dir, sizeof(dir), ".");
        lua_pushstring(L, dir);
        lua_pushcclosure(L, l_include, 1);
        lua_setglobal(L, "include");
    }

    int rc = luaL_loadbuffer(L, PRELUDE, sizeof(PRELUDE) - 1, "=prelude");
    if (rc == LUA_OK)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc == LUA_OK)
        rc = load_script(L, script_path);
    if (rc == LUA_OK)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        const char *m = lua_tostring(L, -1);
        if (err && errsz)
            snprintf(err, errsz, "Lua error in %s: %s", script_path,
                     m ? m : "unknown error");
        lua_close(L);
        return false;
    }
    b->L = L;
    return true;
}

void event_builders_shutdown(EventBuilders *b)
{
    if (b && b->L)
        lua_close(b->L);
    if (b)
        b->L = NULL;
}

bool event_builders_has(EventBuilders *b, const char *name)
{
    if (!b || !b->L || !name)
        return false;
    lua_getglobal(b->L, "builders");
    bool ok = false;
    if (lua_istable(b->L, -1)) {
        lua_getfield(b->L, -1, name);
        ok = lua_isfunction(b->L, -1);
        lua_pop(b->L, 1);
    }
    lua_pop(b->L, 1);
    return ok;
}

bool event_builders_run(EventBuilders *b, const char *name, EventVm *vm,
                        EventAux *aux)
{
    b->err[0] = '\0';
    if (!b->L || !vm || !vm->state || !vm->map || !aux) {
        snprintf(b->err, sizeof(b->err), "builder runtime not ready");
        return false;
    }
    if (!event_builders_has(b, name)) {
        snprintf(b->err, sizeof(b->err), "no builder '%s'", name);
        return false;
    }
    bool outer_monster_set = b->monster_set;   /* a nested run's caller */
    b->monster_set = false;
    /* Snapshot for fail-closed restore. */
    StateBlock *state_copy = malloc(sizeof(*state_copy));
    MapBlock *map_copy = malloc(sizeof(*map_copy));
    EventCommand *cmd_copy = malloc(sizeof(vm->commands));
    if (!state_copy || !map_copy || !cmd_copy) {
        free(state_copy); free(map_copy); free(cmd_copy);
        snprintf(b->err, sizeof(b->err), "out of memory");
        return false;
    }
    *state_copy = *vm->state;
    *map_copy = *vm->map;
    memcpy(cmd_copy, vm->commands, sizeof(vm->commands));
    EventAux aux_copy = *aux;
    int count_copy = vm->count;
    bool overflow_copy = vm->overflow;
    int32_t zeno_copy[3] = { vm->zeno[0], vm->zeno[1], vm->zeno[2] };

    RunContext saved = g_ctx;
    g_ctx.b = b;
    g_ctx.vm = vm;
    g_ctx.aux = aux;

    lua_State *L = b->L;
    int base = lua_gettop(L);
    lua_pushcfunction(L, traceback);
    lua_getglobal(L, "builders");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    int rc = lua_pcall(L, 0, 0, base + 1);
    bool ok = rc == LUA_OK;
    if (!ok) {
        const char *m = lua_tostring(L, -1);
        snprintf(b->err, sizeof(b->err), "%s: %s", name, m ? m : "error");
        *vm->state = *state_copy;
        *vm->map = *map_copy;
        memcpy(vm->commands, cmd_copy, sizeof(vm->commands));
        *aux = aux_copy;
        vm->count = count_copy;
        vm->overflow = overflow_copy;
        memcpy(vm->zeno, zeno_copy, sizeof(zeno_copy));
    }
    if (ok) {
        /* g_monster_index (0x48c5b8) and the monster HP (0x48c5bc) are the
         * VM's battle operands (ops 0x04/0x05/0x0e), one global each in
         * the original: whenever a builder writes them (taisen through
         * handan, taisen2, ...) the VM takes them, also when the value is
         * the same as before -- a second monster of the same kind starts
         * from its full HP, not from the last fight's 0. */
        bool picked = b->monster_set ||
                      aux->v[EVAUX_MONSTER_HP] != aux_copy.v[EVAUX_MONSTER_HP] ||
                      aux->v[EVAUX_CURRENT_MONSTER] != aux_copy.v[EVAUX_CURRENT_MONSTER];
        if (picked) {
            event_builders_bind_monster(vm, aux->v[EVAUX_CURRENT_MONSTER]);
            vm->monster_hp = aux->v[EVAUX_MONSTER_HP];
        }
    }
    b->monster_set = outer_monster_set;
    lua_settop(L, base);
    g_ctx = saved;
    free(state_copy);
    free(map_copy);
    free(cmd_copy);
    return ok;
}
