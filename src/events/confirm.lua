-- confirm.lua: Button1Click: confirming the dialogue window, by sub-event.
-- Loaded by builders.lua (include); see it for the API.

-- Button1Click (0x44da10): the player confirms the zyouout window.
-- aux.subevent_id is g_subevent_id (0x48c5a4).  Every confirmed sub-event
-- 10 < id < 0x44 is recorded as state[0x50 + id] = 1 (0x44da2a): that is
-- how the floor-10 area-1 flag 0x6a is set (sub-event 26, the fairy).
-- Dispatch: table 0x44da4f (id < 0x1b), table 0x44dad3 (0x1b..0x32), then
-- compares.  Presentation (Button1/RichEdit1 visibility, itemlive, disp,
-- mhyouji) goes through present(); Timer2 is real engine state.
-- Case 20 calls openning2 (the capture cutscene, below).


-- mhyouji (0x442c38): map redraw; it also clears state[0xa6] (0x442c45).
function mhyouji()
    set_state(ST.STAIRS_ADJ, 0)
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
-- and Button1Click itself does not restart Timer2; the form's OnClose
-- (form2onclose 0x43a504) does, so the timeline resumes when the ranking
-- window closes (event_engine_close_ranking).  Otherwise Timer2 resumes.
local function confirm_0x1a()
    hide_button(); hide_dialog()
    aux.sprite = 0
    if st(ST.OPTION_A) == 0 then
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

local CONFIRM_CASE = {   -- Button1Click: g_subevent_id -> its case
    [0] = function()
        if slot(SLOT.ORB_OF_HERO) == 2 then
            set_state(ST.SPECIAL0 + SLOT.ORB_OF_HERO, 1)
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
        set_state(ST.BATTLE_RENDER, 0)
        set_count(0)
        mhyouji()
        hide_dialog()
        itemlive()
    end,
    [0x0e] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        play(function(t)               -- the thief walks 68 -> 67 -> 66 -> 77
            t:tick(OP.MONSTER_MOVE, 68, 67)
            t:tick(OP.MONSTER_MOVE, 67, 66)
            t:tick(OP.MONSTER_MOVE, 66, 77)
        end)
    end,
    [0x0f] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        play(function(t)               -- 77 -> 88 -> 99, then gone
            t:tick(OP.MONSTER_MOVE, 77, 88)
            t:tick(OP.MONSTER_MOVE, 88, 99)
            tile_sound(t, 99, 6)
        end)
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
    local id = aux.subevent_id
    if id > 10 and id < 0x44 then
        set_state(ST.SUBEVENT_DONE0 + id, 1)
    end
    if id > 0x1a then
        confirm_high(id)
    elseif CONFIRM_CASE[id] then
        CONFIRM_CASE[id]()
    end
    -- common tail (switchD_0044da48_caseD_2)
    hide_button()
    if id < 2 then hide_dialog() end
    present("disp")
end
