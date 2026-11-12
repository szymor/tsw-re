-- Event builders: Lua ports of the original TSW.exe event handlers.
--
-- Each builder is named after the original method and cites its address in
-- the pinned binary (sha256 41cb9844...2779).  A builder runs to completion
-- without yielding, exactly like the original: it reads state, map and
-- auxiliaries, applies builder-side effects and appends 6-byte commands with
-- emit(op, arg1, arg2) in the original append order.  The command VM
-- (src/event_vm.c) consumes them tail first.
--
-- Acceptance: golden-vector parity with the native oracle
-- (docs/re/tools/export_event_vectors.py, tests/test_event_parity.c).
-- Paths that are not ported call fail(); the runtime then restores every
-- input, so an unported branch never half-runs.
--
-- API (src/event_builders.c):
--   emit(op, a1, a2)            get_state(i) / set_state(i, v)
--   get_tile(i) / set_tile(i, v)          current floor (state[4])
--   get_tile_at(f, i) / set_tile_at(f, i, v)
--   monster_stat(index, field)   monster table lookup (hp/attack/defense/gold)
--   aux.<name>                  auxiliary globals (see event_builders.h)
--   call(name)                  run another builder
--   present(kind, ...)          presentation side effect; the engine acts on
--                               timer2on, timer2(0|1), zyouout, dialog_hide, ...
--   get_count() / set_count(n)  command count (0x48c5ac); set_count shrinks
--   fail(msg)                   abort: path not ported
--   idiv(a, b)                  C integer division (truncates toward zero)
--
-- Named constants at the top cover the VM opcodes, sub-dispatch commands,
-- state indices and map geometry.  Handler-specific sprite/message/tile IDs
-- stay as annotated literals until P5 gives them verified names.

local ST_LIFE, ST_ATTACK, ST_DEFENSE = 0, 1, 2
local ST_GOLD = 3
local ST_FLOOR, ST_FLOOR_LIMIT, ST_X, ST_Y = 4, 5, 6, 7
local ST_BATTLE_RENDER = 0x0c          -- 0x30 / 4
local ST_SPECIAL0 = 17                 -- special-item slot 0
local SLOT_ORB_OF_HERO, SLOT_ORB_OF_WISDOM, SLOT_MAGIC_ELIXIR = 0, 1, 4
local ST_SHIELD_SACRED = 41            -- Sacred Shield picked (0x4b872c)
local ST_SOUND = 0x5a                  -- WAV/sound state (flag_5a)
local ST_ROUJIN_F2 = 0x62              -- floor-2 old man one-shot (0x4b8810)
local ST_ROUJIN_SUB1B = 0x6d           -- floor-16 x=9 old man gift (0x4b883c)
local ST_FLOOR40_DONE = 0x96           -- floor-40 moncheck one-shot (0x4b88e0)
local ST_STAIRS_ADJ = 0xa6             -- set by mmes next to stairs (meta_a6)
local ST_ALTAR = 0x0b                  -- altar offerings so far (0x4b86b4)
local ST_OPTION_A = 159                -- 0x27c
local ST_FACING = 0x160 // 4           -- 88

-- VM opcodes (src/event_vm.h, docs/re/12-event-vm.md section 4).  Builders
-- append in original order; the VM consumes from the tail.
local OP = {
    NOP = 0x00, SOUND = 0x01, SPRITE = 0x02, ALTAR = 0x03,
    LIFE_DELTA = 0x04, MONSTER_DELTA = 0x05, SET_TILE = 0x06,
    ADD_GOLD = 0x07, SHOW_ITEMS = 0x08, MASKED_TILE = 0x09,
    START_SUBEVENT = 0x0a, SUBDISPATCH = 0x0b, MONSTER_MOVE = 0x0c,
    DOOR_OPEN = 0x0d, BATTLE_HIT = 0x0e, STOP_BGM = 0x0f,
    ADD_LIFE = 0x10, SET_TILE_SILENT = 0x11, NOP_12 = 0x12,
    HALVE_LIFE = 0x13, SET_STATE = 0x14, DRAW_ACTOR = 0x15, RESET = 0x16,
}

-- 0x0b sub-dispatch commands (docs/re/12-event-vm.md section 5).
local SUB = {
    OPENNING31 = 1, ZENOLIVEWORK = 2, MADOUSHI2 = 3, BANPAIAWORK = 4,
    MONCHECK = 5, GAMEOVER = 6, TITLE = 7, PLAYER_UPDATE = 8,
    PLAYER_ANIM = 9, TAISEN2 = 10, CHANGE_FLOOR = 0x0b,
    OPENNING9 = 0x0c, HIDE_MESSAGE = 0x0d, HIDE_IMAGES = 0x0e, FADE = 0x0f,
}

local RAW_FLOOR, RAW_ZENO_TRIGGER = 0x06, 0x07
local RAW_MAGIC_SERGEANT = 0x5d
local TILE_PX = 32                     -- 640x400 layout: Image (form+0x254) width
local MAP_COLS = 11                    -- 11x11 map records (02-map-format.md)

local function st(i) return get_state(i) end
local function slot(s) return get_state(ST_SPECIAL0 + s) end

-- Presentation helpers with no state effect in the original:
--   itemdel (0x450758) hides the item panel, timer2on (0x44bc0c) starts the
--   timeline timer, zyouout (0x449e18) opens the dialogue window for the
--   current message selector, disp (0x44cb34) redraws the status panel.
-- status_text(id): ListBox2[id] into the status-bar string 0x4b8684; disp
--   shows it in Edit8 (the bar under the map) while g_anim_sprite is 1.
-- itemdel (0x450758) / itemlive (0x450880) also clear/set the word flag at
-- 0x48c5ca, the high half of aux.ui_selector (0x48c5c8 is a word).
local function itemdel()
    aux.ui_selector = aux.ui_selector & 0xffff
    present("itemdel")
end
local function itemlive()
    aux.ui_selector = (aux.ui_selector & 0xffff) | 0x10000
    present("itemlive")
end
local function timer2on() present("timer2on") end
local function zyouout() present("zyouout", aux.message_selector) end

---------------------------------------------------------------------------
-- mmes (0x44c9f0): classify the tile at aux.event_selector on the current
-- floor into aux.sprite (g_anim_sprite, 0x48c58c).  Stairs next to the
-- player on the highest floor set state[0xa6] to 1 (up) or 2 (down).

local function mmes()
    local raw = get_tile(aux.event_selector)
    if raw <= 0x12 then
        if raw == 0x0b or raw == 0x0c then
            aux.sprite = 0x13
            if st(ST_FLOOR) == st(ST_FLOOR_LIMIT) then
                set_state(ST_STAIRS_ADJ, raw == 0x0b and 1 or 2)
            end
        elseif raw == 0x0d then
            aux.sprite = 0x34
        elseif raw == 0x10 or raw == 0x12 then
            aux.sprite = 0x18
        end
    elseif raw <= 0x18 then
        if raw <= 0x14 then aux.sprite = 0x0d
        elseif raw <= 0x16 then aux.sprite = 0x0f
        else aux.sprite = 0x0e end
    else
        if raw <= 0x1a then aux.sprite = 0x0c
        elseif raw == 0x97 or raw == 0x98 then aux.sprite = 0x6e
        elseif raw == 0x99 or raw == 0x9a then aux.sprite = 0x6f end
    end
end

---------------------------------------------------------------------------
-- idou (0x44c04c): move the player onto aux.target_index, then run the
-- adjacency checks.  The step animation (yusyaidou) and redraws are
-- presentation.  Magician attacks (mazyutu1, tiles 0x97..0x9a) and the
-- Magic Sergeant pincer (madoushi1) are not ported yet and fail closed.

