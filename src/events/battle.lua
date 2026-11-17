-- battle.lua: Battles: moncheck (after every won battle), taisen (an ordinary
-- battle from handan) and taisen2 (a battle spliced into a timeline).
-- Loaded by builders.lua (include); see it for the API.

-- moncheck (0x4664e4): runs after every won battle (the battle timeline's
-- last command is the splice 0x0b/5).  Floor switch 0x466512 / 0x466545;
-- each case sums a few map tiles to tell whether a group of monsters is
-- gone (their tiles became floor 6) and then opens doors / changes tiles.
-- Ported: every case (floors 2, 8, 10, 11, 14, 15, 17, 20, 25, 30, 32-35,
-- 38, 40, 41, 44, 45, 48, 49 and 50); floors without one do nothing.  No timer2on: in the original the battle
-- timeline is still running.

function tile_sum(list)
    local n = 0
    for _, i in ipairs(list) do n = n + get_tile(i) end
    return n
end

-- The cases below are written in play order (play(); common.lua).  A case
-- with several groups checks them all against the map first; in play
-- order the group the original appends last comes first.

-- A group of monsters gone (their tiles summed) opens one door.
local function group_door(t, tiles, total, door)
    if tile_sum(tiles) ~= total then return end
    t:wait(2)
    door_sound(t, door, 4)
end

-- One-shot guard (`sub eax, 1; jae skip`): runs only while state[i] == 0.
local function once(i)
    if st(i) ~= 0 then return false end
    set_state(i, 1)
    return true
end

MONCHECK = {}

MONCHECK[2] = function()               -- 0x4665a1: six doors
    if tile_sum({16, 18, 48}) ~= 0x11 then return end
    play(function(t)
        t:wait(2)
        for _, c in ipairs({48, 52, 81, 85, 114, 118}) do
            door_sound(t, c, 6)
        end
    end)
end

MONCHECK[8] = function()               -- 0x466814
    play(function(t) group_door(t, {52, 54, 42}, 0x10, 42) end)
end

-- Floor 10 (0x466901): the eight skeleton soldiers dead and the gate 27
-- still a door -> the gate opens, sub-event 24, with the music on the
-- tune.  Skeleton A (the boss) at 5 dead with the doors 71/40/36 still closed
-- (0x466b0c) -> with the music on the tune stops, sub-event 25, the doors
-- 36/40/71 open, then the room's rewards appear one by one (keys, gems,
-- the fairy 0x17 at 27, the last a raw 7 at 93 without a sound).
local F10_REWARDS = {        -- {cell, raw}, in the order they appear
    {27, 0x17}, {22, 0x22}, {23, 0x22}, {24, 0x22}, {30, 0x23}, {31, 0x23},
    {32, 0x23}, {33, 0x21}, {34, 0x21}, {35, 0x21}, {41, 0x1d}, {42, 0x1d},
    {43, 0x1d}, {115, 0x0b},
}

MONCHECK[10] = function()
    play(function(t)
        if tile_sum({5, 71, 40, 36}) == 0x12 then
            if st(ST.BGM) ~= 0 then
                t:tick(OP.SOUND, SND.BGM_STOP, 0)
            end
            t:wait(2)
            t:cmd(OP.START_SUBEVENT, 0x0f, 0)
            t:wait(3)
            door_sound(t, 36, 4)
            door_sound(t, 40, 4)
            door_sound(t, 71, 4)
            for _, r in ipairs(F10_REWARDS) do
                tile_sound(t, r[1], r[2])
            end
            t:tick(OP.SET_TILE, 93, 7)
        end
        if tile_sum({37, 38, 39, 48, 50, 59, 60, 61, 27}) == 0x34 then
            t:wait(2)
            t:cmd(OP.START_SUBEVENT, 0x0e, 0)
            t:wait(3)
            door_sound(t, 27, 4)
            if st(ST.BGM) ~= 0 then
                t:tick(OP.SOUND, SND.BGM, 0)
            end
        end
    end)
end

