-- npcs.lua: The characters: the thief (touzoku), the fairy (yousei), the old
-- men (roujin), the merchants (syounin) and the altar, with their Yes / No
-- answers (Button2Click / Button7Click).
-- Loaded by builders.lua (include); see it for the API.

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


builders.touzoku = function()
    itemdel()
    present("button_caption")
    local floor = st(ST.FLOOR)
    if floor == 0x1d then
        if hero_cell() == cell(5, 2) then
            tick(OP.START_SUBEVENT, 0x1b, 0)
            timer2on()
        elseif st(ST.THIEF_F29) == 0 then
            tick(OP.START_SUBEVENT, 0x1c, 0)
            timer2on()
        elseif st(ST.THIEF_F29) == 1 then
            tick(OP.START_SUBEVENT, 0x1d, 0)
            timer2on()
        end
    elseif floor == 2 then
        local x = st(ST.X)
        if x == 0 then
            timer2on()
            tick(OP.START_SUBEVENT, 5, 0)
        elseif x == 2 or x == 3 then
            aux.sprite = 1
            present("status_text", 0x89)
            timer2on()
            tick(OP.START_SUBEVENT, 4, 0)
            tick(OP.SPRITE, 1, 0)
            wait(1)
            doorstackin(0x43, 7)
            tick(OP.SOUND, SND.OPEN, 0)
            if st(ST.BGM) ~= 0 then
                soundcheck()
                soundplay()
            end
        elseif x == 9 or x == 10 then
            aux.work_558 = st(ST.THIEF_F2) == 1 and 7 or 6
            timer2on()
            tick(OP.START_SUBEVENT, aux.work_558, 0)
        end
    elseif floor == 0x0f then
        tick(OP.SET_TILE, 6, 6)
        tick(OP.SOUND, SND.GET, 0)
        tick(OP.MONSTER_MOVE, 7, 6)
        tick(OP.MONSTER_MOVE, 8, 7)
        tick(OP.START_SUBEVENT, 0x12, 0)
        wait(1)
        doorstackin(7, 7)
        tick(OP.SOUND, SND.OPEN, 0)
        wait(1)
        wait(1)
        timer2on()
    elseif floor == 0x23 then
        set_tile_at(2, 120, 6)         -- g_map[0x16e]
        aux.work_558 = st(ST.THIEF_F35) == 1 and 0x21 or 0x20
        timer2on()
        tick(OP.START_SUBEVENT, aux.work_558, 0)
    elseif floor == 0x32 then
        timer2on()
        tick(OP.START_SUBEVENT, 0x108, 0)
    end
end


---------------------------------------------------------------------------
-- yousei (0x478f0c): the fairy at the end of an area (floors 10, 20, 25,
-- 40, 49).  Computes the area score (aux.work_558, state[0x55]), the
-- rank against the area's ranking table (state[0x56], 0 = not ranked),
-- the area id (state[0x57]), counts the visit (state[0xe]), writes the
-- save0.dat checkpoint, shows the result text and queues sub-event
-- 0x10/0x16/0x18/0x28/0x2f (Button1Click 26.. -> the area flag).
--
-- The ranking table comes from optionN.dat; when that file is missing or
-- fails its checksums the original uses the built-in temporary ranking
-- below (after a ShowMessage).  The remake does not keep ranking files yet,
-- so it always uses the temporary ranking (and shows no message).

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
RANKING[5] = RANKING[3]                -- 0x479585: area 5 uses area 3's table
RANKING[5] = RANKING[3]

-- floor -> area, the cleared-gate index (op 0x06 tile 6), sub-event param
local FAIRY = {
    [10] = { 1, 0x1b, 0x10 }, [20] = { 2, 0x31, 0x16 },
    [25] = { 3, 0x47, 0x18 }, [40] = { 4, 0x3c, 0x28 },
    [49] = { 5, 0x10, 0x2f },
}