builders.idou = function()
    local target = aux.target_index
    -- The step loop (0x44c10e) counts g_battle_turns 1..n, n = tile width
    -- (32 px; 8 at high and 16 at middle speed, which the harness does not
    -- set), so it ends at n.  The drawing in it is presentation; the engine
    -- turns present("player_step") into the normal tile slide.
    present("player_step", target)
    aux.battle_turns = TILE_PX
    set_state(ST_X, target % MAP_COLS)
    set_state(ST_Y, target // MAP_COLS)
    present("player_draw")              -- yusyahyouji1
    aux.event_param = 0                 -- DAT_0048c560
    aux.sprite = 0
    local x, y = st(ST_X), st(ST_Y)
    local p = y * MAP_COLS + x
    aux.event_sprite = p                -- DAT_0048c558

    local probes = {
        { -11, y >= 1 and y <= 10 },
        { -1, x >= 1 and x <= 10 },
        { 1, x >= 0 and x < 10 },
        { 11, y >= 0 and y < 10 },
    }
    for _, pr in ipairs(probes) do
        aux.event_selector = p + pr[1]
        if pr[2] then
            mmes()
            if aux.sprite == 0x6e or aux.sprite == 0x6f then
                fail("idou: mazyutu1 magician adjacency")
            end
        end
    end
    if st(ST_SHIELD_SACRED) == 0 then
        if x >= 1 and x <= 9 and get_tile(p - 1) == RAW_MAGIC_SERGEANT
            and get_tile(p + 1) == RAW_MAGIC_SERGEANT then
            fail("idou: madoushi1 horizontal pincer")
        end
        if y >= 1 and y <= 9 and get_tile(p - 11) == RAW_MAGIC_SERGEANT
            and get_tile(p + 11) == RAW_MAGIC_SERGEANT then
            fail("idou: madoushi1 vertical pincer")
        end
    end
    if aux.event_param == 0 then
        aux.sprite = 0
        for _, pr in ipairs(probes) do
            aux.event_selector = p + pr[1]
            if pr[2] then mmes() end
        end
        present("disp")
    end
end

---------------------------------------------------------------------------
-- zenolivework (0x461a0c): Zeno's appearance.  17 three-sprite frames
-- 0xb6 -> 0xa6 at the actor index in aux.event_sprite, then sound 7.
-- Also reached as timeline sub-event 0x0b/2.

builders.zenolivework = function()
    present("zeno_setup")
    timer2on()
    local actor = aux.event_sprite & 0xffff
    for frame = 0xb6, 0xa6, -1 do
        emit(OP.NOP, 6, 0x5b)
        emit(OP.MASKED_TILE, actor, frame)
    end
    emit(OP.NOP, 0, 0)
    emit(OP.SOUND, 7, 0)
end

---------------------------------------------------------------------------
-- mevent (0x46397c): map events reached from handan raw 0x07.  Floor
-- switch 0x4639b6 / 0x4639e8.  Ported: floor 3 (0x463a2c, the opening Zeno
-- trigger) and floor 10 (idou, tile cleared, ichicheck).
--
-- Floor 3: the player steps onto the trigger tile (idou), the tile is
-- cleared 0x07 -> 0x06 (the one-shot), sub-event 10 (selector 0x14) is
-- queued with four waits, then Zeno appears at actor index 0x46.

local MEVENT_ICHICHECK = {      -- idou, clear the tile, ichicheck
    [10] = true, [15] = true, [30] = true, [33] = true, [38] = true,
    [43] = true,
}

builders.mevent = function()
    itemdel()
    local floor = st(ST_FLOOR)
    if floor == 32 or floor == 42 or floor == 48 then
        -- 0x463ec7 / 0x463f8e / 0x463fc9: ichicheck only
        return call("ichicheck")
    end
    if floor == 20 then                 -- 0x463c3b: player index 0x52, BGM 0x10
        call("idou")
        if st(ST_Y) * MAP_COLS + st(ST_X) ~= 0x52 then return end
        set_tile(st(ST_Y) * MAP_COLS + st(ST_X), RAW_FLOOR)
        if st(ST_SOUND) ~= 0 then
            present("media_stop")
            set_state(ST_SOUND, 0x10)
            present("bgm", 0x10)
        end
        return call("ichicheck")
    end
    if floor == 25 then                 -- 0x463e3e: BGM 7 when sound is on
        call("idou")
        set_tile(st(ST_Y) * MAP_COLS + st(ST_X), RAW_FLOOR)
        if st(ST_SOUND) ~= 0 then
            present("media_stop")
            set_state(ST_SOUND, 7)
            present("bgm", 7)
        end
        return call("ichicheck")
    end
    if floor == 49 then                 -- 0x463fd2: BGM 0x13 when sound is on
        call("idou")
        set_tile(st(ST_Y) * MAP_COLS + st(ST_X), RAW_FLOOR)
        if st(ST_SOUND) ~= 0 then
            present("media_stop")
            set_state(ST_SOUND, 0x13)
            present("bgm", 0x13)
        end
        return call("ichicheck")
    end
    if MEVENT_ICHICHECK[floor] then  -- e.g. floor 10 (0x463bff)
        call("idou")
        set_tile(st(ST_Y) * MAP_COLS + st(ST_X), RAW_FLOOR)
        return call("ichicheck")
    end
    if floor ~= 3 then
        fail(string.format("mevent floor %d", floor))
    end
    call("idou")
    timer2on()
    set_tile(st(ST_Y) * MAP_COLS + st(ST_X), RAW_FLOOR)
    present("button_caption", 0xda)
    emit(OP.START_SUBEVENT, 10, 0)
    emit(OP.NOP, 0, 0)
    emit(OP.NOP, 0, 0)
    emit(OP.NOP, 0, 0)
    emit(OP.NOP, 0, 0)
    aux.event_sprite = 0x46
    call("zenolivework")
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0)
        emit(OP.SOUND, 6, 0)
    end
end

---------------------------------------------------------------------------
-- soundcheck (0x47c8f8): pick the floor's BGM
-- into state[0x5a]: floor 0 -> 0x15, 1..10 -> 5, 11..20 -> 6,
-- 21..30 -> 0x11, 31..40 -> 8, 41..49 -> 9, 50 -> 10.  soundplay
-- (0x47c2bc) then plays the MIDI for that id; that part is presentation.

local function soundcheck()
    local f = st(ST_FLOOR)
    local id
    if f == 0 then id = 0x15
    elseif f >= 1 and f <= 10 then id = 5
    elseif f >= 11 and f <= 20 then id = 6
    elseif f >= 21 and f <= 30 then id = 0x11
    elseif f >= 31 and f <= 40 then id = 8
    elseif f >= 41 and f <= 49 then id = 9
    elseif f == 50 then id = 10 end
    if id then set_state(ST_SOUND, id) end
end

local function soundplay()
    present("bgm", st(ST_SOUND))
end

---------------------------------------------------------------------------
-- doorstackin (0x4660d0): expands the door command just appended into the
-- door animation.  The original pops that command (op 0x0d, a1, a2), sets
-- aux.event_sprite = a1 and aux.event_selector = a2, and appends 16 pairs
-- (0x00, 0, k), (0x0d, a1, a2) for k = 16 .. 1.  Executed tail first, the
-- door progress (previous command's arg2) runs 1 .. 16; at 16 the door
-- handler writes the map.  Callers pass the door command instead of
-- emitting it first: the queue that results is identical.

local function doorstackin(a1, a2)
    aux.event_sprite = a1
    aux.event_selector = a2
    for k = 0x10, 1, -1 do
        emit(OP.NOP, 0, k)
        emit(OP.DOOR_OPEN, a1, a2)
    end
end

---------------------------------------------------------------------------
-- touzoku (0x465900): the thief (raw 0x15/0x16), first met as the prisoner
-- in the floor-2 cell.  Floor switch on state[4]; every path first hides the
-- item panel and sets the Button1 caption.
--
-- Floor 2 (by the player's x):
--   x 2..3  the cell: sub-event 14 (his first speech), the cell door at
--           index 0x43 opens (door kind 7, doorstackin), sound 1.
--           Button1Click case 0x0e then walks him 68 -> 67 -> 66 -> 77.
--   x 0     at the tunnel (index 77): sub-event 15; case 0x0f walks him
--           out and clears the tile.
--   x 9..10 later meetings: sub-event 16, or 17 once state[0x60] is set.
-- Floor 15: tile 6 cleared, two moves, sub-event 0x1c, door at 7.
-- Floor 29: sub-events 0x25 / 0x26 / 0x27.  Floor 35: clears block offset
-- 0x16e (floor 2, index 120) and runs sub-event 0x2a / 0x2b.  Floor 50:
-- sub-event 0x112.

local ST_THIEF_F2 = 0x60               -- flag_60
local ST_THIEF_F29 = 0x76              -- flag_76
local ST_THIEF_F35 = 0x7a              -- flag_7a

builders.touzoku = function()
    itemdel()
    present("button_caption")
    local floor = st(ST_FLOOR)
    if floor == 0x1d then
        if st(ST_Y) * MAP_COLS + st(ST_X) == 0x1b then
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x1b, 0)
            timer2on()
        elseif st(ST_THIEF_F29) == 0 then
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x1c, 0)
            timer2on()
        elseif st(ST_THIEF_F29) == 1 then
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x1d, 0)
            timer2on()
        end
    elseif floor == 2 then
        local x = st(ST_X)
        if x == 0 then
            timer2on()
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 5, 0)
        elseif x == 2 or x == 3 then
            aux.sprite = 1
            present("status_text", 0x89)
            timer2on()
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 4, 0)
            emit(OP.NOP, 0, 0); emit(OP.SPRITE, 1, 0)
            emit(OP.NOP, 0, 0)
            doorstackin(0x43, 7)
            emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
            if st(ST_SOUND) ~= 0 then
                soundcheck()
                soundplay()
            end
        elseif x == 9 or x == 10 then
            aux.event_sprite = st(ST_THIEF_F2) == 1 and 7 or 6
            timer2on()
            emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, aux.event_sprite, 0)
        end
    elseif floor == 0x0f then
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 6, 6)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 7, 6)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 8, 7)
        emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x12, 0)
        emit(OP.NOP, 0, 0)
        doorstackin(7, 7)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
        emit(OP.NOP, 0, 0)
        emit(OP.NOP, 0, 0)
        timer2on()
    elseif floor == 0x23 then
        set_tile_at(2, 120, 6)         -- g_map[0x16e]
        aux.event_sprite = st(ST_THIEF_F35) == 1 and 0x21 or 0x20
        timer2on()
        emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, aux.event_sprite, 0)
    elseif floor == 0x32 then
        timer2on()
        emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x108, 0)
    end
