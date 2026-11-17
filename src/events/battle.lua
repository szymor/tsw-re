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

local function mc_door(index, kind)
    doorstackin(index, kind)
    tick(OP.SOUND, SND.OPEN, 0)
end

MONCHECK = {}

MONCHECK[2] = function()               -- 0x4665a1
    if tile_sum({0x10, 0x12, 0x30}) ~= 0x11 then return end
    for _, i in ipairs({0x76, 0x72, 0x55, 0x51, 0x34, 0x30}) do
        mc_door(i, 6)
    end
    wait(2)
end

MONCHECK[8] = function()               -- 0x466814
    if tile_sum({0x34, 0x36, 0x2a}) ~= 0x10 then return end
    mc_door(0x2a, 4)
    wait(2)
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
        if st(ST.BGM) ~= 0 then
            tick(OP.SOUND, SND.BGM, 0)
        end
        mc_door(0x1b, 4)
        wait(3)
        emit(OP.START_SUBEVENT, 0x0e, 0)
        wait(2)
    end
    if tile_sum({5, 71, 40, 36}) ~= 0x12 then return end
    tick(OP.SET_TILE, F10_REWARDS[1][1], F10_REWARDS[1][2])
    for i = 2, #F10_REWARDS do
        tick(OP.SET_TILE, F10_REWARDS[i][1], F10_REWARDS[i][2])
        tick(OP.SOUND, SND.GET, 0)
    end
    mc_door(0x47, 4)
    mc_door(0x28, 4)
    mc_door(0x24, 4)
    wait(3)
    emit(OP.START_SUBEVENT, 0x0f, 0)
    wait(2)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

-- Floor 40 (0x46a2d3): after tile 5 + tile 16 sum to 0x11, one-shot
-- state[0x96] opens the final room's doors/walls (13 tile writes with sound 2).
MONCHECK[40] = function()
    if get_tile(0x10) + get_tile(0x05) ~= 0x11 then return end
    if st(ST.FLOOR40_DONE) ~= 0 then return end
    set_state(ST.FLOOR40_DONE, 1)
    local tiles = {
        {0x29, 0x1d}, {0x28, 0x1d}, {0x27, 0x1d},
        {0x25, 0x21}, {0x24, 0x21}, {0x23, 0x21},
        {0x14, 0x23}, {0x13, 0x23}, {0x12, 0x23},
        {0x0e, 0x22}, {0x0d, 0x22}, {0x0c, 0x22},
        {0x3c, 0x17},
    }
    for _, t in ipairs(tiles) do
        tick(OP.SET_TILE, t[1], t[2])
        tick(OP.SOUND, SND.GET, 0)
    end
    wait(2)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

local function mc_nops(n)
    wait(n)
end

-- One tile write with its sound: NOP, 0x06 index/tile, NOP, 0x01/s.
local function mc_tile(index, tile, sound)
    tick(OP.SET_TILE, index, tile)
    tick(OP.SOUND, sound or SND.GET, 0)
end

-- A group of monsters gone (their tiles summed) opens one door.
local function mc_group_door(tiles, total, door)
    if tile_sum(tiles) ~= total then return end
    mc_door(door, 4)
    mc_nops(2)
end

-- One-shot guard (`sub eax, 1; jae skip`): runs only while state[i] == 0.
local function mc_once(i)
    if st(i) ~= 0 then return false end
    set_state(i, 1)
    return true
end

MONCHECK[11] = function()              -- 0x46763c
    mc_group_door({0x2c, 0x2e, 0x22}, 0x10, 0x22)
end

MONCHECK[14] = function()              -- 0x467729
    if tile_sum({0x00, 0x02, 0x0c, 0x16}) ~= 0x1a then return end
    mc_tile(0x16, 0x1e)
    mc_nops(2)
end

MONCHECK[15] = function()              -- 0x46783e
    if tile_sum({0x3c, 0x1b, 0x5d}) ~= 0x0e then return end
    mc_tile(0x31, 0x29)
    mc_door(0x5d, 4)
    mc_door(0x1b, 4)
    mc_nops(2)
end

MONCHECK[17] = function()              -- 0x467a3b: four rooms, each own door
    mc_group_door({0x34, 0x36, 0x2a}, 0x10, 0x2a)
    mc_group_door({0x55, 0x57, 0x4b}, 0x10, 0x4b)
    mc_group_door({0x2c, 0x2e, 0x22}, 0x10, 0x22)
    mc_group_door({0x4d, 0x4f, 0x43}, 0x10, 0x43)
end

