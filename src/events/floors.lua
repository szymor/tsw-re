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
        tick(OP.SPRITE, 0x72, 0)
        tick(OP.SET_TILE, aux.target_index, 8)
        tick(OP.SOUND, SND.OPEN, 0)
        wait(2)
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
    emit(OP.START_SUBEVENT, 10, 0)
    wait(1)
    wait(1)
    wait(1)
    wait(1)
    aux.work_558 = 0x46
    call("zenolivework")
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
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
--   * state[0x6a] != 0 (set by Button1Click sub-event 26): cell 99
--     becomes the thief 0x15, who walks from cell 88 round to 104;
--     sub-event 27; then cell 104 is cleared.

local function f10_door(index, kind)
    doorstackin(index, kind)
    tick(OP.SOUND, SND.OPEN, 0)
end

local function f10_step(from, to)
    wait(3)
    emit(OP.SOUND, SND.ZENO, 0); tick(OP.MONSTER_MOVE, from, to)
end

local F10_MARCH = {
    {40,39}, {41,40}, {42,41}, {43,42}, {32,43}, {36,37}, {35,36}, {34,35},
    {33,34}, {22,33}, {39,50}, {40,39}, {41,40}, {42,41}, {31,42}, {37,48},
    {36,37}, {35,36}, {34,35}, {23,34}, {50,61}, {39,50}, {40,39}, {41,40},
    {30,41}, {48,59}, {37,48}, {36,37}, {35,36}, {24,35}, {39,38}, {40,39},
    {41,40}, {42,41}, {59,60}, {48,59}, {37,48}, {36,37}, {35,36}, {34,35},
}

local F10_THIEF_WALK = {
    {103,104}, {114,103}, {113,114}, {112,113}, {101,112},
    {90,101}, {79,90}, {78,79}, {77,78}, {88,77},
    {99,88},
}

local function ichicheck_floor10()
    if st(ST.FLOOR10_DONE) ~= 0 then
        tick(OP.SET_TILE, 0x68, 6)
        tick(OP.SOUND, SND.GET, 0)
        tick(OP.START_SUBEVENT, 0x11, 0)
        for _, m in ipairs(F10_THIEF_WALK) do
            tick(OP.MONSTER_MOVE, m[1], m[2])
        end
        tick(OP.SET_TILE, 0x63, 0x15)
        tick(OP.SOUND, SND.GET, 0)
        return
    end
    if st(ST.BGM) ~= 0 then
        set_state(ST.BGM, 0x0f)
        tick(OP.SOUND, SND.BGM, 0)
    end
    f10_door(40, 5)
    f10_door(36, 5)
    wait(2)
    for _, m in ipairs(F10_MARCH) do f10_step(m[1], m[2]) end
    f10_door(40, 4)
    f10_door(36, 4)
    f10_door(71, 5)
    f10_door(27, 5)
    tick(OP.MONSTER_MOVE, 16, 5)
    tick(OP.MONSTER_MOVE, 27, 16)
    tick(OP.MONSTER_MOVE, 38, 27)
    tick(OP.SET_TILE, 61, 6)
    tick(OP.SOUND, SND.GET, 0)
    tick(OP.SET_TILE, 59, 6)
    tick(OP.SOUND, SND.GET, 0)
    tick(OP.START_SUBEVENT, 0x0d, 0)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

local function ichicheck_floor15()
    doorstackin(0x5d, 5)
    tick(OP.SOUND, SND.OPEN, 5)
    wait(2)
end

