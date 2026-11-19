-- floors.lua: Map events: mevent (handan raw 0x07) and ichicheck, the floor
-- 10..49 event dispatcher.
-- Loaded by builders.lua (include); see it for the API.

-- mevent (0x46397c): map events reached from handan raw 0x07.  Floor
-- switch 0x4639b6 / 0x4639e8.  Ported: floor 3 (0x463a2c, the opening Zeno
-- trigger) and floor 10 (idou, tile cleared, ichicheck).
--
-- Floor 3: the player steps onto the trigger tile (idou), the tile is
-- cleared 0x07 -> 0x06 (the one-shot), sub-event 10 (selector 0x14) is
-- queued with four waits, then Zeno appears at actor index 0x46.

local MEVENT_ICHICHECK = {      -- idou, clear the tile, ichicheck
    [10] = true, [15] = true, [24] = true, [30] = true, [33] = true,
    [38] = true, [43] = true,
}

builders.mevent = function()
    itemdel()
    local floor = st(ST.FLOOR)
    if floor == 32 or floor == 42 or floor == 48 then
        -- 0x463ec7 / 0x463f8e / 0x463fc9: ichicheck only
        return call("ichicheck")
    end
    if floor == 20 then                 -- 0x463c3b: player index 0x52, BGM 0x10
        call("idou")
        if hero_cell() ~= cell(5, 7) then return end
        set_tile(hero_cell(), RAW.FLOOR)
        if st(ST.BGM) ~= 0 then
            present("media_stop")
            set_state(ST.BGM, 0x10)
            present("bgm", 0x10)
        end
        return call("ichicheck")
    end
    if floor == 25 then                 -- 0x463e3e: BGM 7 when sound is on
        call("idou")
        set_tile(hero_cell(), RAW.FLOOR)
        if st(ST.BGM) ~= 0 then
            present("media_stop")
            set_state(ST.BGM, 7)
            present("bgm", 7)
        end
        return call("ichicheck")
    end
    if floor == 49 then                 -- 0x463fd2: BGM 0x13 when sound is on
        call("idou")
        set_tile(hero_cell(), RAW.FLOOR)
        if st(ST.BGM) ~= 0 then
            present("media_stop")
            set_state(ST.BGM, 0x13)
            present("bgm", 0x13)
        end
        return call("ichicheck")
    end
    if floor == 40 then                 -- 0x463f37: BGM 0x12 when sound is on
        call("idou")
        set_tile(hero_cell(), RAW.FLOOR)
        if st(ST.BGM) ~= 0 then
            present("media_stop")
            set_state(ST.BGM, 0x12)
            present("bgm", 0x12)
        end
        return call("ichicheck")
    end
    if MEVENT_ICHICHECK[floor] then  -- e.g. floor 10 (0x463bff)
        call("idou")
        set_tile(hero_cell(), RAW.FLOOR)
        return call("ichicheck")
    end
    if floor == 23 then                 -- 0x463c9b: the wall at the target
        play(function(t)
            t:wait(2)
            t:tick(OP.SOUND, SND.OPEN, 0)
            t:tick(OP.SET_TILE, aux.target_index, 8)
            t:tick(OP.SPRITE, 0x72, 0)
        end)
        timer2on()
        return
    end
    if floor ~= 3 then
        return                          -- no case: nothing happens
    end
    call("idou")
    timer2on()
    set_tile(hero_cell(), RAW.FLOOR)
    present("button_caption", 0xda)
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:call("zenolivework", function() aux.work_558 = 0x46 end)
        t:wait(4)
        t:cmd(OP.START_SUBEVENT, 10, 0)
    end)
end


---------------------------------------------------------------------------
-- ichicheck (0x46cb1c): floor 10..49 event dispatcher, called from mevent
-- after the player stepped onto the floor's trigger tile.  Table 0x46cb63
-- (index floor - 10) selects a handler; the common exit 0x4770a1 calls
-- timer2on when anything was queued.  Only floor 10 is ported.

-- The ichicheck handlers below are written in play order (play()).
--
-- Floor 10 (0x46cbc7), area 1's boss room (parity: floor10* vectors).
--   * state[0x6a] == 0 (first time): with the music on the tune stops;
--     sub-event 13; the walls at 59 and 61 open (sound 2 each); the actor
--     at 38 walks up to 5; doors close at 27 and 71 and doors 36 and 40
--     open (door kind 4); 40 single-cell moves (hit sound each) bring the
--     side-room guards to the middle; doors 36 and 40 close (kind 5); with
--     the music on, tune 0x0f.
--   * state[0x6a] != 0 (set by Button1Click sub-event 26): cell 99
--     becomes the thief 0x15, who walks from cell 99 round to 104;
--     sub-event 27; then cell 104 is cleared.

