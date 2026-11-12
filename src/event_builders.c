#include "event_builders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

static const char *const AUX_NAMES[EVAUX_COUNT] = {
    "target_index", "direction", "sprite", "ui_mode", "message_selector",
    "event_selector", "event_sprite", "event_param", "roujin_aux",
    "roujin_selector", "ui_selector", "current_monster", "monster_hp",
    "shop_price", "shop_flag", "shop_talk_flag", "choice_mode",
    "battle_turns",
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
    /* syokidata2: with state[0x9c] == 1 Zeno's stats (g_monsters[0x0f])
     * scale with the difficulty instead of 8000/5000/1000. */
    if (index == 0x0f && vm->state && vm->state->raw[0x9c] == 1) {
        int32_t k = vm->state->raw[ST_OPTION_A] + 1;
        vm->monster.hp = k * 800;
        vm->monster.attack = k * 500;
        vm->monster.defense = k * 100;
    }
}

static int l_monster_stat(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    const char *field = luaL_checkstring(L, 2);
    if (index < 0 || index >= MONSTER_COUNT)
        return luaL_error(L, "monster index %d out of range", index);
    const MonsterDef *m = &MONSTER_TABLE[index];
    if (strcmp(field, "hp") == 0)
        lua_pushinteger(L, m->hp);
    else if (strcmp(field, "attack") == 0)
        lua_pushinteger(L, m->attack);
    else if (strcmp(field, "defense") == 0)
        lua_pushinteger(L, m->defense);
    else if (strcmp(field, "gold") == 0)
        lua_pushinteger(L, m->gold);
    else
        return luaL_error(L, "unknown monster field '%s'", field);
    return 1;
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
    return 0;
}

static int l_present(lua_State *L)
{
    const char *kind = luaL_checkstring(L, 1);
    int32_t args[8];
    int n = lua_gettop(L) - 1;
    if (n > 8)
        n = 8;
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
    lua_register(L, "monster_stat", l_monster_stat);
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

    int rc = luaL_loadbuffer(L, PRELUDE, sizeof(PRELUDE) - 1, "=prelude");
    if (rc == LUA_OK)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc == LUA_OK)
        rc = luaL_loadfile(L, script_path);
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
    }
    if (ok) {
        /* g_monster_index (0x48c5b8) and the monster HP (0x48c5bc) are the
         * VM's battle operands (ops 0x04/0x05/0x0e): a builder that picks
         * a monster (taisen2) hands it over. */
        if (aux->v[EVAUX_CURRENT_MONSTER] != aux_copy.v[EVAUX_CURRENT_MONSTER])
            event_builders_bind_monster(vm, aux->v[EVAUX_CURRENT_MONSTER]);
        if (aux->v[EVAUX_MONSTER_HP] != aux_copy.v[EVAUX_MONSTER_HP])
            vm->monster_hp = aux->v[EVAUX_MONSTER_HP];
    }
    lua_settop(L, base);
    g_ctx = saved;
    free(state_copy);
    free(map_copy);
    free(cmd_copy);
    return ok;
}