-- Floor 40 (0x46a2d3): after tile 5 + tile 16 sum to 0x11, one-shot
-- state[0x96] opens the final room's doors/walls (13 tile writes with sound 2).
local F40_TILES = {
    {60, 0x17}, {12, 0x22}, {13, 0x22}, {14, 0x22}, {18, 0x23}, {19, 0x23},
    {20, 0x23}, {35, 0x21}, {36, 0x21}, {37, 0x21}, {39, 0x1d}, {40, 0x1d},
    {41, 0x1d},
}
MONCHECK[40] = function()
    if get_tile(16) + get_tile(5) ~= 0x11 then return end
    if st(ST.FLOOR40_DONE) ~= 0 then return end
    set_state(ST.FLOOR40_DONE, 1)
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:wait(2)
        for _, c in ipairs(F40_TILES) do tile_sound(t, c[1], c[2]) end
    end)
end

MONCHECK[11] = function()              -- 0x46763c
    play(function(t) group_door(t, {44, 46, 34}, 0x10, 34) end)
end

MONCHECK[14] = function()              -- 0x467729
    if tile_sum({0, 2, 12, 22}) ~= 0x1a then return end
    play(function(t)
        t:wait(2)
        tile_sound(t, 22, 0x1e)
    end)
end

MONCHECK[15] = function()              -- 0x46783e
    if tile_sum({60, 27, 93}) ~= 0x0e then return end
    play(function(t)
        t:wait(2)
        door_sound(t, 27, 4)
        door_sound(t, 93, 4)
        tile_sound(t, 49, 0x29)
    end)
end

MONCHECK[17] = function()              -- 0x467a3b: four rooms, each own door
    play(function(t)
        group_door(t, {77, 79, 67}, 0x10, 67)
        group_door(t, {44, 46, 34}, 0x10, 34)
        group_door(t, {85, 87, 75}, 0x10, 75)
        group_door(t, {52, 54, 42}, 0x10, 42)
    end)
end

-- Floor 20 (0x467df8): the vampire's room cleared -> the doors 27/93,
-- sub-event 31 (0x15 + 10), with the music on the tune stops, then the
-- fairy, the reward tiles and the stairs 0x0b at 5 appear one by one.
local F20_TILES = {
    {49, 0x17}, {47, 0x22}, {58, 0x22}, {69, 0x22}, {51, 0x23}, {62, 0x23},
    {73, 0x23}, {81, 0x21}, {82, 0x21}, {83, 0x21}, {37, 0x1d}, {38, 0x1d},
    {39, 0x1d}, {5, 0x0b},
}
MONCHECK[20] = function()
    if tile_sum({48, 49, 50, 59, 61, 70, 71, 72, 60, 27, 93}) ~= 0x3e then
        return
    end
    play(function(t)
        t:wait(2)
        door_sound(t, 27, 4)
        door_sound(t, 93, 4)
        t:tick(OP.START_SUBEVENT, 0x15, 0)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        for _, c in ipairs(F20_TILES) do tile_sound(t, c[1], c[2]) end
    end)
end

-- Floor 25 (0x46887e): with sub-event 36 done the floor-24 cells 4..6,
-- 16, 27 and 38 are rewritten builder-side first.
MONCHECK[25] = function()
    if tile_sum({60, 104}) ~= 0x0a then return end
    if subevent_done(0x24) == 1 then
        set_tile_at(24, 4, 8)          -- g_map[0xb8c]
        set_tile_at(24, 6, 8)
        set_tile_at(24, 16, 6)
        set_tile_at(24, 27, 6)
        set_tile_at(24, 38, 6)
        set_tile_at(24, 5, 7)
    end
    play(function(t)
        t:wait(2)
        door_sound(t, 104, 4)
        for _, c in ipairs({ {71, 0x17}, {80, 0x1e}, {81, 0x1e}, {82, 0x1e},
                             {83, 0x1e}, {84, 0x1e} }) do
            tile_sound(t, c[1], c[2])
        end
    end)
end

MONCHECK[30] = function()              -- 0x468d0f
    if tile_sum({46, 47, 48, 50, 51, 52, 38, 71}) ~= 0x2c then return end
    play(function(t)
        t:wait(2)
        door_sound(t, 38, 4)
        door_sound(t, 71, 4)
        tile_sound(t, 5, 0x0b)
    end)
end

-- Floor 32 (0x468f4d): a door, then (tile 60 + sub-events 40 and 41 done
-- == 7) the thief goes back: Button1 "OK", floor-29 cell 17 and the
-- floor-2 cell 120 (the thief back in his cell) builder-side; sub-event 41
-- (0x1f + 10); the thief appears at 104, walks up the column to 9 and is
-- gone.
local F32_PATH = {104, 93, 82, 71, 60, 49, 38, 27, 16, 5, 6, 7, 8, 9}