end

---------------------------------------------------------------------------
-- moncheck (0x4664e4): runs after every won battle (the battle timeline's
-- last command is the splice 0x0b/5).  Floor switch 0x466512 / 0x466545;
-- each case sums a few map tiles to tell whether a group of monsters is
-- gone (their tiles became floor 6) and then opens doors / changes tiles.
-- Ported: floors 2, 8 and 10.  Other floors with a case fail closed;
-- floors without one do nothing.  No timer2on: in the original the battle
-- timeline is still running.

local function tile_sum(list)
    local n = 0
    for _, i in ipairs(list) do n = n + get_tile(i) end
    return n
end

local function mc_door(index, kind)
    doorstackin(index, kind)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
end

local MONCHECK = {}

MONCHECK[2] = function()               -- 0x4665a1
    if tile_sum({0x10, 0x12, 0x30}) ~= 0x11 then return end
    for _, i in ipairs({0x76, 0x72, 0x55, 0x51, 0x34, 0x30}) do
        mc_door(i, 6)
    end
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

MONCHECK[8] = function()               -- 0x466814
    if tile_sum({0x34, 0x36, 0x2a}) ~= 0x10 then return end
    mc_door(0x2a, 4)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

-- Floor 10 (0x466901): the eight skeleton soldiers dead and the gate 0x1b
-- still a door -> the gate opens, sub-event 24.  Then (0x466b0c) the Golden
-- Knight at 5 dead with the doors 71/40/36 still closed -> the room's
-- rewards appear (keys, gems), the fairy 0x17 at 0x1b, doors 0x47/0x28/
-- 0x24 open, sub-event 25.
local F10_REWARDS = {
    {0x5d, 7}, {0x73, 0x0b}, {0x2b, 0x1d}, {0x2a, 0x1d}, {0x29, 0x1d},
    {0x23, 0x21}, {0x22, 0x21}, {0x21, 0x21}, {0x20, 0x23}, {0x1f, 0x23},
    {0x1e, 0x23}, {0x18, 0x22}, {0x17, 0x22}, {0x16, 0x22}, {0x1b, 0x17},
}

MONCHECK[10] = function()
    if tile_sum({37, 38, 39, 48, 50, 59, 60, 61, 27}) == 0x34 then
        if st(ST_SOUND) ~= 0 then
            emit(OP.NOP, 0, 0); emit(OP.SOUND, 5, 0)
        end
        mc_door(0x1b, 4)
        emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
        emit(OP.START_SUBEVENT, 0x0e, 0)
        emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    end
    if tile_sum({5, 71, 40, 36}) ~= 0x12 then return end
    emit(OP.NOP, 0, 0); emit(OP.SET_TILE, F10_REWARDS[1][1], F10_REWARDS[1][2])
    for i = 2, #F10_REWARDS do
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, F10_REWARDS[i][1], F10_REWARDS[i][2])
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    end
    mc_door(0x47, 4)
    mc_door(0x28, 4)
    mc_door(0x24, 4)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, 0x0f, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 6, 0)
    end
end

-- Floor 40 (0x46a2d3): after tile 5 + tile 16 sum to 0x11, one-shot
-- state[0x96] opens the final room's doors/walls (13 tile writes with sound 2).
MONCHECK[40] = function()
    if get_tile(0x10) + get_tile(0x05) ~= 0x11 then return end
    if st(ST_FLOOR40_DONE) ~= 0 then return end
    set_state(ST_FLOOR40_DONE, 1)
    local tiles = {
        {0x29, 0x1d}, {0x28, 0x1d}, {0x27, 0x1d},
        {0x25, 0x21}, {0x24, 0x21}, {0x23, 0x21},
        {0x14, 0x23}, {0x13, 0x23}, {0x12, 0x23},
        {0x0e, 0x22}, {0x0d, 0x22}, {0x0c, 0x22},
        {0x3c, 0x17},
    }
    for _, t in ipairs(tiles) do
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, t[1], t[2])
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    end
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 6, 0)
    end
end

local MONCHECK_CASES = {
    [11]=1, [14]=1, [15]=1, [17]=1, [20]=1, [25]=1, [30]=1, [32]=1, [33]=1,
    [34]=1, [35]=1, [38]=1, [41]=1, [44]=1, [45]=1, [48]=1, [49]=1,
    [50]=1,
}

builders.moncheck = function()
    local floor = st(ST_FLOOR)
    local h = MONCHECK[floor]
    if h then return h() end
    if MONCHECK_CASES[floor] then
        fail(string.format("moncheck floor %d", floor))
    end
end