local function ichicheck_floor25()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    tick(OP.START_SUBEVENT, 0x17, 0)
    doorstackin(0x68, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
end

local function ichicheck_floor30()
    doorstackin(0x47, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    doorstackin(0x26, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
end

local function ichicheck_floor38()
    tick(OP.SPRITE, 0x72, 0)
    wait(1)
    doorstackin(0x2d, 8)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
end

local function ichicheck_floor48()
    tick(OP.SET_TILE, 0, 0x5d)
    tick(OP.SOUND, SND.GET, 0)
    wait(2)
end

local function ichicheck_floor33()
    if hero_cell() == cell(8, 9) then
        tick(OP.SPRITE, 0x72, 0)
        wait(1)
        doorstackin(0x6a, 8)
        tick(OP.SOUND, SND.OPEN, 0)
        wait(2)
    else
        doorstackin(0x56, 5)
        tick(OP.SOUND, SND.OPEN, 0)
        doorstackin(0x2a, 5)
        tick(OP.SOUND, SND.OPEN, 0)
        wait(2)
    end
end

local function ichicheck_floor43()
    if get_tile(8) ~= 0x5d then
        wait(1)
        return
    end
    wait(1)
    doorstackin(9, 8)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(3)
    doorstackin(6, 8)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(5)
    emit(OP.MONSTER_MOVE, 9, 10)
    tick(OP.SOUND, SND.GET, 0)
    wait(5)
    emit(OP.MONSTER_MOVE, 8, 9)
    tick(OP.SOUND, SND.GET, 0)
    wait(2)
end

local function ichicheck_floor49()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    tick(OP.SUBDISPATCH, SUB.MADOUSHI2, 0)
    wait(1)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM, 0)
    end
    tick(OP.START_SUBEVENT, 45, 0)
    wait(3)
    emit(OP.SUBDISPATCH, SUB.ZENOLIVEWORK, 0)
    doorstackin(0x47, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

local function ichicheck_floor42()
    set_tile_at(35, 103, 6)
    tick(OP.SET_TILE, 60, 6)
    tick(OP.SOUND, SND.GET, 0)
    wait(7)
    emit(OP.START_SUBEVENT, 44, 0)
    wait(6)
    for _, t in ipairs({83, 81, 93, 71}) do
        emit(OP.SET_TILE, t, 6)
    end
    wait(7)
    emit(OP.SET_TILE, 82, 6)
    wait(4)
    emit(OP.NOP, get_tile(0x52), 0xb8)
    emit(OP.MASKED_TILE, 82, 0xb7)
    tick(OP.SOUND, SND.ZENO, 0)
    tick(OP.START_SUBEVENT, 43, 0)
    aux.work_558 = 0x47
    aux.event_selector = 0x5d
    aux.work_560 = 0x51
    aux.work_564 = 0x53
    call("madoushilivework")
    wait(5)
    emit(OP.START_SUBEVENT, 42, 0)
    wait(5)
    emit(OP.START_SUBEVENT, 41, 0)
    aux.work_558 = 0x3c
    call("zenolivework")
    wait(6)
    timer2on()
end

local function ichicheck_floor20()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM, 0)
    end
    for _, i in ipairs({0x30, 0x31, 0x32, 0x3b, 0x3d, 0x46, 0x47, 0x48}) do
        set_tile(i, RAW.FLOOR)
    end
    set_tile(0x3c, 0x5f)
    tick(OP.START_SUBEVENT, 20, 0)
    for stage = 32, 1, -1 do
        tick(OP.SUBDISPATCH, SUB.BANPAIAWORK, stage)
    end
    tick(OP.SOUND, SND.F20, 0)
    doorstackin(93, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
end

local function ichicheck_floor32()
    present("button_caption", 0xda)   -- Button1.Caption := ListBox2[0xda]
    tick(OP.SUBDISPATCH, SUB.TAISEN2, 104)
    local moves = {
        {93,104}, {82,93}, {71,82}, {60,71},
    }
    for _, m in ipairs(moves) do
        wait(3)
        emit(OP.MONSTER_MOVE, m[1], m[2])
    end
    wait(3)
    emit(OP.START_SUBEVENT, 30, 0)
    wait(7)
    local more = {
        {49,60}, {38,49}, {27,38}, {16,27}, {5,16},
        {6,5}, {7,6}, {8,7}, {9,8},
    }
    for i, m in ipairs(more) do
        if i > 1 then
            wait(3)
        end
        emit(OP.MONSTER_MOVE, m[1], m[2])
    end
    wait(3)
    emit(OP.SET_TILE, 9, 0x85)
    tick(OP.SOUND, SND.GET, 0)
    wait(10)
end

local function ichicheck_floor24()
    timer2on()
    present("button_caption")          -- Button1.Caption := 'OK' (0x4704fb)
    if st(ST.ZENO_WEAK) == 1 then      -- 0x470505: Zeno's entry x 10
        set_state(ST.ZENO_WEAK, 0)
        set_zeno(monster_stat(0x0f, "hp") * 10, monster_stat(0x0f, "attack") * 10,
                 monster_stat(0x0f, "defense") * 10)
    end
    if st(ST.OPTION_A) ~= 0 and st(ST.OPTION_B) == 0 then
        set_zeno(1000, 0x271, 0x7d)      -- 0x47055d
        tick(OP.SUBDISPATCH, SUB.TAISEN2, 71)
        wait(3)
        emit(OP.MONSTER_MOVE, 60, 71)
        tick(OP.SOUND, SND.GET, 0)
        wait(3)
        emit(OP.MONSTER_MOVE, 49, 60)
        tick(OP.SOUND, SND.GET, 0)
    end
    wait(3)
    for i, sub in ipairs({57,56,55,54,53,52,51,50}) do
        emit(OP.START_SUBEVENT, sub, 0)
        if i < 8 then
            wait(5)
        end
    end
    if st(ST.BGM) ~= 0 then
        emit(OP.STOP_BGM, 0, 0)
    end
    emit(OP.SET_TILE_SILENT, 49, 91)
    wait(6)
    for v = 182, 166, -1 do
        emit(OP.NOP, 21, 91)
        emit(OP.MASKED_TILE, 49, v)
    end
    tick(OP.SOUND, SND.F3, 0)
    wait(7)
    emit(OP.START_SUBEVENT, 49, 0)
    wait(5)
    emit(OP.START_SUBEVENT, 48, 0)
    wait(11)
    for v = 50, 25, -1 do
        emit(OP.SUBDISPATCH, SUB.CHANGE_FLOOR, v)
        tick(OP.SOUND, SND.OPEN, 0)
        wait(1)
    end
    wait(1)
    if st(ST.BGM) ~= 0 then
        tick(OP.SOUND, SND.BGM_STOP, 0)
    end
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
    for _, t in ipairs(traps) do
        if get_tile(t[1]) == RAW.FLOOR then
            set_tile(t[1], t[2])
        end
    end
    tick(OP.SUBDISPATCH, SUB.MONCHECK, 0)
    tick(OP.SET_TILE, 16, 6)
    tick(OP.SOUND, SND.GET, 0)
    tick(OP.MONSTER_MOVE, 27, 16)
    wait(3)
    emit(OP.MONSTER_MOVE, 38, 27)
    wait(3)
    emit(OP.MONSTER_MOVE, 49, 38)
    wait(3)
    emit(OP.MONSTER_MOVE, 60, 49)
    wait(3)
    emit(OP.MONSTER_MOVE, 71, 60)
    wait(6)
    emit(OP.SET_TILE_SILENT, 71, 133)
    emit(OP.START_SUBEVENT, 39, 0)
    tick(OP.SET_TILE, 5, 11)
    tick(OP.SOUND, SND.GET, 0)
    wait(3)
    emit(OP.SPRITE, 4, 0)
    wait(1)
    doorstackin(82, 4)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(3)
    local function mv(a, b)
        tick(OP.MONSTER_MOVE, a, b)
    end
    local function sub(id)
        tick(OP.START_SUBEVENT, id, 0)
    end
    local function prefix(moves, leading)
        if leading ~= false then emit(OP.NOP, 0, 0) end
        emit(OP.SUBDISPATCH, SUB.TAISEN2, 71)
        mv(60, 71); mv(49, 60)
        for _, m in ipairs(moves) do mv(m[1], m[2]) end
    end
    prefix({{38,49},{27,38},{16,27}}, false)
    sub(38)
    prefix({{38,49},{39,38},{28,39},{29,28},{18,29},{19,18},{20,19}})
    prefix({{38,49},{39,38},{28,39},{29,28},{18,29},{19,18}})
    prefix({{38,49},{39,38},{28,39},{29,28},{18,29}})
    sub(37)
    prefix({{38,49},{37,38},{26,37},{25,26},{14,25},{13,14},{12,13}})
    prefix({{38,49},{37,38},{26,37},{25,26},{14,25},{13,14}})
    prefix({{38,49},{37,38},{26,37},{25,26},{14,25}})
    sub(36)
    prefix({{50,49},{39,50},{40,39},{41,40}})
    prefix({{50,49},{39,50},{40,39}})
    prefix({{50,49},{39,50}})
    sub(35)
    prefix({{48,49},{37,48},{36,37},{35,36}})
    prefix({{48,49},{37,48},{36,37}})
    prefix({{48,49},{37,48}})
    sub(34)
    doorstackin(82, 5)
    tick(OP.SOUND, SND.OPEN, 0)
    wait(2)
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