local F10_MARCH = {          -- {from, to}, in the order they happen
    {34,35}, {35,36}, {36,37}, {37,48}, {48,59}, {59,60}, {42,41}, {41,40},
    {40,39}, {39,38}, {24,35}, {35,36}, {36,37}, {37,48}, {48,59}, {30,41},
    {41,40}, {40,39}, {39,50}, {50,61}, {23,34}, {34,35}, {35,36}, {36,37},
    {37,48}, {31,42}, {42,41}, {41,40}, {40,39}, {39,50}, {22,33}, {33,34},
    {34,35}, {35,36}, {36,37}, {32,43}, {43,42}, {42,41}, {41,40}, {40,39},
}

local F10_THIEF_WALK = {
    {99,88}, {88,77}, {77,78}, {78,79}, {79,90}, {90,101},
    {101,112}, {112,113}, {113,114}, {114,103}, {103,104},
}

local function ichicheck_floor10()
    if st(ST.FLOOR10_DONE) ~= 0 then
        play(function(t)
            t:tick(OP.SOUND, SND.GET, 0)
            t:tick(OP.SET_TILE, 99, 0x15)
            for _, m in ipairs(F10_THIEF_WALK) do
                t:tick(OP.MONSTER_MOVE, m[1], m[2])
            end
            t:tick(OP.START_SUBEVENT, 0x11, 0)
            t:tick(OP.SOUND, SND.GET, 0)
            t:tick(OP.SET_TILE, 104, 6)
        end)
        return
    end
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:tick(OP.START_SUBEVENT, 0x0d, 0)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 59, 6)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 61, 6)
        t:tick(OP.MONSTER_MOVE, 38, 27)
        t:tick(OP.MONSTER_MOVE, 27, 16)
        t:tick(OP.MONSTER_MOVE, 16, 5)
        door_sound(t, 27, 5)
        door_sound(t, 71, 5)
        door_sound(t, 36, 4)
        door_sound(t, 40, 4)
        for _, m in ipairs(F10_MARCH) do
            t:tick(OP.MONSTER_MOVE, m[1], m[2])
            t:tick(OP.SOUND, SND.ZENO, 0)
            t:wait(2)
        end
        t:wait(2)
        door_sound(t, 36, 5)
        door_sound(t, 40, 5)
        if st(ST.BGM) ~= 0 then
            set_state(ST.BGM, 0x0f)
            t:tick(OP.SOUND, SND.BGM, 0)
        end
    end)
end

local function ichicheck_floor15()
    play(function(t)
        t:wait(2)
        door_sound(t, 93, 5, 5)
    end)
end

local function ichicheck_floor25()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    play(function(t)
        t:wait(2)
        door_sound(t, 104, 5)
        t:tick(OP.START_SUBEVENT, 0x17, 0)
    end)
end

local function ichicheck_floor30()
    play(function(t)
        t:wait(2)
        door_sound(t, 38, 5)
        door_sound(t, 71, 5)
    end)
end

local function ichicheck_floor38()
    play(function(t)
        t:wait(2)
        door_sound(t, 45, 8)
        t:wait(1)
        t:tick(OP.SPRITE, 0x72, 0)
    end)
end

local function ichicheck_floor48()
    play(function(t)
        t:wait(2)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 0, 0x5d)
    end)
end

local function ichicheck_floor33()
    play(function(t)
        t:wait(2)
        if hero_cell() == cell(8, 9) then
            door_sound(t, 106, 8)
            t:wait(1)
            t:tick(OP.SPRITE, 0x72, 0)
        else
            door_sound(t, 42, 5)
            door_sound(t, 86, 5)
        end
    end)
end

local function ichicheck_floor43()
    if get_tile(8) ~= 0x5d then
        wait(1)
        return
    end
    play(function(t)
        t:wait(2)
        t:tick(OP.SOUND, SND.GET, 0)
        t:cmd(OP.MONSTER_MOVE, 8, 9)
        t:wait(5)
        t:tick(OP.SOUND, SND.GET, 0)
        t:cmd(OP.MONSTER_MOVE, 9, 10)
        t:wait(5)
        door_sound(t, 6, 8)
        t:wait(3)
        door_sound(t, 9, 8)
        t:wait(1)
    end)
end

local function ichicheck_floor49()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:wait(2)
        door_sound(t, 71, 5)
        t:cmd(OP.SUBDISPATCH, SUB.ZENOLIVEWORK, 0)
        t:wait(3)
        t:tick(OP.START_SUBEVENT, 45, 0)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM, 0)
        end
        t:wait(1)
        t:tick(OP.SUBDISPATCH, SUB.MADOUSHI2, 0)
    end)
end