MONCHECK[32] = function()
    play(function(t)
        if get_tile(60) + subevent_done(0x28) + subevent_done(0x29) == 7 then
            present("button_caption")
            set_tile_at(29, 17, 6)             -- g_map[0xe00]
            set_tile_at(2, 120, 0x15)          -- g_map[0x16e]
            t:wait(2)
            t:cmd(OP.START_SUBEVENT, 0x1f, 0)
            t:wait(5)
            t:tick(OP.SET_TILE_SILENT, 104, 0x85)
            for k = 1, #F32_PATH - 1 do
                t:cmd(OP.MONSTER_MOVE, F32_PATH[k], F32_PATH[k + 1])
                t:wait(3)
            end
            tile_sound(t, 9, 6)
        end
        group_door(t, {99, 101, 89}, 0x10, 89)
    end)
end

MONCHECK[33] = function()              -- 0x469a08
    if tile_sum({52, 54, 74, 76, 42, 86}) ~= 0x20 then return end
    play(function(t)
        t:wait(2)
        door_sound(t, 42, 4)
        door_sound(t, 86, 4)
    end)
end

MONCHECK[34] = function()              -- 0x469b98, one-shot state[0x94]
    if tile_sum({37, 39, 41, 43, 81, 83, 85, 87}) ~= 0x30 then return end
    if not once(0x94) then return end
    play(function(t)
        t:wait(2)
        for _, c in ipairs({ {56, 0x1e}, {44, 0x1d}, {46, 0x1d}, {66, 0x1d},
                             {68, 0x1d} }) do
            tile_sound(t, c[1], c[2])
        end
    end)
end

MONCHECK[35] = function()              -- 0x469f53, one-shot state[0x95]
    if tile_sum({71, 27}) ~= 0x0a then return end
    if not once(0x95) then return end
    play(function(t)
        t:wait(2)
        door_sound(t, 27, 4)
        for _, c in ipairs({ {60, 0x2f}, {48, 0x21}, {49, 0x21},
                             {50, 0x21} }) do
            tile_sound(t, c[1], c[2])
        end
    end)
end

MONCHECK[38] = function()              -- 0x46a2a2: builder-side only
    if tile_sum({99, 101}) ~= 0x0c then return end
    set_tile(89, 9)
end

MONCHECK[41] = function()              -- 0x46ab37, one-shot state[0x9a]
    if tile_sum({12, 20}) ~= 0x0c then return end
    if not once(0x9a) then return end
    play(function(t)
        t:wait(2)
        for _, c in ipairs({ {59, 8}, {60, 8}, {61, 8}, {70, 6},
                             {72, 6} }) do
            tile_sound(t, c[1], c[2], SND.OPEN)
        end
        tile_sound(t, 49, 0x2d)
    end)
end

MONCHECK[44] = function()              -- 0x46af38
    play(function(t) group_door(t, {92, 94, 82}, 0x10, 82) end)
end

MONCHECK[45] = function()              -- 0x46b02b
    play(function(t)
        group_door(t, {95, 117, 105}, 0x10, 105)
        group_door(t, {92, 114, 102}, 0x10, 102)
    end)
end

MONCHECK[48] = function()              -- 0x46b20c
    play(function(t) group_door(t, {0, 84}, 0x0a, 84) end)
end

