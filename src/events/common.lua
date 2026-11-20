-- common.lua: Shared by every module: the state, slot, opcode, sub-dispatch,
-- sound and tile constants, the timeline helpers (tick, wait, play), the
-- presentation helpers, and the tile classifiers and movers the handlers
-- share (mmes, idou, mazyutu1, madoushi1, soundcheck, doorstackin).
-- Loaded by builders.lua (include); see it for the API.

-- State block indices (int32 words from 0x4b8688; 03-state-and-save.md).
ST = {
    LIFE = 0, ATTACK = 1, DEFENSE = 2, GOLD = 3,
    FLOOR = 4, FLOOR_LIMIT = 5, X = 6, Y = 7,
    KEY_YELLOW = 8, KEY_RED = 9, KEY_BLUE = 10,
    ALTAR = 0x0b,             -- altar offerings so far (0x4b86b4)
    BATTLE_RENDER = 0x0c,     -- set during a battle / the opening
    VISITS = 0x0e,            -- the fairy's visit count
    WEAPON_LEVEL = 15, SHIELD_LEVEL = 16,
    SPECIAL0 = 17,            -- special-item slot 0 (see SLOT)
    SWORD_FLAG0 = 32,         -- swords picked, 32..36
    SWORD_SACRED = 36,
    SHIELD_FLAG0 = 37,        -- shields picked, 37..41
    SHIELD_SACRED = 41,       -- (0x4b872c)
    SUBEVENT_DONE0 = 0x50,    -- Button1Click sets [0x50 + id] (subevent_done)
    SCORE = 0x55, RANK = 0x56, AREA = 0x57,   -- the fairy's ranking
    FACING = 0x58,            -- 1 down, 2 left, 3 right, 4 up (0x160 / 4)
    BGM = 0x5a,               -- the tune playing, 0 = music off (20-audio.md)
    THIEF_F2 = 0x60, ROUJIN_F2 = 0x62, FLOOR10_DONE = 0x6a,
    ROUJIN_SUB1B = 0x6d,      -- floor-16 x=9 old man's gift (0x4b883c)
    THIEF_F29 = 0x76, THIEF_F35 = 0x7a,
    FLOOR42_MEVENT = 0x83,    -- floor 42's mevent ran (stairs)
    FLOOR40_DONE = 0x96,      -- floor-40 moncheck one-shot (0x4b88e0)
    FLOOR32_MEVENT = 0x9b,    -- floor 32's mevent ran (stairs)
    ZENO_WEAK = 0x9c,         -- 1: Zeno at a tenth (floor 49) until floor 24
    SAVE_MAGIC = 0x9d,        -- -4 (0x274)
    LAST_VERTICAL = 0x9e,     -- last vertical move: 1 up, 2 down (0x278)
    OPTION_A = 0x9f,          -- second run / difficulty multiplier (0x27c)
    OPTION_B = 0xa0,          -- item carry (0x280)
    STAIRS_ADJ = 0xa6,        -- set by mmes next to stairs (meta_a6)
    WORDS_ONLY = 0xa7,        -- the Orb of Wisdom's words: no Button1 (0x29c)
}

-- Special-item slots (state ST.SPECIAL0 + slot).
SLOT = {
    ORB_OF_HERO = 0, ORB_OF_WISDOM = 1, MAGIC_ELIXIR = 4,
    SUPER_MATTOCK = 13,       -- (0x4b8700)
}

-- VM opcodes (src/event_vm.h, docs/re/12-event-vm.md section 4).  Builders
-- append in original order; the VM consumes from the tail.
OP = {
    NOP = 0x00, SOUND = 0x01, SPRITE = 0x02, ALTAR = 0x03,
    LIFE_DELTA = 0x04, MONSTER_DELTA = 0x05, SET_TILE = 0x06,
    ADD_GOLD = 0x07, SHOW_ITEMS = 0x08, MASKED_TILE = 0x09,
    START_SUBEVENT = 0x0a, SUBDISPATCH = 0x0b, MONSTER_MOVE = 0x0c,
    DOOR_OPEN = 0x0d, BATTLE_HIT = 0x0e, STOP_BGM = 0x0f,
    ADD_LIFE = 0x10, SET_TILE_SILENT = 0x11, NOP_12 = 0x12,
    HALVE_LIFE = 0x13, SET_STATE = 0x14, DRAW_ACTOR = 0x15, RESET = 0x16,
}

-- 0x0b sub-dispatch commands (docs/re/12-event-vm.md section 5).
SUB = {
    OPENNING31 = 1, ZENOLIVEWORK = 2, MADOUSHI2 = 3, BANPAIAWORK = 4,
    MONCHECK = 5, GAMEOVER = 6, TITLE = 7, PLAYER_UPDATE = 8,
    PLAYER_ANIM = 9, TAISEN2 = 10, CHANGE_FLOOR = 0x0b,
    OPENNING9 = 0x0c, HIDE_MESSAGE = 0x0d, HIDE_IMAGES = 0x0e, FADE = 0x0f,
}