---------------------------------------------------------------------------
-- taisen2 (0x47f7d4): timeline sub-event 0xb/10.  The 0xb command's arg2
-- (aux.target_index, set by the VM splice) names the monster tile.  The
-- builder computes dameiji2's turn count and appends the battle animation;
-- the VM applies op 0x04/0x05/0x07/0x0b during the drain.  The gaining
-- branch also schedules moncheck (0xb/5/5) and the gold reward; the losing
-- branch leaves battle_render_flag set for the death timeline.
local function taisen_monster(raw)
    if ((raw - 0x3d) & 0xff) < 0x23 then
        return ((raw + 1) // 2) - 31
    end
    if ((raw + 0x7c) & 0xff) < 0x1a then
        return ((raw + 1) // 2) - 47
    end
    return nil
end

builders.taisen2 = function()
    local target = aux.target_index
    local index = taisen_monster(get_tile(target))
    if not index then
        fail("taisen2: target tile is not a monster")
    end
    local diff = st(ST_OPTION_A)
    local hp_total = monster_stat(index, "hp") * (diff + 1)
    local mdef = monster_stat(index, "defense")
    local matk = monster_stat(index, "attack")
    local gold = monster_stat(index, "gold")

    aux.event_selector = index
    aux.current_monster = index
    aux.monster_hp = hp_total
    set_state(ST_BATTLE_RENDER, 1)
    present("monshyouji", target, index)

    -- dameiji2 / damei3: turns to kill the monster.
    local attack = st(ST_ATTACK)
    if mdef * (diff + 1) >= attack then
        fail("taisen2: monster defense blocks every attack")
    end
    local hp = hp_total
    local turns = 0
    while hp > 0 do
        hp = mdef * (diff + 1) + (hp - attack)
        turns = turns + 1
    end
    aux.battle_turns = turns
    local defense = st(ST_DEFENSE)
    local battle_damage = 0
    if matk * (diff + 1) > defense then
        battle_damage = (matk * (diff + 1) - defense) * turns
    end
    present("battle_forecast", index, turns, battle_damage)

    local p = st(ST_Y) * MAP_COLS + st(ST_X)
    local tile = get_tile(p)
    local facing = (st(ST_FACING) - 1) * 8

    if battle_damage < st(ST_LIFE) then
        -- Player survives: reward, moncheck, and the winning animation.
        set_state(ST_BATTLE_RENDER, 0)
        emit(OP.SUBDISPATCH, SUB.MONCHECK, 5)                -- moncheck splice
        emit(OP.ADD_GOLD, gold, 0)
        emit(OP.SPRITE, 0, 0)
        aux.sprite = 1
        present("monswork", index)
        emit(OP.SET_TILE_SILENT, p, 6)
        emit(OP.NOP, 0, 0)
        emit(OP.SUBDISPATCH, SUB.PLAYER_ANIM, 0)                -- yusyahyouji1
        for i = 1, turns do
            aux.event_sprite = i
            emit(OP.NOP, 0, 0)
            emit(OP.MONSTER_DELTA, target, 0xc1)
            emit(OP.NOP, tile, 0xb8)
            emit(OP.MASKED_TILE, target, 0xb7)
            emit(OP.NOP, 0, 0)
            emit(OP.SOUND, 3, 0)
            emit(OP.NOP, 0, 0)
            emit(OP.LIFE_DELTA, target, 0)
            emit(OP.NOP, tile, facing + 0xbf)
            emit(OP.MASKED_TILE, target, facing + 0xc0)
            emit(OP.NOP, 0, 0)
            emit(OP.SOUND, 3, 0)
        end
    else
        -- The player dies: ydamei3 counts the losing turns.
        local idelta = matk * (diff + 1) - defense
        if idelta <= 0 then
            fail("taisen2: losing battle never terminates")
        end
        hp = st(ST_LIFE)
        turns = 0
        while hp > 0 do
            hp = hp - matk * (diff + 1) + defense
            turns = turns + 1
        end
        aux.battle_turns = turns
        emit(OP.NOP, 0, 0)
        emit(OP.ALTAR, 0, 0)
        for _ = 1, 19 do emit(OP.NOP, 0, 0) end
        for i = 1, turns do
            aux.event_sprite = i
            if i > 1 then
                emit(OP.NOP, 0, 0)
                emit(OP.MONSTER_DELTA, target, 0)
                emit(OP.NOP, tile, 0xb8)
                emit(OP.MASKED_TILE, target, 0xb7)
                emit(OP.NOP, 0, 0)
                emit(OP.SOUND, 3, 0)
                emit(OP.NOP, 0, 0)
            end
            emit(OP.LIFE_DELTA, target, i == 1 and tile or 0xfa)
            emit(OP.NOP, tile, facing + 0xbf)
            emit(OP.MASKED_TILE, target, facing + 0xc0)
            emit(OP.NOP, 0, 0)
            emit(OP.SOUND, 3, 0)
        end
    end
    timer2on()
end

---------------------------------------------------------------------------
-- ichicheck (0x46cb1c): floor 10..49 event dispatcher, called from mevent
-- after the player stepped onto the floor's trigger tile.  Table 0x46cb63
-- (index floor - 10) selects a handler; the common exit 0x4770a1 calls
-- timer2on when anything was queued.  Only floor 10 is ported.

-- Floor 10 (0x46cbc7), area 1's boss room.  Straight-line code, transcribed
-- from the listing (parity: floor10* vectors).  Executed tail first:
--   * state[0x6a] == 0 (first time): sub-event 23; the walls at 59 and 61
--     open (op 0x06, sound 2); the actor at 38 walks up to 5; doors close
--     at 27 and 71 (door kind 5 -> tile 4); doors 36 and 40 open (kind 4);
--     40 single-tile moves (sound 4 each) bring the side-room guards to
--     the middle; doors 36 and 40 close again.  With sound on, BGM 0xf
--     (state[0x5a]) at the start and sound 6 at the end.
--   * state[0x6a] != 0 (set by Button1Click sub-event 26): tile 0x63
--     becomes the thief 0x15, who walks from 0x58 round to 0x68; sub-event
--     27; then tile 0x68 is cleared.
local ST_FLOOR10_DONE = 0x6a           -- 0x4b8830

local function f10_door(index, kind)
    doorstackin(index, kind)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
end

local function f10_step(from, to)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    emit(OP.SOUND, 4, 0); emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, from, to)
end

local F10_MARCH = {
    {40,39}, {41,40}, {42,41}, {43,42}, {32,43}, {36,37}, {35,36}, {34,35},
    {33,34}, {22,33}, {39,50}, {40,39}, {41,40}, {42,41}, {31,42}, {37,48},
    {36,37}, {35,36}, {34,35}, {23,34}, {50,61}, {39,50}, {40,39}, {41,40},
    {30,41}, {48,59}, {37,48}, {36,37}, {35,36}, {24,35}, {39,38}, {40,39},
    {41,40}, {42,41}, {59,60}, {48,59}, {37,48}, {36,37}, {35,36}, {34,35},
}

local F10_THIEF_WALK = {
    {0x67,0x68}, {0x72,0x67}, {0x71,0x72}, {0x70,0x71}, {0x65,0x70},
    {0x5a,0x65}, {0x4f,0x5a}, {0x4e,0x4f}, {0x4d,0x4e}, {0x58,0x4d},
    {0x63,0x58},
}

local function ichicheck_floor10()
    if st(ST_FLOOR10_DONE) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 0x68, 6)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
        emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x11, 0)
        for _, m in ipairs(F10_THIEF_WALK) do
            emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, m[1], m[2])
        end
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 0x63, 0x15)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
        return
    end
    if st(ST_SOUND) ~= 0 then
        set_state(ST_SOUND, 0x0f)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 5, 0)
    end
    f10_door(40, 5)
    f10_door(36, 5)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    for _, m in ipairs(F10_MARCH) do f10_step(m[1], m[2]) end
    f10_door(40, 4)
    f10_door(36, 4)
    f10_door(71, 5)
    f10_door(27, 5)
    emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 16, 5)
    emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 27, 16)
    emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 38, 27)
    emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 61, 6)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 59, 6)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 0x0d, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 6, 0)
    end
end

local function ichicheck_floor15()
    doorstackin(0x5d, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 5)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor25()
    present("status_text", 0xda)
    emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, 0x17, 0)
    doorstackin(0x68, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor30()
    doorstackin(0x47, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    doorstackin(0x26, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor38()
    emit(OP.NOP, 0, 0); emit(OP.SPRITE, 0x72, 0)
    emit(OP.NOP, 0, 0)
    doorstackin(0x2d, 8)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor48()
    emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 0, 0x5d)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor33()
    if st(ST_Y) * MAP_COLS + st(ST_X) == 0x6b then
        emit(OP.NOP, 0, 0); emit(OP.SPRITE, 0x72, 0)
        emit(OP.NOP, 0, 0)
        doorstackin(0x6a, 8)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
        emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    else
        doorstackin(0x56, 5)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
        doorstackin(0x2a, 5)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
        emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    end
end

local function ichicheck_floor43()
    if get_tile(8) ~= 0x5d then
        emit(OP.NOP, 0, 0)
        return
    end
    emit(OP.NOP, 0, 0)
    doorstackin(9, 8)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    doorstackin(6, 8)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    for _ = 1, 5 do emit(OP.NOP, 0, 0) end
    emit(OP.MONSTER_MOVE, 9, 10)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    for _ = 1, 5 do emit(OP.NOP, 0, 0) end
    emit(OP.MONSTER_MOVE, 8, 9)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
end

local function ichicheck_floor49()
    present("status_text", 0xda)
    emit(OP.NOP, 0, 0)
    emit(OP.SUBDISPATCH, SUB.MADOUSHI2, 0)
    emit(OP.NOP, 0, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 5, 0)
    end
    emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, 45, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    emit(OP.SUBDISPATCH, SUB.ZENOLIVEWORK, 0)
    doorstackin(0x47, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 6, 0)
    end
end

local function ichicheck_floor42()
    set_tile_at(35, 103, 6)
    emit(OP.NOP, 0, 0)
    emit(OP.SET_TILE, 60, 6)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    for _ = 1, 7 do emit(OP.NOP, 0, 0) end
    emit(OP.START_SUBEVENT, 44, 0)
    for _ = 1, 6 do emit(OP.NOP, 0, 0) end
    for _, t in ipairs({83, 81, 93, 71}) do
        emit(OP.SET_TILE, t, 6)
    end
    for _ = 1, 7 do emit(OP.NOP, 0, 0) end
    emit(OP.SET_TILE, 82, 6)
    for _ = 1, 4 do emit(OP.NOP, 0, 0) end
    emit(OP.NOP, get_tile(0x52), 0xb8)
    emit(OP.MASKED_TILE, 82, 0xb7)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 4, 0)
    emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, 43, 0)
    aux.event_sprite = 0x47
    aux.event_selector = 0x5d
    aux.event_param = 0x51
    aux.roujin_selector = 0x53
    call("madoushilivework")
    for _ = 1, 5 do emit(OP.NOP, 0, 0) end
    emit(OP.START_SUBEVENT, 42, 0)
    for _ = 1, 5 do emit(OP.NOP, 0, 0) end
    emit(OP.START_SUBEVENT, 41, 0)
    aux.event_sprite = 0x3c
    call("zenolivework")
    for _ = 1, 6 do emit(OP.NOP, 0, 0) end
    timer2on()
end

local function ichicheck_floor20()
    present("status_text", 0xda)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 5)
    end
    for _, i in ipairs({0x30, 0x31, 0x32, 0x3b, 0x3d, 0x46, 0x47, 0x48}) do
        set_tile(i, RAW_FLOOR)
    end
    set_tile(0x3c, 0x5f)
    emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, 20, 0)
    for stage = 32, 1, -1 do
        emit(OP.NOP, 0, 0)
        emit(OP.SUBDISPATCH, SUB.BANPAIAWORK, stage)
    end
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 8, 0)
    doorstackin(93, 5)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    if st(ST_SOUND) ~= 0 then
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 6, 0)
    end
end