-- Floor 49 (0x46b2f2): two doors; the four Magic Sergeants (4 x 0x5d at
-- 15/17/37/39) with their ring of floor cells -> one-shot ZENO_WEAK with
-- Zeno's table entry (0x489a00..08) divided by 10 and sub-event 56
-- (0x2e + 10), the music stopped and restarted around it.  Zeno gone ->
-- the timeline is emptied (g_tl_count = 0: what came before never plays)
-- and the last room opens: the sergeants' cells cleared, door 71, then
-- the rewards one by one.
local F49_TILES = {
    {16, 0x17}, {48, 0x21}, {49, 0x21}, {50, 0x21}, {34, 0x22}, {35, 0x22},
    {36, 0x22}, {40, 0x23}, {41, 0x23}, {42, 0x23}, {15, 0x1e}, {17, 0x2e},
}
MONCHECK[49] = function()
    play(function(t)
        if tile_sum({16, 26, 28, 38}) == 0x18 and
           tile_sum({15, 17, 37, 39}) == 0x174 then
            if st(ST.ZENO_WEAK) == 0 then
                set_state(ST.ZENO_WEAK, 1)
                set_zeno(idiv(monster_stat(0x0f, "hp"), 10),
                         idiv(monster_stat(0x0f, "attack"), 10),
                         idiv(monster_stat(0x0f, "defense"), 10))
            end
            t:wait(2)
            if st(ST.BGM) ~= 0 then
                t:tick(OP.SOUND, SND.BGM_STOP, 0)
            end
            t:tick(OP.START_SUBEVENT, 0x2e, 0)
            if st(ST.BGM) ~= 0 then
                t:tick(OP.SOUND, SND.BGM, 0)
            end
        end
        group_door(t, {81, 83, 71, 60}, 0x17, 71)
        group_door(t, {103, 105, 93}, 0x10, 93)
    end)
    if tile_sum({27, 71, 60}) ~= 0x10 then return end
    set_count(0)
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:wait(2)
        for _, c in ipairs({15, 17, 37, 39}) do
            t:cmd(OP.SET_TILE, c, 6)
        end
        door_sound(t, 71, 4)
        for _, c in ipairs(F49_TILES) do tile_sound(t, c[1], c[2]) end
    end)
end