-- Floor 20 (0x467df8): the vampire's room cleared -> stairs 0x0b at 5, the
-- reward tiles and the fairy, sub-event 31 (0x15 + 10), doors 0x5d/0x1b.
MONCHECK[20] = function()
    if tile_sum({0x30, 0x31, 0x32, 0x3b, 0x3d, 0x46, 0x47, 0x48, 0x3c,
                 0x1b, 0x5d}) ~= 0x3e then return end
    for _, t in ipairs({
        {0x05, 0x0b}, {0x27, 0x1d}, {0x26, 0x1d}, {0x25, 0x1d},
        {0x53, 0x21}, {0x52, 0x21}, {0x51, 0x21}, {0x49, 0x23},
        {0x3e, 0x23}, {0x33, 0x23}, {0x45, 0x22}, {0x3a, 0x22},
        {0x2f, 0x22}, {0x31, 0x17},
    }) do mc_tile(t[1], t[2]) end
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
    tick(OP.START_SUBEVENT, 0x15, 0)
    mc_door(0x5d, 4)
    mc_door(0x1b, 4)
    mc_nops(2)
end

-- Floor 25 (0x46887e): with state[0x74] == 1 the floor-24 cells 4..6,
-- 16, 27 and 38 are rewritten builder-side first.
MONCHECK[25] = function()
    if tile_sum({0x3c, 0x68}) ~= 0x0a then return end
    if subevent_done(0x24) == 1 then
        set_tile_at(24, 4, 8)          -- g_map[0xb8c]
        set_tile_at(24, 6, 8)
        set_tile_at(24, 16, 6)
        set_tile_at(24, 27, 6)
        set_tile_at(24, 38, 6)
        set_tile_at(24, 5, 7)
    end
    for _, t in ipairs({
        {0x54, 0x1e}, {0x53, 0x1e}, {0x52, 0x1e}, {0x51, 0x1e},
        {0x50, 0x1e}, {0x47, 0x17},
    }) do mc_tile(t[1], t[2]) end
    mc_door(0x68, 4)
    mc_nops(2)
end

MONCHECK[30] = function()              -- 0x468d0f
    if tile_sum({0x2e, 0x2f, 0x30, 0x32, 0x33, 0x34, 0x26, 0x47}) ~= 0x2c then
        return
    end
    mc_tile(0x05, 0x0b)
    mc_door(0x47, 4)
    mc_door(0x26, 4)
    mc_nops(2)
end

-- Floor 32 (0x468f4d): a door, then (tile 60 + sub-events 40 and 41 done
-- == 7) the thief's tunnel: Button1 "OK", floor-29 cell 17 and the floor-2
-- cell 120 (the thief back in his cell) builder-side, a run of 0x0c moves
-- from cell 9 down the column to cell 104 and sub-event 41 (0x1f + 10).
local F32_PATH = {9, 8, 7, 6, 5, 16, 27, 38, 49, 60, 71,
                  82, 93, 104}

MONCHECK[32] = function()
    mc_group_door({0x63, 0x65, 0x59}, 0x10, 0x59)
    if get_tile(0x3c) + subevent_done(0x28) + subevent_done(0x29) ~= 7 then return end
    present("button_caption")
    set_tile_at(29, 17, 6)             -- g_map[0xe00]
    set_tile_at(2, 120, 0x15)          -- g_map[0x16e]
    mc_tile(9, 6)
    for k = 2, #F32_PATH do
        mc_nops(3)
        emit(OP.MONSTER_MOVE, F32_PATH[k], F32_PATH[k - 1])
    end
    tick(OP.SET_TILE_SILENT, 0x68, 0x85)
    mc_nops(5)
    emit(OP.START_SUBEVENT, 0x1f, 0)
    mc_nops(2)
end

MONCHECK[33] = function()              -- 0x469a08
    if tile_sum({0x34, 0x36, 0x4a, 0x4c, 0x2a, 0x56}) ~= 0x20 then return end
    mc_door(0x56, 4)
    mc_door(0x2a, 4)
    mc_nops(2)
end

MONCHECK[34] = function()              -- 0x469b98, one-shot state[0x94]
    if tile_sum({0x25, 0x27, 0x29, 0x2b, 0x51, 0x53, 0x55, 0x57}) ~= 0x30 then
        return
    end
    if not mc_once(0x94) then return end
    for _, t in ipairs({{0x44, 0x1d}, {0x42, 0x1d}, {0x2e, 0x1d},
                        {0x2c, 0x1d}, {0x38, 0x1e}}) do
        mc_tile(t[1], t[2])
    end
    mc_nops(2)
end