local function ichicheck_floor32()
    present("status_text", 0xda)
    emit(OP.NOP, 0, 0)
    emit(OP.SUBDISPATCH, SUB.TAISEN2, 104)
    local moves = {
        {93,104}, {82,93}, {71,82}, {60,71},
    }
    for _, m in ipairs(moves) do
        for _ = 1, 3 do emit(OP.NOP, 0, 0) end
        emit(OP.MONSTER_MOVE, m[1], m[2])
    end
    for _ = 1, 3 do emit(OP.NOP, 0, 0) end
    emit(OP.START_SUBEVENT, 30, 0)
    for _ = 1, 7 do emit(OP.NOP, 0, 0) end
    local more = {
        {49,60}, {38,49}, {27,38}, {16,27}, {5,16},
        {6,5}, {7,6}, {8,7}, {9,8},
    }
    for i, m in ipairs(more) do
        if i > 1 then
            for _ = 1, 3 do emit(OP.NOP, 0, 0) end
        end
        emit(OP.MONSTER_MOVE, m[1], m[2])
    end
    for _ = 1, 3 do emit(OP.NOP, 0, 0) end
    emit(OP.SET_TILE, 9, 0x85)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    for _ = 1, 10 do emit(OP.NOP, 0, 0) end
end

local ICHICHECK = {
    [10] = ichicheck_floor10,
    [15] = ichicheck_floor15,
    [20] = ichicheck_floor20,
    [25] = ichicheck_floor25,
    [30] = ichicheck_floor30,
    [32] = ichicheck_floor32,
    [33] = ichicheck_floor33,
    [38] = ichicheck_floor38,
    [42] = ichicheck_floor42,
    [43] = ichicheck_floor43,
    [48] = ichicheck_floor48,
    [49] = ichicheck_floor49,
}

builders.ichicheck = function()
    local floor = st(ST_FLOOR)
    if floor >= 10 and floor <= 49 then
        local h = ICHICHECK[floor]
        if h then
            h()
        elseif floor == 24 or floor == 40 then
            fail(string.format("ichicheck floor %d", floor))
        end
    end
    if get_count() >= 1 then timer2on() end   -- 0x4770a1
end
builders.floor10 = builders.ichicheck   -- vector name (entry 0x46cb1c)

---------------------------------------------------------------------------
-- yousei (0x478f0c): the fairy at the end of an area (floors 10, 20, 25,
-- 40, 49).  Computes the area score (aux.event_sprite, state[0x55]), the
-- rank against the area's ranking table (state[0x56], 0 = not ranked),
-- the area id (state[0x57]), counts the visit (state[0xe]), writes the
-- save0.dat checkpoint, shows the result text and queues sub-event
-- 0x10/0x16/0x18/0x28/0x2f (Button1Click 26.. -> the area flag).
--
-- The ranking table comes from optionN.dat; when that file is missing or
-- fails its checksums the original uses the built-in temporary ranking
-- below (after a ShowMessage).  The remake does not keep ranking files yet,
-- so it always uses the temporary ranking (and shows no message).
local ST_VISITS, ST_SCORE, ST_RANK, ST_AREA = 0x0e, 0x55, 0x56, 0x57
local ST_KEYS_Y, ST_KEYS_R, ST_KEYS_B = 8, 9, 10
local ST_DIFF = 159

-- per area: rows of { score, life, attack + defense }
local RANKING = {
    [1] = { {100,500,25}, {90,450,24}, {80,400,23}, {70,350,22}, {60,300,21},
            {50,250,20}, {40,200,19}, {30,150,18}, {20,100,17}, {10,50,16} },
    [2] = { {100,1000,50}, {90,900,45}, {80,800,40}, {70,700,35},
            {60,600,30}, {50,500,25}, {40,400,20}, {30,300,15}, {20,200,10},
            {10,100,5} },
    [3] = { {100,10000,500}, {90,9000,450}, {80,8000,400}, {70,7000,350},
            {60,6000,300}, {50,5000,250}, {40,4000,200}, {30,3000,150},
            {20,2000,100}, {10,1000,50} },
    [4] = { {100,5000,200}, {90,4500,190}, {80,4000,180}, {70,3500,170},
            {60,3000,160}, {50,2500,150}, {40,2000,140}, {30,1500,130},
            {20,1000,120}, {10,500,110} },
}
RANKING[5] = RANKING[3]

-- floor -> area, the cleared-gate index (op 0x06 tile 6), sub-event param
local FAIRY = {
    [10] = { 1, 0x1b, 0x10 }, [20] = { 2, 0x31, 0x16 },
    [25] = { 3, 0x47, 0x18 }, [40] = { 4, 0x3c, 0x28 },
    [49] = { 5, 0x10, 0x2f },
}

local function fairy_score(floor)
    local d = st(ST_DIFF) + 1
    local life, atk, def = st(ST_LIFE), st(ST_ATTACK), st(ST_DEFENSE)
    local keys = st(ST_KEYS_Y) + st(ST_KEYS_B) * 2 + st(ST_KEYS_R) * 3
    if floor == 10 then
        return idiv(life - d * 400, 50) + 60 + atk * 3 - d * 157 + def * 2 + keys
    elseif floor == 20 then
        return idiv(life - d * 800, 100) + 60 + atk * 2 - d * 228 + def + keys
    elseif floor == 25 or floor == 49 then
        return idiv(life - d * 3000, 400) + 60 + idiv(atk - d * 240, 2) +
               idiv(def - d * 300, 3) + keys
    elseif floor == 40 then
        return idiv(life - d * 1600, 200) + 60 + atk - d * 152 +
               idiv(def - d * 198, 2) + keys
    end
    return 0
end

builders.yousei = function()
    present("button_caption")
    if st(ST_SOUND) ~= 0 then
        set_state(ST_SOUND, 0x0c)
        present("bgm", 0x0c)
    end
    set_state(ST_VISITS, st(ST_VISITS) + 1)
    local floor = st(ST_FLOOR)
    local score = fairy_score(floor)
    if score < 0 then score = 0 end
    aux.event_sprite = score
    set_state(ST_SCORE, score)
    set_state(ST_RANK, 0)
    local f = FAIRY[floor]
    local rows = f and RANKING[f[1]] or RANKING[1]
    local life, ad = st(ST_LIFE), st(ST_ATTACK) + st(ST_DEFENSE)
    for r, row in ipairs(rows) do
        if score > row[1] or (score == row[1] and
            (life > row[2] or (life == row[2] and ad - row[2] > row[3]))) then
            set_state(ST_RANK, r)
            break
        end
    end
    if f then set_state(ST_AREA, f[1]) end
    present("checkpoint")              -- save0.dat: map + state
    aux.sprite = 1
    present("fairy_text", st(ST_RANK), score, st(ST_VISITS))
    if st(ST_SOUND) ~= 0 then emit(OP.STOP_BGM, 0, 0) end
    local gate = f and f[2] or 0
    if f then
        aux.ui_selector = (aux.ui_selector & 0xffff0000) | f[3]
    end
    emit(OP.SET_TILE, gate, 6)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    emit(OP.START_SUBEVENT, aux.ui_selector & 0xffff, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    timer2on()
end

---------------------------------------------------------------------------
-- handan (0x444490): tile-action dispatcher on the raw tile at
-- aux.target_index (switch 0x4444e3).  Ported cases: 0x06 floor (walk),
-- 0x07 map event, 0x09 secret wall, 0x13/0x14 merchant (syounin), 0x15/0x16
-- thief (touzoku) and 0x19/0x1a old man (roujin).  Everything else is still
-- handled by the legacy C path.

-- handan case 9 (0x4447df): the brown secret wall (raw 0x09; floor 5
-- (8,10), floor 9 (9,4)).  Status text ListBox2[0x89], sprite 1, then the
-- wall slides down like the prison cell (door kind 7 at the target, 16
-- frames from doorstackin; dooropen writes the floor tile) with sound 1.
-- The player does not move.
local RAW_SECRET_WALL = 0x09

local function secret_wall()
    timer2on()
    aux.sprite = 1
    present("status_text", 0x89)
    emit(OP.NOP, 0, 0); emit(OP.SPRITE, 1, 0)
    emit(OP.NOP, 0, 0)
    doorstackin(aux.target_index, 7)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 1, 0)
end

builders.handan = function()
    local raw = get_tile(aux.target_index)
    if raw == RAW_FLOOR then
        return call("idou")
    elseif raw == RAW_ZENO_TRIGGER then
        return call("mevent")
    elseif raw == RAW_SECRET_WALL then
        return secret_wall()
    elseif raw == 0x10 then
        return call("altar")           -- 0x44509b
    elseif raw == 0x13 or raw == 0x14 then
        return call("syounin")         -- 0x445363
    elseif raw == 0x15 or raw == 0x16 then
        return call("touzoku")         -- 0x445370
    elseif raw == 0x17 or raw == 0x18 then
        return call("yousei")          -- 0x44537d
    elseif raw == 0x19 or raw == 0x1a then
        return call("roujin")          -- 0x44538a
    end
    fail(string.format("handan raw 0x%02x", raw))