local function fairy_score(floor)
    local d = st(ST.OPTION_A) + 1
    local life, atk, def = st(ST.LIFE), st(ST.ATTACK), st(ST.DEFENSE)
    local keys = st(ST.KEY_YELLOW) + st(ST.KEY_BLUE) * 2 + st(ST.KEY_RED) * 3
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
    if st(ST.BGM) ~= 0 then
        set_state(ST.BGM, 0x0c)
        present("bgm", 0x0c)
    end
    set_state(ST.VISITS, st(ST.VISITS) + 1)
    local floor = st(ST.FLOOR)
    local score = fairy_score(floor)
    if score < 0 then score = 0 end
    aux.work_558 = score
    set_state(ST.SCORE, score)
    set_state(ST.RANK, 0)
    local f = FAIRY[floor]
    -- optionN.dat (0x4793ac): its rows when it reads back intact, else a
    -- ShowMessage and the temporary ranking.  The fairy only stands on the
    -- five area floors; elsewhere (no witness) area 1's table is used.
    local area = f and f[1] or 0
    local rows, status
    if area ~= 0 then rows, status = ranking_table(area, st(ST.SAVE_MAGIC)) end
    if not rows then
        if area ~= 0 then present("ranking_message", status) end
        rows = RANKING[area] or RANKING[1]
    end
    local life, ad = st(ST.LIFE), st(ST.ATTACK) + st(ST.DEFENSE)
    for r, row in ipairs(rows) do
        if score > row[1] or (score == row[1] and
            (life > row[2] or (life == row[2] and ad - row[2] > row[3]))) then
            set_state(ST.RANK, r)
            break
        end
    end
    if f then set_state(ST.AREA, f[1]) end
    present("checkpoint")              -- save0.dat: map + state
    aux.sprite = 1
    present("fairy_text", st(ST.RANK), score, st(ST.VISITS))
    if st(ST.BGM) ~= 0 then emit(OP.STOP_BGM, 0, 0) end
    local gate = f and f[2] or 0
    if f then
        aux.ui_selector = (aux.ui_selector & 0xffff0000) | f[3]
    end
    emit(OP.SET_TILE, gate, 6)
    tick(OP.SOUND, SND.GET, 0)
    wait(3)
    emit(OP.START_SUBEVENT, aux.ui_selector & 0xffff, 0)
    wait(2)
    timer2on()
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
    present("button_caption")          -- Button1.Caption := 'OK' (0x449716)
    present("message_panel_clear")
    itemdel()
    aux.subevent_id = 0                  -- g_subevent_id 0x48c5a4
    aux.work_564 = 0             -- g_battle_unwinnable 0x48c564
    local floor = st(ST.FLOOR)

    if floor == 2 then
        aux.message_selector = 0x19
        aux.event_selector = 0x42
        if st(ST.ROUJIN_F2) == 0 then
            -- one-shot +3% attack/defense (0x449805)
            set_state(ST.ROUJIN_F2, 1)
            set_state(ST.ATTACK, st(ST.ATTACK) + idiv(st(ST.ATTACK) * 3, 100))
            set_state(ST.DEFENSE, st(ST.DEFENSE) + idiv(st(ST.DEFENSE) * 3, 100))
            tick(OP.SPRITE, 0, 0)
            tick(OP.SOUND, SND.GET, 0)
            wait(1)
            wait(1)
            timer2on()
        end
    elseif floor == 0x10 then
        if st(ST.X) == 9 then
            aux.message_selector = 0x1b
            aux.event_selector = 0x47
        else
            aux.message_selector = 0x06
            aux.event_selector = 0x46
        end
    elseif floor == 0x17 then
        aux.work_564 = 1
        tick(OP.START_SUBEVENT, 0x3d, 0)
        wait(1)
        wait(1)
        timer2on()
    elseif ROUJIN[floor] then
        aux.message_selector = ROUJIN[floor][1]
        aux.event_selector = ROUJIN[floor][2]
    end

    if aux.work_564 == 1 then
        return
    end
    if aux.message_selector == 0x1b and st(ST.ROUJIN_SUB1B) == 0 then
        set_state(ST.SPECIAL0 + SLOT.MAGIC_ELIXIR, 1)
        set_state(ST.ROUJIN_SUB1B, 1)
        tick(OP.SHOW_ITEMS, 0, 0)
        tick(OP.SOUND, SND.GET, 0)
        wait(1)
        wait(1)
        timer2on()
    end
    local flag = aux.event_selector
    local seen = st(flag)
    if seen == 0 then
        if slot(SLOT.ORB_OF_WISDOM) == 0 then
            set_state(flag, 1)
        elseif slot(SLOT.ORB_OF_WISDOM) == 1 then
            set_state(flag, 2)
        end
        zyouout()
        if floor == 3 then
            set_state(ST.SPECIAL0 + SLOT.ORB_OF_HERO, 2)
            tick(OP.SHOW_ITEMS, 0, 0)
            tick(OP.SOUND, SND.GET, 0)
            tick(OP.SPRITE, 0x0b, 0)
            timer2on()
        end
    elseif seen == 1 or seen == 2 then
        aux.message_selector = 0x18
        zyouout()
    end