MONCHECK[35] = function()              -- 0x469f53, one-shot state[0x95]
    if tile_sum({0x47, 0x1b}) ~= 0x0a then return end
    if not mc_once(0x95) then return end
    for _, t in ipairs({{0x32, 0x21}, {0x31, 0x21}, {0x30, 0x21},
                        {0x3c, 0x2f}}) do
        mc_tile(t[1], t[2])
    end
    mc_door(0x1b, 4)
    mc_nops(2)
end

MONCHECK[38] = function()              -- 0x46a2a2: builder-side only
    if tile_sum({0x63, 0x65}) ~= 0x0c then return end
    set_tile(0x59, 9)
end

MONCHECK[41] = function()              -- 0x46ab37, one-shot state[0x9a]
    if tile_sum({0x0c, 0x14}) ~= 0x0c then return end
    if not mc_once(0x9a) then return end
    mc_tile(0x31, 0x2d)
    for _, t in ipairs({{0x48, 6}, {0x46, 6}, {0x3d, 8}, {0x3c, 8},
                        {0x3b, 8}}) do
        mc_tile(t[1], t[2], 1)
    end
    mc_nops(2)
end

MONCHECK[44] = function()              -- 0x46af38
    mc_group_door({0x5c, 0x5e, 0x52}, 0x10, 0x52)
end

MONCHECK[45] = function()              -- 0x46b02b
    mc_group_door({0x5c, 0x72, 0x66}, 0x10, 0x66)
    mc_group_door({0x5f, 0x75, 0x69}, 0x10, 0x69)
end

MONCHECK[48] = function()              -- 0x46b20c
    mc_group_door({0x00, 0x54}, 0x0a, 0x54)
end

-- Floor 49 (0x46b2f2): two doors; the four Magic Sergeants (4 x 0x5d at
-- 0x0f/0x11/0x25/0x27) with their ring of floor cells -> one-shot
-- state[0x9c] with Zeno's table entry (0x489a00..08) divided by 10, and
-- sub-event 56 (0x2e + 10).  Zeno gone -> the timeline is emptied
-- (g_tl_count = 0) and the last room opens.
MONCHECK[49] = function()
    mc_group_door({0x67, 0x69, 0x5d}, 0x10, 0x5d)
    mc_group_door({0x51, 0x53, 0x47, 0x3c}, 0x17, 0x47)
    if tile_sum({0x10, 0x1a, 0x1c, 0x26}) == 0x18 and
       tile_sum({0x0f, 0x11, 0x25, 0x27}) == 0x174 then
        if st(ST.ZENO_WEAK) == 0 then
            set_state(ST.ZENO_WEAK, 1)
            set_zeno(idiv(monster_stat(0x0f, "hp"), 10),
                     idiv(monster_stat(0x0f, "attack"), 10),
                     idiv(monster_stat(0x0f, "defense"), 10))
        end
        if st(ST.BGM) ~= 0 then
            tick(OP.SOUND, SND.BGM, 0)
        end
        tick(OP.START_SUBEVENT, 0x2e, 0)
        if st(ST.BGM) ~= 0 then
            tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        mc_nops(2)
    end
    if tile_sum({0x1b, 0x47, 0x3c}) ~= 0x10 then return end
    set_count(0)
    for _, t in ipairs({
        {0x11, 0x2e}, {0x0f, 0x1e}, {0x2a, 0x23}, {0x29, 0x23},
        {0x28, 0x23}, {0x24, 0x22}, {0x23, 0x22}, {0x22, 0x22},
        {0x32, 0x21}, {0x31, 0x21}, {0x30, 0x21}, {0x10, 0x17},
    }) do mc_tile(t[1], t[2]) end
    mc_door(0x47, 4)
    emit(OP.SET_TILE, 0x27, 6); emit(OP.SET_TILE, 0x25, 6)
    emit(OP.SET_TILE, 0x11, 6); emit(OP.SET_TILE, 0x0f, 6)
    mc_nops(2)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

-- Floor 50 (0x46bfa8): only with option A set (the R run / second round):
-- timer2on, then the ending: sub-dispatch 6 (run last), sixteen 0x0b/0x0c
-- openning9 frames 0xe8..0xf7 with sound 1, fade, hide images and message,
-- sprite 0x8c, sub-event 0x48 (0x3e + 10) and timer2on.
MONCHECK[50] = function()
    if st(ST.OPTION_A) == 0 then return end
    timer2on()
    tick(OP.SUBDISPATCH, SUB.GAMEOVER, 0)
    for k = 0xf7, 0xe8, -1 do
        emit(OP.NOP, 2, 0); emit(OP.SUBDISPATCH, SUB.OPENNING9, k)
        tick(OP.SOUND, SND.OPEN, 0)
    end
    tick(OP.SUBDISPATCH, SUB.FADE, 0)
    tick(OP.SUBDISPATCH, SUB.HIDE_IMAGES, 0)
    tick(OP.SUBDISPATCH, SUB.HIDE_MESSAGE, 0)
    tick(OP.SPRITE, 0x8c, 0)
    mc_nops(3)
    emit(OP.START_SUBEVENT, 0x3e, 0x63)
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