local function ichicheck_floor42()
    set_tile_at(35, 103, 6)
    play(function(t)
        t:wait(6)
        t:call("zenolivework", function() aux.work_558 = 0x3c end)
        t:cmd(OP.START_SUBEVENT, 41, 0)
        t:wait(5)
        t:cmd(OP.START_SUBEVENT, 42, 0)
        t:wait(5)
        t:call("madoushilivework", function()
            aux.work_558 = 0x47
            aux.event_selector = 0x5d
            aux.work_560 = 0x51
            aux.work_564 = 0x53
        end)
        t:tick(OP.START_SUBEVENT, 43, 0)
        t:tick(OP.SOUND, SND.ZENO, 0)
        t:cmd(OP.MASKED_TILE, 82, 0xb7)
        t:cmd(OP.NOP, get_tile(82), 0xb8)
        t:wait(4)
        t:cmd(OP.SET_TILE, 82, 6)
        t:wait(7)
        for _, c in ipairs({71, 93, 81, 83}) do
            t:cmd(OP.SET_TILE, c, 6)
        end
        t:wait(6)
        t:cmd(OP.START_SUBEVENT, 44, 0)
        t:wait(7)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 60, 6)
    end)
    timer2on()
end

local function ichicheck_floor20()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    for _, i in ipairs({0x30, 0x31, 0x32, 0x3b, 0x3d, 0x46, 0x47, 0x48}) do
        set_tile(i, RAW.FLOOR)
    end
    set_tile(0x3c, 0x5f)
    -- 0x46f974 writes the map without redrawing it: the screen keeps the
    -- ring of bats round an empty cell until the first banpaiawork stage.
    present("vampire_block")
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:wait(2)
        door_sound(t, 93, 5)
        t:tick(OP.SOUND, SND.F20, 0)
        for stage = 1, 32 do
            t:tick(OP.SUBDISPATCH, SUB.BANPAIAWORK, stage)
        end
        t:tick(OP.START_SUBEVENT, 20, 0)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM, 0)
        end
    end)
end

-- Floor 32: the thief comes down the column from cell 9 and fights at 104.
local F32_DOWN = { {9,8}, {8,7}, {7,6}, {6,5}, {5,16}, {16,27}, {27,38},
                   {38,49}, {49,60} }
local F32_LAST = { {60,71}, {71,82}, {82,93}, {93,104} }

local function ichicheck_floor32()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    play(function(t)
        t:wait(10)
        t:tick(OP.SOUND, SND.GET, 0)
        t:cmd(OP.SET_TILE, 9, 0x85)
        t:wait(3)
        for i, m in ipairs(F32_DOWN) do
            if i > 1 then t:wait(3) end
            t:cmd(OP.MONSTER_MOVE, m[1], m[2])
        end
        t:wait(7)
        t:cmd(OP.START_SUBEVENT, 30, 0)
        t:wait(3)
        for _, m in ipairs(F32_LAST) do
            t:cmd(OP.MONSTER_MOVE, m[1], m[2])
            t:wait(3)
        end
        t:tick(OP.SUBDISPATCH, SUB.TAISEN2, 104)
    end)
end

local function ichicheck_floor24()
    timer2on()
    present("button_caption")          -- Button1.Caption := 'OK' (0x4704fb)
    if st(ST.ZENO_WEAK) == 1 then      -- 0x470505: Zeno's entry x 10
        set_state(ST.ZENO_WEAK, 0)
        set_zeno(monster_stat(0x0f, "hp") * 10, monster_stat(0x0f, "attack") * 10,
                 monster_stat(0x0f, "defense") * 10)
    end
    play(function(t)
        if st(ST.BGM) ~= 0 then
            t:tick(OP.SOUND, SND.BGM_STOP, 0)
        end
        t:wait(1)
        for v = 25, 50 do
            t:wait(1)
            t:tick(OP.SOUND, SND.OPEN, 0)
            t:cmd(OP.SUBDISPATCH, SUB.CHANGE_FLOOR, v)
        end
        t:wait(11)
        t:cmd(OP.START_SUBEVENT, 48, 0)
        t:wait(5)
        t:cmd(OP.START_SUBEVENT, 49, 0)
        t:wait(7)
        t:tick(OP.SOUND, SND.F3, 0)
        for v = 166, 182 do
            t:cmd(OP.MASKED_TILE, 49, v)
            t:cmd(OP.NOP, 21, 91)
        end
        t:wait(6)
        t:cmd(OP.SET_TILE_SILENT, 49, 91)
        if st(ST.BGM) ~= 0 then
            t:cmd(OP.STOP_BGM, 0, 0)
        end
        for i, sub in ipairs({50, 51, 52, 53, 54, 55, 56, 57}) do
            if i > 1 then t:wait(5) end
            t:cmd(OP.START_SUBEVENT, sub, 0)
        end
        t:wait(3)
        if st(ST.OPTION_A) ~= 0 and st(ST.OPTION_B) == 0 then
            set_zeno(1000, 0x271, 0x7d)      -- 0x47055d
            t:tick(OP.SOUND, SND.GET, 0)
            t:cmd(OP.MONSTER_MOVE, 49, 60)
            t:wait(3)
            t:tick(OP.SOUND, SND.GET, 0)
            t:cmd(OP.MONSTER_MOVE, 60, 71)
            t:wait(3)
            t:tick(OP.SUBDISPATCH, SUB.TAISEN2, 71)
        end
    end)