-- Op 0x01's sound index (0x452762; 20-audio.md): the WAV each player
-- holds, and the tune's play / stop.
SND = {
    OPEN = 1, GET = 2, TAISEN = 3, ZENO = 4,  -- open/get/taisen/zeno.wav
    BGM = 5, BGM_STOP = 6,                    -- soundplay, MediaPlayer5.Stop
    F3 = 7, F20 = 8, KAI = 9, KAI2 = 10,      -- 3f/20f/kai/kai2.wav
}

RAW = {
    FLOOR = 0x06, ZENO_TRIGGER = 0x07, SECRET_WALL = 0x09,
    MAGIC_SERGEANT = 0x5d,
}
TILE_PX = 32                     -- 640x400 layout: Image (form+0x254) width
MAP_COLS = 11                    -- 11x11 map records (02-map-format.md)

-- The map index of cell (x, y).
function cell(x, y) return y * MAP_COLS + x end

function st(i) return get_state(i) end
function slot(s) return get_state(ST.SPECIAL0 + s) end
-- The map index of the hero's cell.
function hero_cell() return cell(st(ST.X), st(ST.Y)) end
-- Button1Click's flag for a confirmed sub-event (state[0x50 + id]).
function subevent_done(id) return get_state(ST.SUBEVENT_DONE0 + id) end