end

---------------------------------------------------------------------------
-- syounin (0x449014): the merchants (raw 0x13/0x14).  Floor switch sets the
-- offer: message_selector (g_dialog_selector), work_558 (offer text
-- ListBox2[0xf6 + n]), shop_flag / shop_talk_flag (state indices), price
-- and battle_turns (thank-you text + 1).  Branches that do not assign a
-- field keep its previous value, as in the original.
--   flag 0: RichEdit1 shows the offer, Button2 (Yes) and Button7 (No);
--           on floor 2 No is disabled (the rescued man's gift).
--   flag 1, talk flag 0: the thank-you text with Button1 (OK); the talk
--           flag becomes 1, or 2 with the Orb of Wisdom.
--   flag 1, talk flag 1..2: zyouout with selector 0x1d (floor 2) / 0x1c.
-- The answer runs Button2Click / Button7Click below.


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
    present("button_caption")          -- Button1.Caption := 'OK' (0x449031)
    itemdel()
    present("richedit_layout")         -- RichEdit1/Button bounds
    aux.choice_mode = 0
    aux.subevent_id = 0
    local floor = st(ST.FLOOR)
    local t = SHOP[floor]
    if floor == 0x0c then t = SHOP_F12[st(ST.X)] end
    if t then
        if t[1] then aux.message_selector = t[1] end
        aux.work_558 = t[2]
        aux.shop_flag = t[3]
        if t[4] then aux.shop_talk_flag = t[4] end
        aux.shop_price = t[5]
        if t[6] then aux.battle_turns = t[6] end
    end
    aux.work_560 = 0
    local bought = st(aux.shop_flag)
    if bought == 0 then
        if aux.work_558 < 0x0c then
            present("richedit_text", aux.work_558 + 0xf6)
        end
        present("richedit_show", 1)
        present("choice_show", floor == 2 and 0 or 1)
    elseif bought == 1 then
        local talk = st(aux.shop_talk_flag)
        if talk == 0 then
            present("richedit_text", aux.battle_turns - 1)
            if slot(SLOT.ORB_OF_WISDOM) == 0 then
                set_state(aux.shop_talk_flag, 1)
            elseif slot(SLOT.ORB_OF_WISDOM) == 1 then
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
-- is the altar menu (Button39..42Click below).
local function shop_done(text)
    present("richedit_text", text)
    present("richedit_show", 1)
    present("button1_show")
end

local function shop_paid()
    shop_done(0x103)
    emit(OP.SPRITE, 0, 0); emit(OP.SHOW_ITEMS, 0, 0)
    tick(OP.SOUND, SND.GET, 0)
    timer2on()
end

local SHOP_REWARD = {
    [0x2a] = function() set_state(ST.GOLD, st(ST.GOLD) + 1000) end,
    [0x2b] = function() set_state(ST.KEY_BLUE, st(ST.KEY_BLUE) + 1) end,
    [0x2c] = function() set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) + 5) end,
    [0x2d] = function() set_state(ST.KEY_RED, st(ST.KEY_RED) + 1) end,
    [0x2e] = function() set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) + 1) end,
    [0x2f] = function() set_state(ST.KEY_BLUE, st(ST.KEY_BLUE) + 1) end,
    [0x30] = function()
        set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) - 1)
        set_state(ST.GOLD, st(ST.GOLD) + 100)
    end,
    [0x31] = function()
        set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) + 4)
        set_state(ST.KEY_BLUE, st(ST.KEY_BLUE) + 1)
    end,
    [0x32] = function() set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) + 3) end,
    [0x33] = function() set_state(ST.KEY_BLUE, st(ST.KEY_BLUE) + 3) end,
    [0x34] = function() set_state(ST.LIFE, st(ST.LIFE) + 2000) end,
    [0x35] = function() set_state(ST.SPECIAL0 + SLOT.SUPER_MATTOCK, 1) end,
}