end

---------------------------------------------------------------------------
-- roujin (0x449708): the old men.  Floor switch 0x449767 / 0x449798 sets
-- the dialogue selector (aux.message_selector, 0x48c5a0) and the event
-- selector (aux.event_selector, 0x48c55c).  The event selector is also the
-- state index of that old man's "talked to" flag: 1, or 2 with the Orb of
-- Wisdom (15-event-flags.md section 5).

local ROUJIN = {
    [3] = { 0x1a, 0x43 }, [4] = { 0x00, 0x44 }, [6] = { 0x01, 0x45 },
    [0x12] = { 0x07, 0x48 }, [0x15] = { 0x08, 0x49 }, [0x1b] = { 0x09, 0x4a },
    [0x1f] = { 0x0a, 0x4b }, [0x21] = { 0x0c, 0x4c }, [0x24] = { 0x0d, 0x4d },
    [0x25] = { 0x0e, 0x4e }, [0x27] = { 0x10, 0x4f }, [0x2a] = { 0x12, 0x50 },
    [0x2d] = { 0x13, 0x51 }, [0x2e] = { 0x15, 0x52 }, [0x30] = { 0x17, 0x53 },
}

builders.roujin = function()
    present("message_panel_clear")
    itemdel()
    aux.roujin_aux = 0                  -- g_subevent_id 0x48c5a4
    aux.roujin_selector = 0             -- g_battle_unwinnable 0x48c564
    local floor = st(ST_FLOOR)

    if floor == 2 then
        aux.message_selector = 0x19
        aux.event_selector = 0x42
        if st(ST_ROUJIN_F2) == 0 then
            -- one-shot +3% attack/defense (0x449805)
            set_state(ST_ROUJIN_F2, 1)
            set_state(ST_ATTACK, st(ST_ATTACK) + idiv(st(ST_ATTACK) * 3, 100))
            set_state(ST_DEFENSE, st(ST_DEFENSE) + idiv(st(ST_DEFENSE) * 3, 100))
            emit(OP.NOP, 0, 0)
            emit(OP.SPRITE, 0, 0)
            emit(OP.NOP, 0, 0)
            emit(OP.SOUND, 2, 0)
            emit(OP.NOP, 0, 0)
            emit(OP.NOP, 0, 0)
            timer2on()
        end
    elseif floor == 0x10 then
        if st(ST_X) == 9 then
            aux.message_selector = 0x1b
            aux.event_selector = 0x47
        else
            aux.message_selector = 0x06
            aux.event_selector = 0x46
        end
    elseif floor == 0x17 then
        aux.roujin_selector = 1
        emit(OP.NOP, 0, 0)
        emit(OP.START_SUBEVENT, 0x3d, 0)
        emit(OP.NOP, 0, 0)
        emit(OP.NOP, 0, 0)
        timer2on()
    elseif ROUJIN[floor] then
        aux.message_selector = ROUJIN[floor][1]
        aux.event_selector = ROUJIN[floor][2]
    end

    if aux.roujin_selector == 1 then
        return
    end
    if aux.message_selector == 0x1b and st(ST_ROUJIN_SUB1B) == 0 then
        set_state(ST_SPECIAL0 + SLOT_MAGIC_ELIXIR, 1)
        set_state(ST_ROUJIN_SUB1B, 1)
        emit(OP.NOP, 0, 0)
        emit(OP.SHOW_ITEMS, 0, 0)
        emit(OP.NOP, 0, 0)
        emit(OP.SOUND, 2, 0)
        emit(OP.NOP, 0, 0)
        emit(OP.NOP, 0, 0)
        timer2on()
    end
    local flag = aux.event_selector
    local seen = st(flag)
    if seen == 0 then
        if slot(SLOT_ORB_OF_WISDOM) == 0 then
            set_state(flag, 1)
        elseif slot(SLOT_ORB_OF_WISDOM) == 1 then
            set_state(flag, 2)
        end
        zyouout()
        if floor == 3 then
            set_state(ST_SPECIAL0 + SLOT_ORB_OF_HERO, 2)
            emit(OP.NOP, 0, 0)
            emit(OP.SHOW_ITEMS, 0, 0)
            emit(OP.NOP, 0, 0)
            emit(OP.SOUND, 2, 0)
            emit(OP.NOP, 0, 0)
            emit(OP.SPRITE, 0x0b, 0)
            timer2on()
        end
    elseif seen == 1 or seen == 2 then
        aux.message_selector = 0x18
        zyouout()
    end
end

---------------------------------------------------------------------------
-- syounin (0x449014): the merchants (raw 0x13/0x14).  Floor switch sets the
-- offer: message_selector (g_dialog_selector), event_sprite (offer text
-- ListBox2[0xf6 + n]), shop_flag / shop_talk_flag (state indices), price
-- and battle_turns (thank-you text + 1).  Branches that do not assign a
-- field keep its previous value, as in the original.
--   flag 0: RichEdit1 shows the offer, Button2 (Yes) and Button7 (No);
--           on floor 2 No is disabled (the rescued man's gift).
--   flag 1, talk flag 0: the thank-you text with Button1 (OK); the talk
--           flag becomes 1, or 2 with the Orb of Wisdom.
--   flag 1, talk flag 1..2: zyouout with selector 0x1d (floor 2) / 0x1c.
-- The answer runs Button2Click / Button7Click below.

local ST_GOLD = 3
local ST_KEY_YELLOW, ST_KEY_RED, ST_KEY_BLUE = 8, 9, 10
local SLOT_SUPER_MATTOCK = 13          -- 0x4b8700

-- floor: { selector, offer, flag, talk, price, thanks }; nil = unchanged
local SHOP = {
    [2]    = { 0x1d, 0,  0x2a, 0x36, 0,    0x5c },
    [6]    = { 0x02, 1,  0x2b, 0x37, 0x32, 0x41 },
    [7]    = { 0x03, 2,  0x2c, 0x38, 0x32, 0x42 },
    [0x0f] = { 0x05, 5,  0x2f, 0x3b, 200,  0x44 },
    [0x1c] = { nil,  6,  0x30, nil,  0,    nil  },
    [0x1f] = { 0x0b, 7,  0x31, 0x3d, 1000, 0x4a },
    [0x26] = { 0x0f, 8,  0x32, 0x3e, 200,  0x4e },
    [0x27] = { 0x11, 9,  0x33, 0x3f, 2000, 0x50 },
    [0x2d] = { 0x14, 10, 0x34, 0x40, 1000, 0x53 },
    [0x2f] = { 0x16, 11, 0x35, 0x41, 4000, 0x55 },
}
local SHOP_F12 = {
    [1] = { 0x04, 3, 0x2d, 0x39, 800, 0x43 },
    [9] = { nil,  4, 0x2e, nil,  1000, nil },
}

builders.syounin = function()
    present("button_caption")          -- Button1.Caption := 'OK' (0x449704)
    itemdel()
    present("richedit_layout")         -- RichEdit1/Button bounds
    aux.choice_mode = 0
    aux.roujin_aux = 0
    local floor = st(ST_FLOOR)
    local t = SHOP[floor]
    if floor == 0x0c then t = SHOP_F12[st(ST_X)] end
    if t then
        if t[1] then aux.message_selector = t[1] end
        aux.event_sprite = t[2]
        aux.shop_flag = t[3]
        if t[4] then aux.shop_talk_flag = t[4] end
        aux.shop_price = t[5]
        if t[6] then aux.battle_turns = t[6] end
    end
    aux.event_param = 0
    local bought = st(aux.shop_flag)
    if bought == 0 then
        if aux.event_sprite < 0x0c then
            present("richedit_text", aux.event_sprite + 0xf6)
        end
        present("richedit_show", 1)
        present("choice_show", floor == 2 and 0 or 1)
    elseif bought == 1 then
        local talk = st(aux.shop_talk_flag)
        if talk == 0 then
            present("richedit_text", aux.battle_turns - 1)
            if slot(SLOT_ORB_OF_WISDOM) == 0 then
                set_state(aux.shop_talk_flag, 1)
            elseif slot(SLOT_ORB_OF_WISDOM) == 1 then
                set_state(aux.shop_talk_flag, 2)
            end
            present("richedit_show", 1)
            present("button1_show")
        elseif talk == 1 or talk == 2 then
            aux.message_selector = floor == 2 and 0x1d or 0x1c
            zyouout()
        end
    end
end

-- Button2Click (0x44e1cc): "Yes".  choice_mode 0 is the merchant; mode 1
-- (the altar menu) is not ported yet and fails closed.
local function shop_done(text)
    present("richedit_text", text)
    present("richedit_show", 1)
    present("button1_show")
end

local function shop_paid()
    shop_done(0x103)
    emit(OP.SPRITE, 0, 0); emit(OP.SHOW_ITEMS, 0, 0)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
    timer2on()
end

local SHOP_REWARD = {
    [0x2a] = function() set_state(ST_GOLD, st(ST_GOLD) + 1000) end,
    [0x2b] = function() set_state(ST_KEY_BLUE, st(ST_KEY_BLUE) + 1) end,
    [0x2c] = function() set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) + 5) end,
    [0x2d] = function() set_state(ST_KEY_RED, st(ST_KEY_RED) + 1) end,
    [0x2e] = function() set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) + 1) end,
    [0x2f] = function() set_state(ST_KEY_BLUE, st(ST_KEY_BLUE) + 1) end,
    [0x30] = function()
        set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) - 1)
        set_state(ST_GOLD, st(ST_GOLD) + 100)
    end,
    [0x31] = function()
        set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) + 4)
        set_state(ST_KEY_BLUE, st(ST_KEY_BLUE) + 1)
    end,
    [0x32] = function() set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) + 3) end,
    [0x33] = function() set_state(ST_KEY_BLUE, st(ST_KEY_BLUE) + 3) end,
    [0x34] = function() set_state(ST_LIFE, st(ST_LIFE) + 2000) end,
    [0x35] = function() set_state(ST_SPECIAL0 + SLOT_SUPER_MATTOCK, 1) end,
}

