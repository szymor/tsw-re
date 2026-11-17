-- handan.lua: handan: the tile action in front of the hero -- secret walls,
-- doors, keys, items and gear, hidden passages, stairs.
-- Loaded by builders.lua (include); see it for the API.

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

local function secret_wall()
    timer2on()
    aux.sprite = 1
    present("status_text", 0x89)
    play(function(t)
        door_sound(t, aux.target_index, 7)
        t:wait(1)
        t:tick(OP.SPRITE, 1, 0)
    end)
end

---------------------------------------------------------------------------
-- handan cases 1..3: doors (0x4444f0).  A key of the door's colour opens
-- it: sprite 4/5/6, item panel, the door animation and sound 1; without one
-- only the "locked" sprite 0x10/0x11/0x12 is queued.  The player stays.
-- doorcheck (0x47beac) runs first and may queue a floor-39/43 extra.

local DOOR_KEYS = { [1] = {8, 0x10, 4}, [2] = {10, 0x11, 5}, [3] = {9, 0x12, 6} }

-- doorcheck's extras, in play order (after the door's own steps).
local function doorcheck(t)
    local target = aux.target_index
    local floor = st(ST.FLOOR)
    if floor == 0x27 then
        local ring = tile_sum({12, 16, 34, 36, 56, 58, 60}) == 7
        local function wall()
            t:wait(2)
            tile_sound(t, 36, 0x2b)
        end
        if ring and get_tile(14) == RAW.FLOOR and target == 0x26 then wall() end
        if ring and get_tile(38) == RAW.FLOOR and target == 0x0e then wall() end
    elseif floor == 0x2b and target == 7 and get_tile(8) == RAW.MAGIC_SERGEANT then
        t:cmd(OP.SET_TILE, 7, 7)
    end
end

function door(raw)
    local k = DOOR_KEYS[raw]
    local opened = false
    aux.sprite = k[2]
    if st(k[1]) > 0 then
        opened = true
        set_state(k[1], st(k[1]) - 1)
        aux.sprite = k[3]
    end
    play(function(t)
        if opened then
            door_sound(t, aux.target_index, raw)
            t:cmd(OP.SHOW_ITEMS, 0, 0)
        end
        t:cmd(OP.SPRITE, aux.sprite, 0)
        doorcheck(t)
    end)
    timer2on()
end

-- The pickup tail shared by keys, items and equipment: sprite, item panel,
-- sound 2, the cell becomes floor and idou steps onto it.
local function pickup_tail()
    play(function(t)
        t:tick(OP.SOUND, SND.GET, 0)
        t:cmd(OP.SHOW_ITEMS, 0, 0)
        t:cmd(OP.SPRITE, aux.sprite, 0)
    end)
    timer2on()
    set_tile(aux.target_index, RAW.FLOOR)
    return call("idou")
end

-- handan 0x1d..0x1f: keys (yellow, red, blue) with ListBox2 0x80/0x82/0x81.
local KEY_PICKUP = { [0x1d] = {8, 0x80}, [0x1e] = {9, 0x82}, [0x1f] = {10, 0x81} }

local function key_pickup(raw)
    local k = KEY_PICKUP[raw]
    set_state(k[1], st(k[1]) + 1)
    aux.sprite = 1
    present("status_text", k[2])
    return pickup_tail()
end

-- handan 0x20..0x32: potions, gems and special items.  The potion and gem
-- amounts scale with the floor's tens ((floor + 9) / 10) and the
-- difficulty; their status line is ListBox2[a] " " n ListBox2[0xa6], or
-- " point up!" (a literal, ui.tsv entry 2) for a gem worth 1.  Raw 0x24
-- has no case: only the tail runs, with whatever sprite is current.
local ITEM_SLOT = {   -- raw -> special-item slot, value, sprite
    [0x25] = {1, 1, 0x16}, [0x26] = {2, 1, 0x15}, [0x27] = {3, 1, 0x28},
    [0x28] = {4, 1, 0x29}, [0x29] = {5, 1, 0x2a}, [0x2a] = {6, 1, 0x2b},
    [0x2b] = {7, 3, 0x2c}, [0x2c] = {8, 1, 0x2d}, [0x2d] = {9, 1, 0x2e},
    [0x2e] = {10, 1, 0x2f}, [0x2f] = {11, 1, 0x30}, [0x30] = {12, 1, 0x31},
    [0x31] = {13, 1, 0x32}, [0x32] = {14, 1, 0x33},
}

local function item_pickup(raw)
    local tens = (st(ST.FLOOR) + 9) // 10
    local diff1 = st(ST.OPTION_A) + 1
    if raw == 0x20 or raw == 0x21 then
        local n = tens * (raw == 0x20 and 0x32 or 200) * diff1
        set_state(ST.LIFE, st(ST.LIFE) + n)
        aux.sprite = 1
        present("status_concat", raw == 0x20 and 0xa2 or 0xa3, n, 0xa6)
    elseif raw == 0x22 or raw == 0x23 then
        local n = tens * diff1
        local stat = raw == 0x22 and ST.ATTACK or ST.DEFENSE
        set_state(stat, st(stat) + n)
        aux.sprite = 1
        present("status_concat", raw == 0x22 and 0xa4 or 0xa5, n,
                n == 1 and -2 or 0xa6)
    elseif ITEM_SLOT[raw] then
        local it = ITEM_SLOT[raw]
        set_state(ST.SPECIAL0 + it[1], it[2])
        aux.sprite = it[3]
    end
    return pickup_tail()