-- Floor 50 (0x46bfa8): only with option A set (the R run / second round):
-- sub-event 0x48 (0x3e + 10), sprite 0x8c, hide the message and images,
-- fade, then sixteen openning9 frames 0xe8..0xf7 (phase 2, from the empty
-- command's arg1 before each) with sound 1, and game over (sub-dispatch 6).
MONCHECK[50] = function()
    if st(ST.OPTION_A) == 0 then return end
    timer2on()
    play(function(t)
        t:cmd(OP.START_SUBEVENT, 0x3e, 0x63)
        t:wait(3)
        t:tick(OP.SPRITE, 0x8c, 0)
        t:tick(OP.SUBDISPATCH, SUB.HIDE_MESSAGE, 0)
        t:tick(OP.SUBDISPATCH, SUB.HIDE_IMAGES, 0)
        t:tick(OP.SUBDISPATCH, SUB.FADE, 0)
        for k = 0xe8, 0xf7 do
            t:tick(OP.SOUND, SND.OPEN, 0)
            t:cmd(OP.SUBDISPATCH, SUB.OPENNING9, k)
            t:cmd(OP.NOP, 2, 0)
        end
        t:tick(OP.SUBDISPATCH, SUB.GAMEOVER, 0)
    end)
    timer2on()
end

builders.moncheck = function()
    local h = MONCHECK[st(ST.FLOOR)]
    if h then return h() end
end

---------------------------------------------------------------------------
-- taisen (0x44a480): an ordinary battle, from handan's monster raws.  The
-- monster index comes from the raw (0x3d..0x5f: (raw+1)/2 - 31; 0x61..0x69
-- the 3x3 monster 18; 0x73..0x7b the dragon 19; 0x84..0x9d: (raw+1)/2 -
-- 47); 0x6a..0x72 leave g_anim_phase stale in the original and are not on
-- any map (fail closed).  An attackable monster: idou steps onto its cell
-- (with idou's own checks), then dameiji (0x450064) forecasts turns and
-- damage.  The player wins when the damage is below his life: moncheck,
-- gold (halved for the 3x3 monster's outer cells), the player's cell
-- cleared (all nine for the 3x3 monster and the dragon at their centres),
-- and the turns as hit animations whose 0x04/0x05 take life and HP in the
-- drain.  Otherwise the losing timeline runs the death window (op 0x03).
-- A monster whose defense blocks every attack only shows ListBox2[0xd9].

local function battle_index(raw)
    local d = (raw - 0x3d) & 0xff
    if d < 0x23 then return ((raw + 1) >> 1) - 31 end
    if raw >= 0x61 and raw <= 0x69 then return 0x12 end
    if raw >= 0x73 and raw <= 0x7b then return 0x13 end
    if raw >= 0x84 and raw <= 0x9d then return ((raw + 1) >> 1) - 47 end
    return nil
end

-- g_monsters[i]; monster_stat(0x0f, ...) is the run-time Zeno entry
-- (set_zeno: syokidata2, handan on floor 50, the floor-24/49 one-shots).
local function battle_monster(index)
    return {
        hp = monster_stat(index, "hp"), attack = monster_stat(index, "attack"),
        defense = monster_stat(index, "defense"), gold = monster_stat(index, "gold"),
    }
end

local function doubled_attack(index)
    if index == 0x0c or index == 0x0d or index == 0x11 then
        return slot(3) ~= 0                -- the Cross
    end
    return index == 0x13 and slot(10) ~= 0 -- the Dragon Slayer
end

-- dameiji: turns to kill (damecount / damecount2) and the damage taken,
-- (monster attack - defense) x (turns - 1).
local function dameiji(index, m)
    local diff1 = st(ST.OPTION_A) + 1
    aux.work_564 = 0            -- g_battle_unwinnable
    local attack = st(ST.ATTACK) * (doubled_attack(index) and 2 or 1)
    local hp = m.hp * diff1            -- g_monster_hp (0x48c5b4)
    local turns = 0
    while hp > 0 do
        hp = m.defense * diff1 + (hp - attack)
        turns = turns + 1
    end
    aux.battle_turns = turns
    local hit = m.attack * diff1
    if st(ST.DEFENSE) - hit < 0 then
        return (hit - st(ST.DEFENSE)) * (turns - 1)
    end
    return 0
end

-- One strike, in play order (the battles below are written with play()):
-- the sound, the struck one's hit frame (a masked tile with the frame's
-- background and sprite in the empty command after it), then the life or
-- HP change.  hit_hero: the monster strikes the hero (his pose by facing);
-- hit_monster: the hero strikes the monster (op: MASKED_TILE, or
-- BATTLE_HIT for the big monsters' last blow).
local function hit_hero(t, target, tile, life_arg)
    local facing = (st(ST.FACING) - 1) * 8
    t:tick(OP.SOUND, SND.TAISEN, 0)
    t:cmd(OP.MASKED_TILE, target, facing + 0xc0)
    t:cmd(OP.NOP, tile, facing + 0xbf)
    t:tick(OP.LIFE_DELTA, target, life_arg)
end

local function hit_monster(t, target, tile, hp_arg, op)
    t:tick(OP.SOUND, SND.TAISEN, 0)
    t:cmd(op or OP.MASKED_TILE, target, 0xb7)
    t:cmd(OP.NOP, tile, 0xb8)
    t:tick(OP.MONSTER_DELTA, target, hp_arg)
end

builders.taisen = function()
    local target = aux.target_index
    local raw = get_tile(target)
    local index = battle_index(raw)
    if not index then
        fail(string.format("taisen: raw 0x%02x has no monster", raw))
    end
    aux.event_selector = index
    aux.current_monster = index
    present("monshyouji", target, index)
    local m = battle_monster(index)
    local diff1 = st(ST.OPTION_A) + 1
    local attackable = m.defense * diff1 - st(ST.ATTACK) < 0
    if attackable then
        call("idou")
        set_state(ST.BATTLE_RENDER, 1)
    end
    aux.event_selector = index
    aux.monster_hp = m.hp * diff1
    if not attackable then
        aux.sprite = 1
        present("status_text", 0xd9)
        present("disp")
        timer2on()
        return
    end
    local damage = dameiji(index, m)
    local p = hero_cell()
    local big = (index == 0x12 and raw == 0x68) or (index == 0x13 and raw == 0x7a)
    if damage < st(ST.LIFE) then
        set_state(ST.BATTLE_RENDER, 0)
        damage = dameiji(index, m)
        local turns = aux.battle_turns
        local gold = m.gold
        if (raw >= 0x61 and raw <= 0x67) or raw == 0x69 then
            gold = (gold - (gold < 0 and -1 or 0)) // 2   -- arithmetic halve
        end
        present("monswork", index)
        aux.sprite = 1
        -- 0x44aac1: [0xc7] " " name ". " [0xc8] " " gold*(Lucky+1) [0xc9];
        -- an outer cell of the octopus (0x61..0x67, 0x69): [0xf0] ... with
        -- half the gold, or the plain table gold with the Lucky Gold.
        local lucky = slot(14)
        if (raw >= 0x61 and raw <= 0x67) or raw == 0x69 then
            local shown = lucky == 0 and (m.gold - (m.gold < 0 and -1 or 0)) // 2
                                     or m.gold
            status_cat(0, 0xf0, 2, 0, 0, monster_name_id(index), 2, 1,
                       0, 0xc8, 2, 0, 1, shown, 0, 0xc9)
        else
            status_cat(0, 0xc7, 2, 0, 0, monster_name_id(index), 2, 1,
                       0, 0xc8, 2, 0, 1, m.gold * (lucky + 1), 0, 0xc9)
        end
        local tile = get_tile(p)
        -- The turns (the hero strikes, then the monster), the last blow,
        -- the hero's step animation, the monster's cells cleared (all nine
        -- of the big ones), the gold and moncheck.
        play(function(t)
            for _ = 1, turns - 1 do
                hit_monster(t, target, tile, 0xb7)
                hit_hero(t, target, tile, 0)
            end
            hit_monster(t, target, tile, 0, big and OP.BATTLE_HIT or nil)
            t:tick(OP.SUBDISPATCH, SUB.PLAYER_ANIM, 0)
            if big then
                for _, d in ipairs({1, 0, -1, -0x0a, -0x0b, -0x0c, -0x17,
                                    -0x16, -0x15}) do
                    t:cmd(OP.SET_TILE_SILENT, p + d, 6)
                end
            else
                t:cmd(OP.SET_TILE_SILENT, p, 6)
            end
            t:cmd(OP.SPRITE, 0, 0)
            t:cmd(OP.ADD_GOLD, gold, 0)
            t:cmd(OP.SUBDISPATCH, SUB.MONCHECK, 5)
        end)
        if turns > 1 then aux.work_558 = turns - 1 end  -- the loop counter
    else
        -- ydameiji / ydameiji2: the player's life counted down instead.
        local hp = st(ST.LIFE)
        local turns = 0
        local hit = m.attack * diff1
        while hp > 0 do
            hp = (hp - hit) + st(ST.DEFENSE)
            turns = turns + 1
        end
        aux.battle_turns = turns
        local tile = get_tile(p)
        -- The turns, the original's last-counted first (its turn 1, the
        -- hero's last life change, carries the tile), then the death
        -- window (op 0x03).
        play(function(t)
            for i = turns, 1, -1 do
                hit_monster(t, target, tile, 0)
                hit_hero(t, target, tile, i == 1 and tile or 0xb8)
            end
            t:wait(6)
            t:cmd(OP.ALTAR, 0, 0)
        end)
        if turns >= 1 then aux.work_558 = turns end
    end
    timer2on()
end

-- handan 0x5b/0x5c (0x445a13): Zeno's cells.  Floor 49: a battle.  Floor
-- 50 on the first round (option A = 0): the ending -- Timer1 off, Button1
-- "OK", then sub-dispatch 6 (the reset, run last) behind 0x0b/0x0c
-- openning9 frames: the fade-in 0x190..1 (one per tick), 400..0xd2 in
-- slowing steps, the 0x11..1 frames with sound 10, the 0xf7..0xe8
-- dissolve with sound 1, the fade and the hidden images.  A later round
-- fights him (battle_monster: 1000/625/125).  Elsewhere nothing happens.
local ZENO_ROOM_HEAD = { {0, 0, 0}, {11, 6, 0}, {0, 8, 0}, {11, 12, 0}, {0, 7, 0}, {11, 12, 0} }
local ZENO_ROOM_TAIL = {
    {0, 4, 0}, {11, 12, 400}, {0, 4, 0}, {11, 12, 399}, {0, 4, 0}, {11, 12, 396},
    {0, 4, 0}, {11, 12, 393}, {0, 4, 0}, {11, 12, 390}, {0, 4, 0}, {11, 12, 387},
    {0, 4, 0}, {11, 12, 384}, {0, 4, 0}, {11, 12, 381}, {0, 4, 0}, {11, 12, 378},
    {0, 4, 0}, {11, 12, 375}, {0, 4, 0}, {11, 12, 372}, {0, 4, 0}, {11, 12, 369},
    {0, 4, 0}, {11, 12, 366}, {0, 4, 0}, {11, 12, 363}, {0, 4, 0}, {11, 12, 360},
    {0, 4, 0}, {11, 12, 357}, {0, 4, 0}, {11, 12, 354}, {0, 4, 0}, {11, 12, 351},
    {0, 4, 0}, {11, 12, 348}, {0, 4, 0}, {11, 12, 345}, {0, 4, 0}, {11, 12, 342},
    {0, 4, 0}, {11, 12, 339}, {0, 4, 0}, {11, 12, 336}, {0, 4, 0}, {11, 12, 333},
    {0, 4, 0}, {11, 12, 330}, {0, 4, 0}, {11, 12, 327}, {0, 4, 0}, {11, 12, 324},
    {0, 4, 0}, {11, 12, 321}, {0, 4, 0}, {11, 12, 318}, {0, 4, 0}, {11, 12, 315},
    {0, 4, 0}, {11, 12, 312}, {0, 4, 0}, {11, 12, 309}, {0, 4, 0}, {11, 12, 306},
    {0, 4, 0}, {11, 12, 303}, {0, 4, 0}, {11, 12, 300}, {0, 4, 0}, {11, 12, 295},
    {0, 4, 0}, {11, 12, 290}, {0, 4, 0}, {11, 12, 285}, {0, 4, 0}, {11, 12, 280},
    {0, 4, 0}, {11, 12, 275}, {0, 4, 0}, {11, 12, 270}, {0, 4, 0}, {11, 12, 265},
    {0, 4, 0}, {11, 12, 260}, {0, 4, 0}, {11, 12, 255}, {0, 4, 0}, {11, 12, 250},
    {0, 4, 0}, {11, 12, 245}, {0, 4, 0}, {11, 12, 240}, {0, 4, 0}, {11, 12, 235},
    {0, 4, 0}, {11, 12, 230}, {0, 4, 0}, {11, 12, 225}, {0, 4, 0}, {11, 12, 220},
    {0, 4, 0}, {11, 12, 215}, {0, 4, 0}, {11, 12, 210}, {0, 4, 0}, {11, 12, 205},
    {0, 4, 0}, {11, 12, 197}, {0, 4, 0}, {11, 12, 189}, {0, 4, 0}, {11, 12, 181},
    {0, 4, 0}, {11, 12, 173}, {0, 4, 0}, {11, 12, 165}, {0, 4, 0}, {11, 12, 157},
    {0, 4, 0}, {11, 12, 149}, {0, 4, 0}, {11, 12, 141}, {0, 4, 0}, {11, 12, 133},
    {0, 4, 0}, {11, 12, 125}, {0, 4, 0}, {11, 12, 117}, {0, 4, 0}, {11, 12, 109},
    {0, 4, 0}, {11, 12, 101}, {0, 4, 0}, {11, 12, 91}, {0, 4, 0}, {11, 12, 81},
    {0, 4, 0}, {11, 12, 71}, {0, 4, 0}, {11, 12, 61}, {0, 4, 0}, {11, 12, 51},
    {0, 4, 0}, {11, 12, 41}, {0, 4, 0}, {11, 12, 31}, {0, 4, 0}, {11, 12, 21},
    {0, 4, 0}, {11, 12, 11}, {0, 4, 0}, {11, 12, 1}, {0, 0, 0}, {0, 0, 0},
    {0, 6, 0}, {11, 12, 17}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 16},
    {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 15}, {0, 0, 0}, {1, 10, 0},
    {0, 6, 0}, {11, 12, 14}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 13},
    {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 12}, {0, 0, 0}, {1, 10, 0},
    {0, 6, 0}, {11, 12, 11}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 10},
    {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 9}, {0, 0, 0}, {1, 10, 0},
    {0, 6, 0}, {11, 12, 8}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 7},
    {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 6}, {0, 0, 0}, {1, 10, 0},
    {0, 6, 0}, {11, 12, 5}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 4},
    {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 3}, {0, 0, 0}, {1, 10, 0},
    {0, 6, 0}, {11, 12, 2}, {0, 0, 0}, {1, 10, 0}, {0, 6, 0}, {11, 12, 1},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 247}, {0, 0, 0}, {1, 1, 0},
    {0, 2, 0}, {11, 12, 246}, {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 245},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 244}, {0, 0, 0}, {1, 1, 0},
    {0, 2, 0}, {11, 12, 243}, {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 242},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 241}, {0, 0, 0}, {1, 1, 0},
    {0, 2, 0}, {11, 12, 240}, {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 239},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 238}, {0, 0, 0}, {1, 1, 0},
    {0, 2, 0}, {11, 12, 237}, {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 236},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 235}, {0, 0, 0}, {1, 1, 0},
    {0, 2, 0}, {11, 12, 234}, {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 233},
    {0, 0, 0}, {1, 1, 0}, {0, 2, 0}, {11, 12, 232}, {0, 0, 0}, {1, 1, 0},
    {0, 0, 0}, {11, 15, 0}, {0, 0, 0}, {11, 14, 0},
}