-- Timelines.  emit() appends; the VM takes the LAST command first, and
-- Timer2 runs two commands a tick.  Two ways to write a timeline:
--
-- * In append order (tail first), with tick(op, a1, a2) -- an empty command
--   then the action, i.e. one tick that does op -- and wait(n), n empty
--   commands.  Kept only where a handler interleaves its commands with
--   other builders' or with aux writes (stairs, the neighbour scans).
--
-- * In play order, with play(function(t) ... end): the steps are recorded
--   in the order they happen and appended last first when the function
--   returns.  On t:
--     t:tick(op, a1, a2)  one tick that does op (op, then an empty command)
--     t:cmd(op, a1, a2)   a single command (half a tick)
--     t:wait(n)           n empty commands
--     t:door(index, kind) a door opening or closing (doorstackin's frames)
--     t:call(name, setup) another builder's timeline at this point; setup(),
--                         if given, runs just before it (to set aux)
--   Builder-side effects written inside (set_tile, present, ...) happen at
--   once, as in append order; t:door's and t:call's happen in append order
--   when the steps are appended.
function wait(n)
    for _ = 1, n do emit(OP.NOP, 0, 0) end
end
function tick(op, a1, a2)
    emit(OP.NOP, 0, 0)
    emit(op, a1, a2)
end

function play(build)
    local steps = {}
    local t = {}
    function t:cmd(op, a1, a2) steps[#steps + 1] = { op, a1, a2 } end
    function t:tick(op, a1, a2)
        self:cmd(op, a1, a2)
        self:cmd(OP.NOP, 0, 0)
    end
    function t:wait(n)
        for _ = 1, n do self:cmd(OP.NOP, 0, 0) end
    end
    function t:door(index, kind)
        -- doorstackin in play order: progress 1 .. 16, each a door frame
        -- then an empty command carrying the progress.
        for k = 1, 0x10 do
            self:cmd(OP.DOOR_OPEN, index, kind)
            self:cmd(OP.NOP, 0, k)
        end
        steps[#steps + 1] = function()
            aux.work_558 = index
            aux.event_selector = kind
        end
    end
    function t:call(name, setup)
        steps[#steps + 1] = function()
            if setup then setup() end
            call(name)
        end
    end
    build(t)
    for i = #steps, 1, -1 do
        local s = steps[i]
        if type(s) == "function" then
            s()
        else
            emit(s[1], s[2], s[3])
        end
    end
end

-- Play-order steps shared by the handlers:
-- a door with its sound (the sound, then doorstackin's frames) ...
function door_sound(t, index, kind, arg)
    t:tick(OP.SOUND, SND.OPEN, arg or 0)
    t:door(index, kind)
end
-- ... and a tile write with its sound (the sound, then the write).
function tile_sound(t, index, tile, sound)
    t:tick(OP.SOUND, sound or SND.GET, 0)
    t:tick(OP.SET_TILE, index, tile)
end

-- Presentation helpers with no state effect in the original:
--   itemdel (0x450758) hides the item panel, timer2on (0x44bc0c) starts the
--   timeline timer, zyouout (0x449e18) opens the dialogue window for the
--   current message selector, disp (0x44cb34) redraws the status panel.
-- status_text(id): ListBox2[id] into the status-bar string 0x4b8684; disp
--   shows it in Edit8 (the bar under the map) while g_anim_sprite is 1.
-- button_caption([id]): Button1.Caption := ListBox2[id], or 'OK' without
--   an id; it stays until the next change (18-messages.md section 2).
-- itemdel (0x450758) / itemlive (0x450880) also clear/set the word flag at
-- 0x48c5ca, the high half of aux.ui_selector (0x48c5c8 is a word).
function itemdel()
    aux.ui_selector = aux.ui_selector & 0xffff
    present("itemdel")
end
function itemlive()
    aux.ui_selector = (aux.ui_selector & 0xffff) | 0x10000
    present("itemlive")
end
-- timer2on (0x44bc0c) runs itemdel first: the item-panel flag
-- (0x48c5ca, ui_selector's high word) is cleared on every timer2on.
function timer2on()
    aux.ui_selector = aux.ui_selector & 0xffff
    present("timer2on")
end
function zyouout() present("zyouout", aux.message_selector) end

-- The status string 0x4b8684 built from parts (LStrCatN): (0, id)
-- ListBox2[id], (1, n) the number n, (2, 0) the literal " ", (2, 1) the
-- literal ". " (0x44b6f0 / 0x480288).
function status_cat(...)
    present("status_cat", ...)
end

-- monswork (0x44b6f4): the monster's name, ListBox2[index + 0x5e] (indices
-- below 0x22; else the generic ListBox2[0x5d]).
function monster_name_id(index)
    return index < 0x22 and index + 0x5e or 0x5d
end

---------------------------------------------------------------------------
-- mmes (0x44c9f0): classify the tile at aux.event_selector on the current
-- floor into aux.sprite (g_anim_sprite, 0x48c58c).  Stairs next to the
-- player on the highest floor set state[0xa6] to 1 (up) or 2 (down).

function mmes()
    local raw = get_tile(aux.event_selector)
    if raw <= 0x12 then
        if raw == 0x0b or raw == 0x0c then
            aux.sprite = 0x13
            if st(ST.FLOOR) == st(ST.FLOOR_LIMIT) then
                set_state(ST.STAIRS_ADJ, raw == 0x0b and 1 or 2)
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
-- presentation.  The checks (idou_checks) are also run by the engine after
-- every ordinary step of the C movement code: handan reaches idou for floor
-- tiles, keys and items.

-- Player sprite cells for the attacked pose by facing (1 down, 2 left,
-- 3 right, 4 up): the background tile 6 and the masked overlay.
local HIT_POSE = { {0xbf, 0xc0}, {0xc7, 0xc8}, {0xcf, 0xd0}, {0xd7, 0xd8} }

local function hit_pose(p)
    local pose = HIT_POSE[st(ST.FACING)] or {0, 0}
    emit(OP.NOP, 6, pose[1])
    emit(OP.MASKED_TILE, p, pose[2])
end

-- mazyutu1 (0x44c5c4): an adjacent magician (mmes sprite 0x6e for raw
-- 0x97/0x98, 0x6f for 0x99/0x9a) strikes: life -200 or -100 per difficulty
-- step unless the Sacred Shield is held.  `d` is g_battle_damage, twice the
-- probe offset.  On floor 47 a 0x6e magician is first pushed one cell
-- further when that cell is floor.  The original tests the destination with
-- a flat index (floor 47's base + p + d), so p + d < 0 reads floor 46 and
-- the move writes there; it also lets a side push wrap to the next row.
-- The remake only takes a real one-cell step inside floor 47 and leaves the
-- magician in place at an edge (08-remake.md decision 19).
local function mazyutu1(p, d)
    local spr = aux.sprite
    if spr ~= 0x6e and spr ~= 0x6f then return end
    local diff = st(ST.OPTION_A)
    if spr == 0x6e and st(ST.FLOOR) == 0x2f then
        local y = st(ST.Y)
        local step = d // 2
        local from = p + step
        local to = p + d
        local fx, fy = from % MAP_COLS, from // MAP_COLS
        local adjacent =
            (step == -MAP_COLS and fy >= 1) or
            (step == MAP_COLS and fy <= MAP_COLS - 2) or
            (step == -1 and fx >= 1) or
            (step == 1 and fx <= MAP_COLS - 2)
        if y >= 1 and y <= 9 and adjacent
            and get_tile_at(0x2f, to) == RAW.FLOOR then
            tick(OP.MONSTER_MOVE, from, to)
            tick(OP.SOUND, SND.GET, 0)
        end
    end
    if st(ST.SHIELD_SACRED) == 0 then
        tick(OP.ADD_LIFE, (diff + 1) * (spr == 0x6e and -200 or -100), 0)
        tick(OP.SPRITE, diff == 0 and spr or spr + 0x1c, 0)
        hit_pose(p)
        tick(OP.SOUND, SND.OPEN, 0)
    end
    timer2on()
    aux.sprite = 0
    aux.work_560 = 1
end

-- madoushi1 (0x44c464): between two Magic Sergeants (0x5d) without the
-- Sacred Shield the player's life is halved (op 0x13).  Horizontal and
-- vertical pincers each run it.
local function madoushi1(p)
    tick(OP.HALVE_LIFE, 0, 0)
    tick(OP.SPRITE, 0x70, 0)
    hit_pose(p)
    tick(OP.SOUND, SND.OPEN, 0)
    timer2on()
    aux.sprite = 0
    aux.work_560 = 1
end

local function idou_checks()
    aux.work_560 = 0                 -- DAT_0048c560
    aux.sprite = 0
    local x, y = st(ST.X), st(ST.Y)
    local p = cell(x, y)
    aux.work_558 = p                -- DAT_0048c558

    -- Probe offset, bound check, g_battle_damage.
    local probes = {
        { -11, y >= 1 and y <= 10, -22 },
        { -1, x >= 1 and x <= 10, -2 },
        { 1, x >= 0 and x < 10, 2 },
        { 11, y >= 0 and y < 10, 22 },
    }
    for _, pr in ipairs(probes) do
        aux.event_selector = p + pr[1]
        if pr[2] then
            mmes()
            if aux.sprite == 0x6e or aux.sprite == 0x6f then
                mazyutu1(p, pr[3])
            end
        end
    end
    if st(ST.SHIELD_SACRED) == 0 then
        if x >= 1 and x <= 9 and get_tile(p - 1) == RAW.MAGIC_SERGEANT
            and get_tile(p + 1) == RAW.MAGIC_SERGEANT then
            madoushi1(p)
        end
        if y >= 1 and y <= 9 and get_tile(p - 11) == RAW.MAGIC_SERGEANT
            and get_tile(p + 11) == RAW.MAGIC_SERGEANT then
            madoushi1(p)
        end
    end
    if aux.work_560 == 0 then
        aux.sprite = 0
        for _, pr in ipairs(probes) do
            aux.event_selector = p + pr[1]
            if pr[2] then mmes() end
        end
        present("disp")
    end
end

builders.idou = function()
    local target = aux.target_index
    -- The step loop (0x44c10e) counts g_battle_turns 1..n, n = tile width
    -- (32 px; 8 at high and 16 at middle speed, which the harness does not
    -- set), so it ends at n.  The drawing in it is presentation; the engine
    -- turns present("player_step") into the normal tile slide.
    present("player_step", target)
    aux.battle_turns = TILE_PX
    set_state(ST.X, target % MAP_COLS)
    set_state(ST.Y, target // MAP_COLS)
    present("player_draw")              -- yusyahyouji1
    idou_checks()
end

-- The checks alone, for a step the C movement code already made.
builders.idou_checks = idou_checks


---------------------------------------------------------------------------
-- soundcheck (0x47c8f8): pick the floor's BGM
-- into state[0x5a]: floor 0 -> 0x15, 1..10 -> 5, 11..20 -> 6,
-- 21..30 -> 0x11, 31..40 -> 8, 41..49 -> 9, 50 -> 10.  soundplay
-- (0x47c2bc) then plays the MIDI for that id; that part is presentation.

function soundcheck()
    local f = st(ST.FLOOR)
    local id
    if f == 0 then id = 0x15
    elseif f >= 1 and f <= 10 then id = 5
    elseif f >= 11 and f <= 20 then id = 6
    elseif f >= 21 and f <= 30 then id = 0x11
    elseif f >= 31 and f <= 40 then id = 8
    elseif f >= 41 and f <= 49 then id = 9
    elseif f == 50 then id = 10 end
    if id then set_state(ST.BGM, id) end
end

function soundplay()
    present("bgm", st(ST.BGM))
end

---------------------------------------------------------------------------
-- doorstackin (0x4660d0): expands the door command just appended into the
-- door animation.  The original pops that command (op 0x0d, a1, a2), sets
-- aux.work_558 = a1 and aux.event_selector = a2, and appends 16 pairs
-- (0x00, 0, k), (0x0d, a1, a2) for k = 16 .. 1.  Executed tail first, the
-- door progress (previous command's arg2) runs 1 .. 16; at 16 the door
-- handler writes the map.  Callers pass the door command instead of
-- emitting it first: the queue that results is identical.

function doorstackin(a1, a2)
    aux.work_558 = a1
    aux.event_selector = a2
    for k = 0x10, 1, -1 do
        emit(OP.NOP, 0, k)
        emit(OP.DOOR_OPEN, a1, a2)
    end
end
