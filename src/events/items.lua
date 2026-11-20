-- items.lua: Item use: the special items' handlers (img4work .. img17work) and
-- their answers.
-- Loaded by builders.lua (include); see it for the API.

-- Item use.  formkeydown (0x460bd8) with the item overlay open runs the
-- handler of the selected slot on Enter (img6work slot 0 .. img17work slot
-- 13; slots 3, 10 and 14 are passive and have none).  The handlers of the
-- usable items only open the Yes/No panel (form+0x2cc/0x2d0) and store the
-- confirm case in 0x48c574 (aux.confirm_case); Yes (Button38Click 0x450ba0)
-- acts on that case, No (Button37Click 0x450b78) closes the panel.

-- mmmes (0x451c20): classify the tile at aux.event_selector for the item
-- effects into aux.sprite: 1 wall (raw 8), 3 lava (0x0d), 2 monster (with
-- aux.current_monster), 0 otherwise.
local function mmmes()
    local raw = get_tile(aux.event_selector)
    if raw < 0x5d then
        if raw == 8 then aux.sprite = 1; return end
        if raw == 0x0d then aux.sprite = 3; return end
        if raw < 0x3d then aux.sprite = 0; return end
    elseif not ((raw >= 0x5d and raw <= 0x60) or (raw >= 0x85 and raw <= 0x90)
                or (raw >= 0x93 and raw <= 0x9e)) then
        aux.sprite = 0
        return
    end
    aux.sprite = 2
    if raw - 0x3d < 0x24 then
        aux.current_monster = (raw - 0x3d) >> 1
    elseif raw >= 0x85 and raw < 0x9f then
        aux.current_monster = ((raw - 0x85) >> 1) + 0x14
    end
end

-- Button38Click visits the four neighbours down, right, left, up.  For
-- each one it sets aux.event_selector, then (on the board only) the target
-- index and runs mmmes; the up neighbour's target index is set even off the
-- board.  fn(dir) runs for a neighbour on the board after mmmes, with dir
-- 0 down, 1 right, 2 left, 3 up.  `each` runs before every neighbour.
local function scan_neighbours(fn, each)
    local x, y = st(ST.X), st(ST.Y)
    local p = cell(x, y)
    local steps = {
        { 11, y < 10 }, { 1, x < 10 }, { -1, x >= 1 and x <= 10 },
        { -11, y >= 1 and y <= 10 },
    }
    for dir = 0, 3 do
        if each then each(p) end
        local s = steps[dir + 1]
        aux.event_selector = p + s[1]
        if dir == 3 then aux.target_index = aux.event_selector end
        if s[2] then
            aux.target_index = aux.event_selector
            mmmes()
            fn(dir)
        end
    end
end

-- img8work .. img17work: slot held -> itemdel, confirm case, panel shown.
local ITEM_CONFIRM = {  -- handler -> slot, confirm case
    img8work = {4, 5}, img9work = {5, 6}, img10work = {6, 7},
    img11work = {7, 8}, img12work = {8, 9}, img13work = {9, 10},
    img15work = {11, 12}, img16work = {12, 13}, img17work = {13, 14},
}
for name, c in pairs(ITEM_CONFIRM) do
    builders[name] = function()
        local held = slot(c[1])
        -- The Warp Staff counts charges (jl: >= 1); the others test == 1.
        if (c[1] == 7 and held >= 1) or held == 1 then
            itemdel()
            aux.confirm_case = c[2]
            present("item_confirm", c[2])
        end
    end
end

-- Button37Click: No.
builders.button37click = function()
    present("item_confirm_hide")
    itemlive()
end