-- The hit poses: the player's (by facing) and the monster's.
local function battle_player_hit(target, tile, life_arg)
    local facing = (st(ST.FACING) - 1) * 8
    tick(OP.LIFE_DELTA, target, life_arg)
    emit(OP.NOP, tile, facing + 0xbf)
    emit(OP.MASKED_TILE, target, facing + 0xc0)
    tick(OP.SOUND, SND.TAISEN, 0)
end

local function battle_monster_hit(target, tile, hp_arg)
    tick(OP.MONSTER_DELTA, target, hp_arg)
    emit(OP.NOP, tile, 0xb8)
    emit(OP.MASKED_TILE, target, 0xb7)
    tick(OP.SOUND, SND.TAISEN, 0)
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
        emit(OP.SUBDISPATCH, SUB.MONCHECK, 5)
        emit(OP.ADD_GOLD, gold, 0)
        emit(OP.SPRITE, 0, 0)
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
        if big then
            for _, d in ipairs({-0x15, -0x16, -0x17, -0x0c, -0x0b, -0x0a,
                                -1, 0, 1}) do
                emit(OP.SET_TILE_SILENT, p + d, 6)
            end
        else
            emit(OP.SET_TILE_SILENT, p, 6)
        end
        tick(OP.SUBDISPATCH, SUB.PLAYER_ANIM, 0)
        local tile = get_tile(p)
        tick(OP.MONSTER_DELTA, target, 0)
        emit(OP.NOP, tile, 0xb8)
        emit(big and OP.BATTLE_HIT or OP.MASKED_TILE, target, 0xb7)
        tick(OP.SOUND, SND.TAISEN, 0)
        for i = 1, turns - 1 do
            aux.work_558 = i
            battle_player_hit(target, tile, 0)
            battle_monster_hit(target, tile, 0xb7)
        end
    else
        emit(OP.ALTAR, 0, 0)
        wait(6)
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
        for i = 1, turns do
            aux.work_558 = i
            battle_player_hit(target, tile, i == 1 and tile or 0xb8)
            battle_monster_hit(target, tile, 0)
        end
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
        emit(OP.SUBDISPATCH, SUB.MONCHECK, 5)                -- moncheck splice
        emit(OP.ADD_GOLD, gold, 0)
        emit(OP.SPRITE, 0, 0)
        aux.sprite = 1
        present("monswork", index)
        -- 0x47ff1e: [0xc7] " " name ". " [0xc8] " " gold [0xc9]
        status_cat(0, 0xc7, 2, 0, 0, monster_name_id(index), 2, 1,
                   0, 0xc8, 2, 0, 1, gold, 0, 0xc9)
        emit(OP.SET_TILE_SILENT, p, 6)
        tick(OP.SUBDISPATCH, SUB.PLAYER_ANIM, 0)                -- yusyahyouji1
        for i = 1, turns do
            aux.work_558 = i
            tick(OP.MONSTER_DELTA, target, 0xc1)
            emit(OP.NOP, tile, 0xb8)
            emit(OP.MASKED_TILE, target, 0xb7)
            tick(OP.SOUND, SND.TAISEN, 0)
            tick(OP.LIFE_DELTA, target, 0)
            emit(OP.NOP, tile, facing + 0xbf)
            emit(OP.MASKED_TILE, target, facing + 0xc0)
            tick(OP.SOUND, SND.TAISEN, 0)
        end
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
        tick(OP.ALTAR, 0, 0)
        wait(19)
        for i = 1, turns do
            aux.work_558 = i
            if i > 1 then
                tick(OP.MONSTER_DELTA, target, 0)
                emit(OP.NOP, tile, 0xb8)
                emit(OP.MASKED_TILE, target, 0xb7)
                tick(OP.SOUND, SND.TAISEN, 0)
                wait(1)
            end
            emit(OP.LIFE_DELTA, target, i == 1 and tile or 0xfa)
            emit(OP.NOP, tile, facing + 0xbf)
            emit(OP.MASKED_TILE, target, facing + 0xc0)
            tick(OP.SOUND, SND.TAISEN, 0)
        end
    end
    timer2on()
end