builders.button2click = function()
    present("choice_hide")             -- Button2/Button7 hidden, No enabled
    present("richedit_text", -1)
    local mode = aux.choice_mode & 0xffff
    if mode == 0 then present("richedit_show", 0) end
    present("disp")
    if mode == 0 then
        if st(ST.FLOOR) == 0x1c then   -- the key buyer
            aux.subevent_id = 0
            if st(ST.KEY_YELLOW) == 0 then
                return shop_done(0x106)
            end
            set_state(ST.KEY_YELLOW, st(ST.KEY_YELLOW) - 1)
            set_state(ST.GOLD, st(ST.GOLD) + 100)
            return shop_paid()
        end
        if st(ST.GOLD) < aux.shop_price then
            shop_done(0x102)
            aux.subevent_id = 0
            return
        end
        set_state(ST.GOLD, st(ST.GOLD) - aux.shop_price)
        aux.subevent_id = 0
        local f = aux.shop_flag
        if (f >= 0x2a and f <= 0x2d) or f == 0x2f or (f >= 0x31 and f <= 0x35) then
            set_state(f, 1)
        end
        if SHOP_REWARD[f] then SHOP_REWARD[f]() end
        return shop_paid()
    elseif mode == 1 then
        -- the altar's menu: RichEdit1 = ListBox2[0xcc]; Button39..41 show
        -- the life/attack/defense offers, Button42 cancels (and has focus)
        local d = st(ST.OPTION_A) + 1
        local area = idiv(st(ST.FLOOR) + 10, 10)
        present("altar_menu", (st(ST.ALTAR) + 1) * 100 * d, area * 2 * d,
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
    local n = st(ST.ALTAR) + 1
    aux.shop_price = 10 * (n * n - n + 2)
    present("altar_offer", aux.shop_price)
    aux.subevent_id = 2
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
    if st(ST.GOLD) < aux.shop_price then
        present("richedit_text", 0xd3)
        aux.subevent_id = 0
        present("richedit_show", 1)
        present("button1_show")
        return
    end
    aux.sprite = 1
    local d = st(ST.OPTION_A) + 1
    local area = idiv(st(ST.FLOOR) + 10, 10)
    local gain
    if kind == 1 then gain = (st(ST.ALTAR) + 1) * 100 * d
    elseif kind == 2 then gain = area * 2 * d
    else gain = area * 4 * d end
    present("status_concat", 0xb4 + kind, gain, 0xa6)
    set_state(ST.ALTAR, st(ST.ALTAR) + 1)
    set_state(ST.GOLD, st(ST.GOLD) - aux.shop_price)
    if kind == 1 then
        set_state(ST.LIFE, st(ST.LIFE) + st(ST.ALTAR) * 100 * d)
    elseif kind == 2 then
        set_state(ST.ATTACK, st(ST.ATTACK) + gain)
    else
        set_state(ST.DEFENSE, st(ST.DEFENSE) + gain)
    end
    timer2on()
    tick(OP.SPRITE, 1, 0)
    tick(OP.SOUND, SND.GET, 0)
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