-- obudameiji (0x44ff0c): the Orb of the Hero's damage forecast for
-- aux.current_monster.  The original forecasts with the plain attack; the
-- remake doubles it as the battle does (the Cross, the Dragon Slayer:
-- doubled_attack), so the Orb agrees with the fight.
local function obudameiji()
    local index = aux.current_monster
    local d = st(ST.OPTION_A) + 1
    local attack = st(ST.ATTACK) * (doubled_attack(index) and 2 or 1)
    aux.work_564 = 0
    if monster_stat(index, "defense") * d - attack < 0 then
        local hp = monster_stat(index, "hp") * d
        local turns = 0
        repeat                          -- damecount
            hp = monster_stat(index, "defense") * d + (hp - attack)
            turns = turns + 1
        until hp <= 0
        aux.battle_turns = turns
        local atk = monster_stat(index, "attack") * d
        local damage = 0
        if st(ST.DEFENSE) - atk < 0 then
            damage = (atk - st(ST.DEFENSE)) * (turns - 1)
        end
        return damage
    end
    aux.work_564 = 1
    return nil
end

-- img6work (0x480f60): the Orb of the Hero (value 1).  Timer1 stops, the
-- window lists up to seven distinct monsters of the floor in map order
-- (yousya 0x44f26c), each with its stats and obudameiji's forecast.
builders.img6work = function()
    if slot(SLOT.ORB_OF_HERO) ~= 1 then return end
    present("timer1", 0)
    itemdel()
    for i = 1, 7 do aux.work_564 = i end
    local shown = {}
    local rows = {}
    for i = 0, 120 do
        local raw = get_tile(i)
        local index
        if raw - 0x3d >= 0 and raw - 0x3d < 0x23 then
            index = (raw - 0x3d) >> 1
        elseif raw == 0x68 then
            index = 0x12
        elseif raw == 0x7a then
            index = 0x13
        elseif raw >= 0x85 and raw < 0x9e then
            index = ((raw - 0x85) >> 1) + 0x14
        end
        if index then                   -- yousya
            aux.event_selector = index
            aux.current_monster = index
            aux.work_560 = 0
            if #rows < 7 then
                if shown[index] then
                    aux.work_560 = 1
                else
                    shown[index] = true
                    local damage = obudameiji()
                    rows[#rows + 1] = index
                    present("hero_row", index, damage or -1)
                end
            end
        end
    end
    present("hero_orb", #rows)
end

-- img5work (0x48198c): the Orb of Wisdom's window (the old men's and the
-- merchants' words; each button's hover hint names the speaker and floor).
-- The window is Button11..34 (the numbered buttons over the left panel's
-- items) and Button35 (OK).  In the original, button k is enabled when the
-- speaker's talked-to flag WISDOM_FLAGS[k] is 2, i.e. heard while holding
-- the Orb; words heard before it (flag 1) were lost for good.  The remake
-- enables every entry heard (flag 1 or 2); the flags are unchanged.
local WISDOM_FLAGS = {
    0x44, 0x45, 0x37, 0x38, 0x39, 0x3b, 0x46, 0x48, 0x49, 0x4a, 0x4b, 0x3d,
    0x4c, 0x4d, 0x4e, 0x3e, 0x4f, 0x3f, 0x50, 0x51, 0x40, 0x52, 0x41, 0x53,
}
builders.img5work = function()
    if slot(SLOT.ORB_OF_WISDOM) ~= 1 then return end
    aux.subevent_id = 1                  -- g_subevent_id
    itemdel()
    for k, flag in ipairs(WISDOM_FLAGS) do
        local heard = st(flag) == 1 or st(flag) == 2
        present("wisdom_button", k - 1, heard and 1 or 0)
    end
    present("wisdom_orb")
end

-- Button11Click .. Button34Click (0x44ee4c ..): the old man's words in
-- RichEdit1 without Button1 (state[0xa7] = 1, zyouout).
for k = 0, 23 do
    builders["button" .. (11 + k) .. "click"] = function()
        aux.message_selector = k
        set_state(ST.WORDS_ONLY, 1)
        zyouout()
    end
end

-- Button35Click (0x44f0e8): the window's OK.  RichEdit1 hidden and
-- cleared, g_subevent_id 0, state[0xa7] 0, the buttons hidden, itemlive.
builders.button35click = function()
    present("richedit_show", 0)
    present("richedit_text", -1)
    aux.subevent_id = 0
    set_state(ST.WORDS_ONLY, 0)
    present("wisdom_close")
    itemlive()
end

-- img4work (0x481e80): the Orb of Flying.  Next to stairs it opens the
-- flight panel (Button8 up, Button9 down, Button10 land); otherwise the
-- status sprite 0x14.
builders.img4work = function()
    set_state(ST.LAST_VERTICAL, 0)
    set_state(ST.STAIRS_ADJ, 0)
    aux.work_564 = 2
    aux.battle_turns = -1
    aux.current_monster = -1
    present("monshyouji", aux.target_index, -1)
    if slot(2) ~= 1 then return end
    itemdel()
    aux.sprite = 0
    local x, y = st(ST.X), st(ST.Y)
    local p = cell(x, y)
    aux.work_558 = p
    if y >= 1 and y <= 10 then aux.event_selector = p - 11; mmes() end
    aux.event_selector = p - 1
    if x >= 1 and x <= 10 then mmes() end
    aux.event_selector = p + 1
    if x < 10 then mmes() end
    aux.event_selector = p + 11
    if y < 10 then mmes() end
    if aux.sprite == 0x13 then
        if st(ST.BGM) ~= 0 then present("media_stop") end
        mhyouji()
        present("disp")
        set_state(ST.BATTLE_RENDER, 1)
        local floor = st(ST.FLOOR)
        present("fly_panel", floor ~= st(ST.FLOOR_LIMIT) and 1 or 0,
                floor >= 2 and 1 or 0)
    else
        aux.sprite = 0x14
        itemlive()
        present("disp")
    end
end

-- Button8Click / Button9Click: the flight panel's up / down.
builders.button8click = function()
    local floor = st(ST.FLOOR)
    if floor >= st(ST.FLOOR_LIMIT) then return end
    set_state(ST.LAST_VERTICAL, 1)
    floor = floor == 0x2b and 0x2d or floor + 1
    set_state(ST.FLOOR, floor)
    mhyouji()
    present("disp")
    if floor == 0x23 and get_tile_at(35, 91) + get_tile_at(2, 120) == 0x1b then
        set_tile_at(2, 120, 6)
    end
    present("fly_panel", floor ~= st(ST.FLOOR_LIMIT) and 1 or 0,
            floor >= 2 and 1 or 0)
end

builders.button9click = function()
    local floor = st(ST.FLOOR)
    if floor <= 1 then return end
    set_state(ST.LAST_VERTICAL, 2)
    floor = floor == 0x2d and 0x2b or floor - 1
    set_state(ST.FLOOR, floor)
    mhyouji()
    present("disp")
    present("fly_panel", floor ~= st(ST.FLOOR_LIMIT) and 1 or 0,
            floor >= 2 and 1 or 0)
end

-- Button10Click (0x44eaf4): land.  The player goes to the floor's arrival
-- cell for the last vertical direction (stair-pair header byte d - 1); on
-- the highest floor the direction follows the stairs next to him (state
-- 0xa6).  Fairy and floor-29 cleanups as on the stairs.
builders.button10click = function()
    present("fly_panel_hide")
    local d = st(ST.LAST_VERTICAL)
    local floor = st(ST.FLOOR)
    if d ~= 0 and floor ~= 25 and get_tile_at(25, 71) == 0x17 then
        set_tile_at(25, 71, 6)
    end
    if d ~= 0 and floor ~= 49 and get_tile_at(49, 16) == 0x17 then
        set_tile_at(49, 16, 6)
    end
    set_state(ST.BATTLE_RENDER, 0)
    if d == 1 then
        if floor == st(ST.FLOOR_LIMIT) and st(ST.STAIRS_ADJ) == 1 then
            d = 2; set_state(ST.LAST_VERTICAL, 2)
        end
    elseif d == 2 and floor == st(ST.FLOOR_LIMIT) and st(ST.STAIRS_ADJ) ~= 1 then
        d = 1; set_state(ST.LAST_VERTICAL, 1)
    end
    if d == 1 or d == 2 then
        local b = get_map_byte(floor * 123 + d - 1)
        set_state(ST.X, b % MAP_COLS)
        set_state(ST.Y, b // MAP_COLS)
    end
    if d ~= 0 and st(ST.FLOOR_LIMIT) == 0x1d and floor < 0x1d then
        set_tile_at(29, 38, 6)
        set_tile_at(29, 27, 6)
        set_tile_at(29, 17, 0x15)
    end
    if d == 1 then stairs_face(0x0c) elseif d == 2 then stairs_face(0x0b) end
    timer2on()
    tick(OP.SOUND, SND.GET, 0)
    present("player_draw")
    aux.sprite = 0x17
    present("disp")
    if st(ST.BGM) ~= 0 then soundcheck(); soundplay() end
    itemlive()
end

-- Button38Click (0x450ba0): Yes on the item panel.
local function confirm_elixir()
    aux.sprite = 1
    timer2on()
    play(function(t)
        t:tick(OP.SOUND, SND.GET, 0)
        t:tick(OP.SHOW_ITEMS, 0, 0)
        t:tick(OP.SPRITE, aux.sprite, 0)
    end)
    local gain = st(ST.ATTACK) * 10 + st(ST.DEFENSE) * 5
    aux.work_558 = gain
    status_cat(0, 0xba, 2, 0, 1, gain, 0, 0xa6)
    set_state(ST.LIFE, st(ST.LIFE) + gain)
    set_state(ST.SPECIAL0 + 4, 0)
end

-- matokwork (0x451b98): the wall at aux.event_selector crumbles (door kind 7).
-- The scans over the hero's neighbours (mattock, bomb, snow crystal) stay
-- in append order: each neighbour sets aux between the door frames.
local function matokwork()
    wait(1)
    doorstackin(aux.event_selector, 7)
    tick(OP.SOUND, SND.OPEN, 0)
end

local function confirm_mattock()
    timer2on()
    emit(OP.SPRITE, 0x3c, 0)
    emit(OP.SHOW_ITEMS, 0, 0)
    aux.sprite = 0
    scan_neighbours(function()
        if aux.sprite == 1 then matokwork() end
    end, function(p) aux.work_558 = p end)
    set_state(ST.SPECIAL0 + 5, 0)
end

-- toroad2 (0x44bc34): a monster at aux.target_index dissolves (masks
-- 0xb6 .. 0xa6 over its own sprite) and the cell becomes floor.
local function toroad2()
    timer2on()
    local t = aux.target_index
    local raw = get_tile(t)
    emit(OP.SET_TILE_SILENT, t, 6)
    for mask = 0xb6, 0xa6, -4 do
        emit(OP.NOP, raw, 6)
        emit(OP.MASKED_TILE, t, mask)
        tick(OP.SOUND, SND.OPEN, 0)
    end
end

local function confirm_bomb()
    timer2on()
    tick(OP.SUBDISPATCH, SUB.MONCHECK, 0)
    emit(OP.SPRITE, 0, 0); emit(OP.SHOW_ITEMS, 0, 0)
    aux.shop_price = 0
    aux.sprite = 0
    aux.work_558 = hero_cell()
    scan_neighbours(function()
        if aux.sprite == 2 then
            aux.shop_price = aux.shop_price +
                monster_stat(aux.current_monster, "gold")
            toroad2()
        end
    end)
    set_state(ST.GOLD, st(ST.GOLD) + aux.shop_price)
    set_state(ST.SPECIAL0 + 6, 0)
    aux.sprite = 1
    status_cat(0, 0xbc, 0, 0xc8, 2, 0, 1, aux.shop_price, 0, 0xc9)
end

local function confirm_warp()
    aux.sprite = 0x3e
    local p = hero_cell()
    local t = 0x78 - p
    aux.target_index = t
    if get_tile(t) == RAW.FLOOR then
        -- The original spends a charge first and only then tests the
        -- target; the remake charges only a warp that happens
        -- (08-remake.md, decisions).
        set_state(ST.SPECIAL0 + 7, st(ST.SPECIAL0 + 7) - 1)
        set_state(ST.X, t % MAP_COLS)
        set_state(ST.Y, t // MAP_COLS)
        present("player_draw")
        timer2on()
        emit(OP.SOUND, SND.GET, 0)
    else
        aux.sprite = 0x44
    end
    itemlive()
    present("disp")
end

local function confirm_wings(up)
    local floor = st(ST.FLOOR)
    local p = hero_cell()
    local ok
    if up then
        ok = floor ~= 0x32 and get_tile_at(floor + 1, p) == RAW.FLOOR
    else
        ok = floor ~= 0 and get_tile_at(floor - 1, p) == RAW.FLOOR
    end
    if not ok then
        aux.sprite = up and 0x45 or 0x46
        present("disp")
        itemlive()
        return
    end
    set_state(ST.SPECIAL0 + (up and 8 or 9), 0)
    aux.sprite = up and 0x3f or 0x40
    set_state(ST.FLOOR, floor + (up and 1 or -1))
    if st(ST.BGM) ~= 0 then soundcheck(); soundplay() end
    mhyouji()
    timer2on()
    play(function(t)
        t:tick(OP.SOUND, SND.GET, 0)
        t:cmd(OP.SHOW_ITEMS, 0, 0)
        t:cmd(OP.SPRITE, aux.sprite, 0)
    end)
    itemlive()
    present("disp")
end

-- yukiwork (0x451d3c): the lava at aux.event_selector cools (door kind
-- aux.work_564: 9..12 by direction) and becomes floor.
local function yukiwork()
    tick(OP.SET_TILE_SILENT, aux.event_selector, 6)
    wait(1)
    doorstackin(aux.event_selector, aux.work_564)
    tick(OP.SOUND, SND.OPEN, 0)
end

-- The Snow Crystal is not used up; it cools the adjacent lava only.
local function confirm_snow()
    timer2on()
    emit(OP.SHOW_ITEMS, 0, 0)
    aux.work_560 = 0
    local kind = { [0] = 0x0b, [1] = 0x0a, [2] = 0x09, [3] = 0x0c }
    scan_neighbours(function(dir)
        if aux.sprite == 3 then
            aux.work_560 = 1
            aux.work_564 = kind[dir]
            yukiwork()
        end
    end, function(p) aux.work_558 = p end)
    if aux.work_560 == 1 then
        aux.sprite = 1
        status_cat(0, 0xc0, 0, 0xc6)
    else
        aux.sprite = 0x41
    end
    emit(OP.SPRITE, aux.sprite, 0)
end

local function confirm_key()
    set_state(ST.SPECIAL0 + 12, 0)
    aux.sprite = 0x42
    timer2on()
    play(function(t)                   -- every yellow door, from cell 0 up
        for i = 0, 0x78 do
            if get_tile(i) == 1 then door_sound(t, i, 1) end
        end
        t:cmd(OP.SHOW_ITEMS, 0, 0)
        t:cmd(OP.SPRITE, aux.sprite, 0)
    end)
end

local function confirm_super_mattock()
    set_state(ST.SPECIAL0 + 13, 0)
    aux.sprite = 0x43
    timer2on()
    play(function(t)                   -- every wall, column by column
        for col = 0, 10 do
            for row = 0, 10 do
                local i = cell(col, row)
                if get_tile(i) == 8 then tile_sound(t, i, 6) end
            end
        end
        t:cmd(OP.SHOW_ITEMS, 0, 0)
        t:cmd(OP.SPRITE, aux.sprite, 0)
    end)
end

builders.button38click = function()
    present("item_confirm_hide")
    local c = aux.confirm_case
    if c == 5 then confirm_elixir()
    elseif c == 6 then confirm_mattock()
    elseif c == 7 then confirm_bomb()
    elseif c == 8 then confirm_warp()
    elseif c == 9 then confirm_wings(true)
    elseif c == 10 then confirm_wings(false)
    elseif c == 12 then confirm_snow()
    elseif c == 13 then confirm_key()
    elseif c == 14 then confirm_super_mattock()
    end
end