builders.button2click = function()
    present("choice_hide")             -- Button2/Button7 hidden, No enabled
    present("richedit_text", -1)
    local mode = aux.choice_mode & 0xffff
    if mode == 0 then present("richedit_show", 0) end
    present("disp")
    if mode == 0 then
        if st(ST_FLOOR) == 0x1c then   -- the key buyer
            aux.roujin_aux = 0
            if st(ST_KEY_YELLOW) == 0 then
                return shop_done(0x106)
            end
            set_state(ST_KEY_YELLOW, st(ST_KEY_YELLOW) - 1)
            set_state(ST_GOLD, st(ST_GOLD) + 100)
            return shop_paid()
        end
        if st(ST_GOLD) < aux.shop_price then
            shop_done(0x102)
            aux.roujin_aux = 0
            return
        end
        set_state(ST_GOLD, st(ST_GOLD) - aux.shop_price)
        aux.roujin_aux = 0
        local f = aux.shop_flag
        if (f >= 0x2a and f <= 0x2d) or f == 0x2f or (f >= 0x31 and f <= 0x35) then
            set_state(f, 1)
        end
        if SHOP_REWARD[f] then SHOP_REWARD[f]() end
        return shop_paid()
    elseif mode == 1 then
        -- the altar's menu: RichEdit1 = ListBox2[0xcc]; Button39..41 show
        -- the life/attack/defense offers, Button42 cancels (and has focus)
        local d = st(ST_DIFF) + 1
        local area = idiv(st(ST_FLOOR) + 10, 10)
        present("altar_menu", (st(ST_ALTAR) + 1) * 100 * d, area * 2 * d,
                area * 4 * d)
    end
end

---------------------------------------------------------------------------
-- The altar (handan case 0x10, 0x44509b): the n-th offering costs
-- 10 * (n*n - n + 2) gold (n = state[0xb] + 1).  RichEdit1 = ListBox2[0xca]
-- price ListBox2[0xcb], Yes/No (choice_mode 1).  Yes opens the menu above
-- (Button2Click); the options are Button39..42 below.  The player does not
-- step onto the altar.

builders.altar = function()
    aux.choice_mode = 1
    itemdel()
    local n = st(ST_ALTAR) + 1
    aux.shop_price = 10 * (n * n - n + 2)
    present("altar_offer", aux.shop_price)
    aux.roujin_aux = 2
    present("richedit_show", 1)
    present("choice_show", 1)
end

-- Button39/40/41Click (0x451e1c / 0x4520b4 / 0x452360): pay and raise life
-- by state[0xb]*100*(d+1) after the count went up, attack by
-- ((floor+10)/10)*2*(d+1) or defense by ((floor+10)/10)*4*(d+1); the
-- status bar shows ListBox2[0xb5/0xb6/0xb7] amount ListBox2[0xa6].  Not
-- enough gold: ListBox2[0xd3] with OK.
local function altar_buy(kind)
    present("altar_menu_hide")
    present("richedit_show", 0)
    if st(ST_GOLD) < aux.shop_price then
        present("richedit_text", 0xd3)
        aux.roujin_aux = 0
        present("richedit_show", 1)
        present("button1_show")
        return
    end
    aux.sprite = 1
    local d = st(ST_DIFF) + 1
    local area = idiv(st(ST_FLOOR) + 10, 10)
    local gain
    if kind == 1 then gain = (st(ST_ALTAR) + 1) * 100 * d
    elseif kind == 2 then gain = area * 2 * d
    else gain = area * 4 * d end
    present("status_concat", 0xb4 + kind, gain, 0xa6)
    set_state(ST_ALTAR, st(ST_ALTAR) + 1)
    set_state(ST_GOLD, st(ST_GOLD) - aux.shop_price)
    if kind == 1 then
        set_state(ST_LIFE, st(ST_LIFE) + st(ST_ALTAR) * 100 * d)
    elseif kind == 2 then
        set_state(ST_ATTACK, st(ST_ATTACK) + gain)
    else
        set_state(ST_DEFENSE, st(ST_DEFENSE) + gain)
    end
    timer2on()
    emit(OP.NOP, 0, 0); emit(OP.SPRITE, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
end

builders.button39click = function() altar_buy(1) end
builders.button40click = function() altar_buy(2) end
builders.button41click = function() altar_buy(3) end
builders.button42click = function()    -- 0x45260c: cancel
    present("altar_menu_hide")
    present("richedit_show", 0)
    itemlive()
end

-- Button7Click (0x44ea90): "No".
builders.button7click = function()
    present("choice_hide")
    itemlive()
    present("richedit_text", -1)
    present("richedit_show", 0)
    present("disp")
end

---------------------------------------------------------------------------
-- Button1Click (0x44da10): the player confirms the zyouout window.
-- aux.roujin_aux is g_subevent_id (0x48c5a4).  Every confirmed sub-event
-- 10 < id < 0x44 is recorded as state[0x50 + id] = 1 (0x44da2a): that is
-- how the floor-10 area-1 flag 0x6a is set (sub-event 26, the fairy).
-- Dispatch: table 0x44da4f (id < 0x1b), table 0x44dad3 (0x1b..0x32), then
-- compares.  Presentation (Button1/RichEdit1 visibility, itemlive, disp,
-- mhyouji) goes through present(); Timer2 is real engine state.
-- Case 20 calls openning2 (the capture cutscene, below).

local ST_DIFFICULTY = 159              -- option A (0x4b8904)
local ST_BATTLE_RENDER = 0x0c          -- set by openning31, cleared by 13

-- mhyouji (0x442c38): map redraw; it also clears state[0xa6] (0x442c45).
local function mhyouji()
    set_state(ST_STAIRS_ADJ, 0)
    present("mhyouji")
end

local function hide_button() present("button_hide") end
local function hide_dialog() present("dialog_hide") end
local function timer2(on) present("timer2", on and 1 or 0) end

local function confirm_resume()        -- case 0x17 (0x44e107)
    hide_button(); hide_dialog()
    timer2(true)
end

-- case 0x1a (the fairy's sub-event and 0x20/0x22/0x32/0x39): on difficulty
-- 0 the ranking form (DAT_0048a764) is shown modally (ShowModal 0x4237dc)
-- and Timer2 is NOT restarted: what is left of the timeline waits for the
-- next timer2on.  Otherwise Timer2 resumes.
local function confirm_0x1a()
    hide_button(); hide_dialog()
    aux.sprite = 0
    if st(ST_DIFFICULTY) == 0 then
        present("ranking_show")
    else
        timer2(true)
    end
end

local function confirm_hide_stop()     -- cases 0x11..0x13, 0x47, 0x112
    hide_button(); hide_dialog()
    timer2(false)
    itemlive()
end

local CONFIRM = {
    [0] = function()
        if slot(SLOT_ORB_OF_HERO) == 2 then
            set_state(ST_SPECIAL0 + SLOT_ORB_OF_HERO, 1)
        end
        itemlive()
    end,
    [1] = function() present("item_button_show") end,
    [0x0b] = function() hide_button(); timer2(true) end,
    [0x0c] = function() hide_button(); timer2(true) end,
    [0x0d] = function()                -- end of the opening
        hide_button()
        timer2(false)
        present("timer1", 1)
        set_state(ST_BATTLE_RENDER, 0)
        set_count(0)
        mhyouji()
        hide_dialog()
        itemlive()
    end,
    [0x0e] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 0x42, 0x4d)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 0x43, 0x42)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 0x44, 0x43)
    end,
    [0x0f] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        emit(OP.NOP, 0, 0); emit(OP.SET_TILE, 99, 6)
        emit(OP.NOP, 0, 0); emit(OP.SOUND, 2, 0)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 0x58, 99)
        emit(OP.NOP, 0, 0); emit(OP.MONSTER_MOVE, 0x4d, 0x58)
    end,
    [0x10] = function()
        hide_button(); hide_dialog()
        timer2(false)
        -- g_map[0x1138] = 0x15, g_map[0x112c] = 6.  g_map starts after the
        -- 2-byte map-block header: floor 35, indices 103 and 91.
        set_tile_at(35, 103, 0x15)
        set_tile_at(35, 91, 0x06)
        itemlive()
    end,
    [0x11] = confirm_hide_stop, [0x12] = confirm_hide_stop,
    [0x13] = confirm_hide_stop,
    [0x14] = function()                -- Zeno's speech -> openning2
        hide_button(); hide_dialog()
        call("openning2")
    end,
    [0x15] = function() hide_button(); hide_dialog(); timer2(true) end,
    [0x17] = confirm_resume, [0x18] = confirm_resume, [0x19] = confirm_resume,
    [0x1a] = confirm_0x1a,
}