end

-- Floor 40: a knight walks a path of cells and fights at its end (71).
local function knight(t, path, pause)
    for i = 1, #path - 1 do
        t:tick(OP.MONSTER_MOVE, path[i], path[i + 1])
    end
    t:cmd(OP.SUBDISPATCH, SUB.TAISEN2, path[#path])
    if pause ~= false then t:wait(1) end
end

local function ichicheck_floor40()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    local traps = {
        {37, 0x8d}, {36, 0x8d}, {35, 0x8d},
        {39, 0x8b}, {40, 0x8b}, {41, 0x8b},
        {14, 0x89}, {13, 0x89}, {12, 0x89},
        {18, 0x87}, {19, 0x87}, {20, 0x87},
        {16, 0x85},
    }
    for _, c in ipairs(traps) do
        if get_tile(c[1]) == RAW.FLOOR then
            set_tile(c[1], c[2])
        end
    end
    play(function(t)
        t:wait(2)
        door_sound(t, 82, 5)
        t:tick(OP.START_SUBEVENT, 34, 0)
        knight(t, {37, 48, 49, 60, 71})
        knight(t, {36, 37, 48, 49, 60, 71})
        knight(t, {35, 36, 37, 48, 49, 60, 71})
        t:tick(OP.START_SUBEVENT, 35, 0)
        knight(t, {39, 50, 49, 60, 71})
        knight(t, {40, 39, 50, 49, 60, 71})
        knight(t, {41, 40, 39, 50, 49, 60, 71})
        t:tick(OP.START_SUBEVENT, 36, 0)
        knight(t, {14, 25, 26, 37, 38, 49, 60, 71})
        knight(t, {13, 14, 25, 26, 37, 38, 49, 60, 71})
        knight(t, {12, 13, 14, 25, 26, 37, 38, 49, 60, 71})
        t:tick(OP.START_SUBEVENT, 37, 0)
        knight(t, {18, 29, 28, 39, 38, 49, 60, 71})
        knight(t, {19, 18, 29, 28, 39, 38, 49, 60, 71})
        knight(t, {20, 19, 18, 29, 28, 39, 38, 49, 60, 71})
        t:tick(OP.START_SUBEVENT, 38, 0)
        knight(t, {16, 27, 38, 49, 60, 71}, false)
        t:wait(3)
        door_sound(t, 82, 4)
        t:wait(1)
        t:cmd(OP.SPRITE, 4, 0)
        t:wait(3)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 5, 11)
        t:cmd(OP.START_SUBEVENT, 39, 0)
        t:cmd(OP.SET_TILE_SILENT, 71, 133)
        t:wait(6)
        t:cmd(OP.MONSTER_MOVE, 71, 60)
        t:wait(3)
        t:cmd(OP.MONSTER_MOVE, 60, 49)
        t:wait(3)
        t:cmd(OP.MONSTER_MOVE, 49, 38)
        t:wait(3)
        t:cmd(OP.MONSTER_MOVE, 38, 27)
        t:wait(3)
        t:tick(OP.MONSTER_MOVE, 27, 16)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SET_TILE, 16, 6)
        t:tick(OP.SUBDISPATCH, SUB.MONCHECK, 0)
    end)
end

local ICHICHECK = {
    [10] = ichicheck_floor10,
    [15] = ichicheck_floor15,
    [20] = ichicheck_floor20,
    [24] = ichicheck_floor24,
    [25] = ichicheck_floor25,
    [30] = ichicheck_floor30,
    [32] = ichicheck_floor32,
    [33] = ichicheck_floor33,
    [38] = ichicheck_floor38,
    [40] = ichicheck_floor40,
    [42] = ichicheck_floor42,
    [43] = ichicheck_floor43,
    [48] = ichicheck_floor48,
    [49] = ichicheck_floor49,
}

builders.ichicheck = function()
    local floor = st(ST.FLOOR)
    if floor >= 10 and floor <= 49 then
        local h = ICHICHECK[floor]
        if h then
            h()
        end
    end
    if get_count() >= 1 then timer2on() end   -- 0x4770a1
end
builders.floor10 = builders.ichicheck   -- vector name (entry 0x46cb1c)