end

-- handan 0x33..0x3c: swords (attack) and shields (defense), level 1..5,
-- the matching "picked" flag, ListBox2 0x98..0xa1 " " n ListBox2[0xa6].
local EQUIP_GAIN = { 10, 0x14, 0x28, 0x32, 100 }

local function equip_pickup(raw)
    local diff1 = st(ST.OPTION_A) + 1
    local shield = raw >= 0x38
    local level = shield and raw - 0x37 or raw - 0x32
    local n = diff1 * EQUIP_GAIN[level]
    if shield then
        set_state(ST.DEFENSE, st(ST.DEFENSE) + n)
        set_state(ST.SHIELD_LEVEL, level)
        set_state(ST.SHIELD_FLAG0 + level - 1, 1)
    else
        set_state(ST.ATTACK, st(ST.ATTACK) + n)
        set_state(ST.WEAPON_LEVEL, level)
        set_state(ST.SWORD_FLAG0 + level - 1, 1)
    end
    aux.sprite = 1
    present("status_concat", 0x98 + raw - 0x33, n, 0xa6)
    return pickup_tail()
end

-- handan case 0x0a (0x444ba0): a hidden passage opens at one cell of
-- floors 12, 16 and 19 (when that cell is the target) and on floor 41
-- (cell 0x14 becomes a magician once cell 12 is floor).
local HIDDEN_PASSAGE = { [0x0c] = {10, 0x13}, [0x10] = {0x78, 0x19},
                         [0x13] = {0x1b, 0x27} }

local function hidden_passage()
    local floor = st(ST.FLOOR)
    local index, tile
    local h = HIDDEN_PASSAGE[floor]
    if h then
        if aux.target_index ~= h[1] then return end
        index, tile = h[1], h[2]
    elseif floor == 0x29 and get_tile(12) == RAW.FLOOR then
        index, tile = 0x14, 0x97
    else
        return
    end
    play(function(t)
        t:wait(2)
        tile_sound(t, index, tile)
    end)
    timer2on()
end

-- handan 0x1b/0x1c (0x445397): BGM 13; with floor-25 cell 60 clear the
-- floor-24 cells are rewritten builder-side (as moncheck 25 does); the
-- floor-50 cell 49 becomes 0x15; tile 0x3c -> 0x1b; sub-events 36 and 35.
local function raw_1b()
    if st(ST.BGM) ~= 0 and st(ST.BGM) ~= 0x0d then
        set_state(ST.BGM, 0x0d)      -- soundplay
    end
    if get_tile_at(25, 60) == RAW.FLOOR then
        set_tile_at(24, 4, 8)
        set_tile_at(24, 6, 8)
        set_tile_at(24, 16, 6)
        set_tile_at(24, 27, 6)
        set_tile_at(24, 38, 6)
        set_tile_at(24, 5, 7)
    end
    set_tile_at(50, 49, 0x15)          -- g_map[0x1837]
    play(function(t)
        t:wait(2)
        t:tick(OP.START_SUBEVENT, 0x19, 0)
        t:tick(OP.START_SUBEVENT, 0x1a, 0)
        t:tick(OP.SET_TILE, 60, 0x1b)
    end)
    timer2on()
    present("button_caption")          -- Button1.Caption := 'OK'
end