local function confirm_high(id)        -- ids above 0x1a
    if id <= 0x32 then
        if id == 0x20 or id == 0x22 or id == 0x32 then return confirm_0x1a() end
        if id ~= 0x1a then return confirm_resume() end
        return
    end
    if id < 0x48 then
        if id == 0x47 then return confirm_hide_stop() end
        if id >= 0x33 and id <= 0x38 then return confirm_resume() end
        if id == 0x39 then return confirm_0x1a() end
        if id >= 0x3a and id <= 0x43 then return confirm_resume() end
        return
    end
    if id == 0x48 or id == 0x67 then
        hide_button(); hide_dialog()
        return timer2(true)
    end
    if id == 0x112 then return confirm_hide_stop() end
end

builders.button1click = function()
    local id = aux.roujin_aux
    if id > 10 and id < 0x44 then
        set_state(0x50 + id, 1)
    end
    if id > 0x1a then
        confirm_high(id)
    elseif CONFIRM[id] then
        CONFIRM[id]()
    end
    -- common tail (switchD_0044da48_caseD_2)
    hide_button()
    if id < 2 then hide_dialog() end
    present("disp")
end


---------------------------------------------------------------------------
-- The opening after Zeno's speech.  Button1Click case 20 -> openning2,
-- whose tail runs madoushilivework (the four Magic Sergeants) and whose
-- 0x0b/1 step runs openning31 (the capture).  P3b execution oracle:
-- docs/re/16-event-pilot.md section 8.


-- madoushilivework (0x461ef4): animate the actors at the four map indices
-- held in roujin_selector (0x48c564), event_param (0x48c560),
-- event_selector (0x48c55c) and event_sprite (0x48c558), frames
-- 0xb6, 0xaf, 0xab, 0xa9, 0xa7; then sound 7.
builders.madoushilivework = function()
    present("madoushi_setup")          -- off-screen bitmaps (0x48c51c..524)
    timer2on()
    local actors = {
        aux.roujin_selector & 0xffff, aux.event_param & 0xffff,
        aux.event_selector & 0xffff, aux.event_sprite & 0xffff,
    }
    for _, frame in ipairs({ 0xb6, 0xaf, 0xab, 0xa9, 0xa7 }) do
        for _, index in ipairs(actors) do
            emit(OP.NOP, 6, 0x5d)
            emit(OP.MASKED_TILE, index, frame)
        end
    end
    emit(OP.NOP, 0, 0)
    emit(OP.SOUND, 7, 0)
end

-- openning2 (0x464048): the capture cutscene.  openning9 frames
-- (0x0b/0xc; phase = previous command's arg1), title, the player's facing
-- frames at (4,8), sub-event 21, then the sergeants.
builders.openning2 = function()
    timer2on()
    present("button_caption", 0xdb)    -- Button1.Caption := ListBox2[0xdb]
    emit(OP.SUBDISPATCH, SUB.OPENNING31, 0)                   -- openning31 (runs last)
    emit(OP.NOP, 1, 0)
    local frames = TILE_PX == 0x20 and 400 or 500
    emit(OP.SUBDISPATCH, SUB.OPENNING9, frames)
    local n = TILE_PX == 0x20 and 0x28 or 0x32
    for i = n, 0, -1 do
        local base = i * 6
        for k = 5, 0, -1 do
            emit(OP.NOP, 1, 0)
            emit(OP.SUBDISPATCH, SUB.OPENNING9, (base + 0x64 + k) & 0xffff)
        end
    end
    emit(OP.NOP, 1, 0)
    emit(OP.SUBDISPATCH, SUB.OPENNING9, 0)
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    for frame = 0xf7, 0xe8, -1 do      -- open1 frames, each with sound 4
        emit(OP.SUBDISPATCH, SUB.OPENNING9, frame)
        emit(OP.NOP, 0, 0)
        emit(OP.SOUND, 4, 0)
        emit(OP.NOP, 0, 0)
    end
    emit(OP.SUBDISPATCH, SUB.HIDE_IMAGES, 0)                -- hide event images
    for _ = 1, 25 do emit(OP.NOP, 0, 0) end
    emit(OP.SUBDISPATCH, SUB.TITLE, 0)                -- title
    emit(OP.NOP, 0, 0)
    emit(OP.SHOW_ITEMS, 0, 0)
    emit(OP.NOP, 0, 0)
    emit(OP.SET_TILE, 0x5c, 6)                -- (4,8) is floor again
    emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0); emit(OP.NOP, 0, 0)
    local facing = st(0x58)
    local f1 = ({ [1] = 0xbf, [2] = 0xc7, [3] = 0xcf, [4] = 0xd7 })[facing]
    local f2 = ({ [1] = 0xc0, [2] = 0xc8, [3] = 0xd0, [4] = 0xd8 })[facing]
    if not f1 then
        fail(string.format("openning2: facing %d", facing))
    end
    emit(OP.NOP, 6, f1)
    emit(OP.MASKED_TILE, 0x5c, f2)
    emit(OP.NOP, 0, 0)
    emit(OP.SOUND, 4, 0)
    for _ = 1, 5 do emit(OP.NOP, 0, 0) end
    emit(OP.START_SUBEVENT, 0x0b, 0)                -- sub-event 21: "What? Oh!"
    aux.event_sprite = 0x51            -- 0x48c558: (4,7)
    aux.event_selector = 0x67          -- 0x48c55c: (4,9)
    aux.event_param = 0x5b             -- 0x48c560: (3,8)
    aux.roujin_selector = 0x5d         -- 0x48c564: (5,8)
    call("madoushilivework")
end

-- openning31 (0x4653b8): the capture, timeline sub-dispatch 0x0b/1.  Normal
-- run: life/attack/defense 400/10/10; second ("R") run: all special items
-- are taken instead.  Always: Sacred gear and display levels cleared,
-- floor 2 at (2,7) with floor limit 2, state[0xc] = 1; then the wake-up
-- dialogues (sub-events 11, 11, 12, 13) and a player redraw.
builders.openning31 = function()
    if st(ST_SOUND) ~= 0 then present("media_stop") end
    present("panels_restore", st(ST_DIFFICULTY))   -- center/left(r)/right(r)
    present("button_caption_clear")
    if st(ST_DIFFICULTY) == 0 then
        set_state(ST_LIFE, 400)
        set_state(ST_ATTACK, 10)
        set_state(ST_DEFENSE, 10)
    else
        for s = 0, 14 do set_state(ST_SPECIAL0 + s, 0) end
    end
    set_state(41, 0)                   -- Sacred Shield picked
    set_state(36, 0)                   -- Sacred Sword picked
    set_state(0x10, 0)                 -- shield display level
    set_state(0x0f, 0)                 -- weapon display level
    set_state(ST_BATTLE_RENDER, 1)
    set_state(ST_Y, 7)
    set_state(ST_X, 2)
    set_state(ST_FLOOR_LIMIT, 2)
    set_state(ST_FLOOR, 2)
    timer2on()
    emit(OP.NOP, 0, 0); emit(OP.SUBDISPATCH, SUB.PLAYER_UPDATE, 0)
    emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 3, 0)
    emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 2, 0)
    emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 1, 0)
    emit(OP.NOP, 0, 0); emit(OP.START_SUBEVENT, 1, 0)
    emit(OP.SHOW_ITEMS, 0, 0)
    emit(OP.SPRITE, 0, 0)
    present("disp")
    present("timer1", 0)
end