function zeno_room()
    local floor = st(ST.FLOOR)
    if floor == 0x31 then
        return call("taisen")
    elseif floor ~= 0x32 then
        return
    end
    if st(ST.OPTION_A) ~= 0 then
        if st(ST.OPTION_B) == 0 then set_zeno(1000, 0x271, 0x7d) end  -- 0x4463b3
        return call("taisen")
    end
    present("timer1", 0)
    timer2on()
    present("button_caption")          -- Button1.Caption := 'OK'
    for _, c in ipairs(ZENO_ROOM_HEAD) do emit(c[1], c[2], c[3]) end
    for i = 1, 0x190 do
        emit(OP.NOP, 5, 0)
        emit(OP.SUBDISPATCH, SUB.OPENNING9, 0x191 - i)
    end
    for _, c in ipairs(ZENO_ROOM_TAIL) do emit(c[1], c[2], c[3]) end
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
    local diff = st(ST.OPTION_A)
    local hp_total = monster_stat(index, "hp") * (diff + 1)
    local mdef = monster_stat(index, "defense")
    local matk = monster_stat(index, "attack")
    local gold = monster_stat(index, "gold")

    aux.event_selector = index
    aux.current_monster = index
    aux.monster_hp = hp_total
    set_state(ST.BATTLE_RENDER, 1)
    present("monshyouji", target, index)

    -- dameiji2 / damei3: turns to kill the monster.
    local attack = st(ST.ATTACK)
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
    local defense = st(ST.DEFENSE)
    local battle_damage = 0
    if matk * (diff + 1) > defense then
        battle_damage = (matk * (diff + 1) - defense) * turns
    end
    present("battle_forecast", index, turns, battle_damage)

    local p = hero_cell()
    local tile = get_tile(p)
    local facing = (st(ST.FACING) - 1) * 8

    if battle_damage < st(ST.LIFE) then
        -- Player survives: reward, moncheck, and the winning animation.
        set_state(ST.BATTLE_RENDER, 0)
        aux.sprite = 1
        present("monswork", index)
        -- 0x47ff1e: [0xc7] " " name ". " [0xc8] " " gold [0xc9]
        status_cat(0, 0xc7, 2, 0, 0, monster_name_id(index), 2, 1,
                   0, 0xc8, 2, 0, 1, gold, 0, 0xc9)
        -- The turns (the monster strikes, then the hero), the hero's step
        -- animation (yusyahyouji1), the cell cleared, the gold, moncheck.
        play(function(t)
            for _ = 1, turns do
                hit_hero(t, target, tile, 0)
                hit_monster(t, target, tile, 0xc1)
            end
            t:tick(OP.SUBDISPATCH, SUB.PLAYER_ANIM, 0)
            t:cmd(OP.SET_TILE_SILENT, p, 6)
            t:cmd(OP.SPRITE, 0, 0)
            t:cmd(OP.ADD_GOLD, gold, 0)
            t:cmd(OP.SUBDISPATCH, SUB.MONCHECK, 5)   -- moncheck splice
        end)
        if turns >= 1 then aux.work_558 = turns end
    else
        -- The player dies: ydamei3 counts the losing turns.
        local idelta = matk * (diff + 1) - defense
        if idelta <= 0 then
            fail("taisen2: losing battle never terminates")
        end
        hp = st(ST.LIFE)
        turns = 0
        while hp > 0 do
            hp = hp - matk * (diff + 1) + defense
            turns = turns + 1
        end
        aux.battle_turns = turns
        -- The turns, the original's last-counted first: the monster strikes
        -- (the life change a single command, the tile on the original's
        -- turn 1, else 0xfa); after every turn but that one, a pause and
        -- the hero strikes back.  Then a pause and the death window.
        play(function(t)
            for i = turns, 1, -1 do
                t:tick(OP.SOUND, SND.TAISEN, 0)
                t:cmd(OP.MASKED_TILE, target, facing + 0xc0)
                t:cmd(OP.NOP, tile, facing + 0xbf)
                t:cmd(OP.LIFE_DELTA, target, i == 1 and tile or 0xfa)
                if i > 1 then
                    t:wait(1)
                    hit_monster(t, target, tile, 0)
                end
            end
            t:wait(19)
            t:tick(OP.ALTAR, 0, 0)
        end)
        if turns >= 1 then aux.work_558 = turns end
    end
    timer2on()
end