-- handan 0x0b/0x0c (0x444d2e): stairs.  The floor changes (43 <-> 45
-- skip 44), a few one-shot cells are cleared on the way, the player lands
-- at the floor's arrival index (map block byte floor*0x7b for up, +1 for
-- down) facing the stairs back (kaidanupcheck/kaidandowncheck), floors 32
-- and 42 run their mevent once, and kaidanwork (0x442dc8) queues the
-- two-pass 0x15 floor wipe (with the floor's BGM at step 20) and sound 9
-- at the stairs speed (2/6/10 ms).
function stairs_face(raw)
    local x, y = st(ST.X), st(ST.Y)
    local p = cell(x, y)
    aux.work_558 = p
    if y >= 1 and y <= 10 and get_tile(p - 11) == raw then set_state(ST.FACING, 1) end
    if x >= 1 and x <= 10 and get_tile(p - 1) == raw then set_state(ST.FACING, 3) end
    if x >= 0 and x < 10 and get_tile(p + 1) == raw then set_state(ST.FACING, 2) end
    aux.event_selector = p + 11
    if y >= 0 and y < 10 and get_tile(p + 11) == raw then set_state(ST.FACING, 4) end
end

-- In play order: sound 9, the cover pass (op 0x15 mode 0) over the spiral's
-- cells 1..121, then the reveal pass (mode 1), with the new floor's tune
-- (op 0x0f) after its 102nd cell when the music is on.
local function kaidanwork()
    present("timer1", 0)
    play(function(t)
        t:tick(OP.SOUND, SND.KAI, 0)
        for c = 1, 0x79 do t:cmd(OP.DRAW_ACTOR, 0, c) end
        for c = 1, 0x79 do
            t:cmd(OP.DRAW_ACTOR, 1, c)
            if st(ST.BGM) ~= 0 and c == 0x66 then t:cmd(OP.STOP_BGM, 0, 0) end
        end
    end)
    present("timer2_stairs")
    timer2on()
end

-- stairs stays in append order: floors 32 and 42 run mevent between its
-- steps, and mevent's own effects must keep their place.
local function stairs(raw)
    -- The screen keeps the floor being left until kaidanwork's op-0x15
    -- pass covers it (presentation: the remake snapshots it here).
    present("wipe_begin", st(ST.FLOOR), hero_cell(),
            st(ST.FACING))
    wait(2)
    if st(ST.BGM) ~= 0 then
        present("timer1", 0)
        present("media_stop")
    end
    aux.current_monster = -1           -- g_monster_index
    present("monshyouji", aux.target_index, -1)
    aux.sprite = 1
    present("status_text", 0xf3)
    present("disp")
    local floor = st(ST.FLOOR)
    if raw == 0x0b then
        if floor == 20 and get_tile_at(20, 49) == 0x17 then set_tile_at(20, 49, 6) end
        if floor == 25 and get_tile_at(25, 71) == 0x17 then set_tile_at(25, 71, 6) end
        if floor == 40 and get_tile_at(40, 60) == 0x17 then set_tile_at(40, 60, 6) end
        floor = floor == 0x2b and 0x2d or floor + 1
        set_state(ST.FLOOR, floor)
        if st(ST.FLOOR_LIMIT) < floor then
            set_state(ST.FLOOR_LIMIT, floor)
            -- (sic) handan+0x996 compares state[35], not the floor as
            -- Button8Click does.
            if st(ST.SWORD_FLAG0 + 3) == 0x23 and get_tile_at(35, 91) + get_tile_at(2, 120) == 0x1b then
                set_tile_at(2, 120, 6)
            end
        end
        local arrive = get_map_byte(floor * 123)
        set_state(ST.X, arrive % MAP_COLS)
        set_state(ST.Y, arrive // MAP_COLS)
        stairs_face(0x0c)
        if floor == 0x20 and st(ST.FLOOR32_MEVENT) == 0 then
            call("mevent")
            set_state(ST.FLOOR32_MEVENT, 1)
        end
        if floor == 0x2a and st(ST.FLOOR42_MEVENT) == 0 then
            call("mevent")
            set_state(ST.FLOOR42_MEVENT, 1)
        end
        tick(OP.SPRITE, 7, 0)
    else
        if floor == 25 and get_tile_at(25, 71) == 0x17 then set_tile_at(25, 71, 6) end
        if floor == 49 and get_tile_at(49, 16) == 0x17 then set_tile_at(49, 16, 6) end
        floor = floor == 0x2d and 0x2b or floor - 1
        set_state(ST.FLOOR, floor)
        if st(ST.FLOOR_LIMIT) == 0x1d and floor == 0x1c then
            set_tile_at(29, 38, 6)
            set_tile_at(29, 27, 6)
            set_tile_at(29, 17, 0x15)
        end
        local arrive = get_map_byte(floor * 123 + 1)
        set_state(ST.X, arrive % MAP_COLS)
        set_state(ST.Y, arrive // MAP_COLS)
        stairs_face(0x0b)
        tick(OP.SPRITE, 8, 0)
    end
    kaidanwork()
    aux.current_monster = -1
    present("monshyouji", aux.target_index, -1)
    itemlive()
    timer2on()
end

builders.handan = function()
    local raw = get_tile(aux.target_index)
    if raw >= 1 and raw <= 3 then
        return door(raw)
    elseif raw >= 0x1d and raw <= 0x1f then
        return key_pickup(raw)
    elseif raw >= 0x20 and raw <= 0x32 then
        return item_pickup(raw)
    elseif raw >= 0x33 and raw <= 0x3c then
        return equip_pickup(raw)
    elseif raw == RAW.FLOOR then
        return call("idou")
    elseif raw == RAW.ZENO_TRIGGER then
        return call("mevent")
    elseif raw == RAW.SECRET_WALL then
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
    elseif raw == 0x0a then
        return hidden_passage()
    elseif raw == 0x1b or raw == 0x1c then
        return raw_1b()
    elseif (raw >= 0x3d and raw <= 0x5a) or (raw >= 0x5d and raw <= 0x72)
        or raw == 0x7a or (raw >= 0x85 and raw <= 0x9d) then
        return call("taisen")          -- 0x445b0e
    elseif raw == 0x5b or raw == 0x5c then
        return zeno_room()
    elseif raw == 0x0b or raw == 0x0c then
        return stairs(raw)
    end
    -- Every other raw (0x00, 0x04, 0x05, 0x08, 0x0d..0x0f, 0x11, 0x12)
    -- has no case: nothing happens and the player stays.
end
