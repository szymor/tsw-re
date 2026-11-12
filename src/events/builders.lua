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
--   aux.<name>                  auxiliary globals (see event_builders.h)
--   call(name)                  run another builder
--   present(kind, ...)          presentation side effect; the engine acts on
--                               timer2on, timer2(0|1), zyouout, dialog_hide,
--                               transitional(id)
--   get_count() / set_count(n)  command count (0x48c5ac); set_count shrinks
--   fail(msg)                   abort: path not ported
--   idiv(a, b)                  C integer division (truncates toward zero)

local ST_LIFE, ST_ATTACK, ST_DEFENSE = 0, 1, 2
local ST_FLOOR, ST_FLOOR_LIMIT, ST_X, ST_Y = 4, 5, 6, 7
local ST_SPECIAL0 = 17                 -- special-item slot 0
local SLOT_ORB_OF_HERO, SLOT_ORB_OF_WISDOM, SLOT_MAGIC_ELIXIR = 0, 1, 4
local ST_SHIELD_SACRED = 41            -- Sacred Shield picked (0x4b872c)
local ST_SOUND = 0x5a                  -- WAV/sound state (flag_5a)
local ST_ROUJIN_F2 = 0x62              -- floor-2 old man one-shot (0x4b8810)
local ST_ROUJIN_SUB1B = 0x6d           -- floor-16 x=9 old man gift (0x4b883c)
local ST_STAIRS_ADJ = 0xa6             -- set by mmes next to stairs (meta_a6)

local RAW_FLOOR, RAW_ZENO_TRIGGER = 0x06, 0x07
local RAW_MAGIC_SERGEANT = 0x5d

local function st(i) return get_state(i) end
local function slot(s) return get_state(ST_SPECIAL0 + s) end

-- Presentation helpers with no state effect in the original:
--   itemdel (0x450758) hides the item panel, timer2on (0x44bc0c) starts the
--   timeline timer, zyouout (0x449e18) opens the dialogue window for the
--   current message selector, disp (0x44cb34) redraws the status panel.
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
    present("player_step", target)
    set_state(ST_X, target % 11)
    set_state(ST_Y, target // 11)
    present("player_draw")              -- yusyahyouji1
    aux.event_param = 0                 -- DAT_0048c560
    aux.sprite = 0
    local x, y = st(ST_X), st(ST_Y)
    local p = y * 11 + x
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
        emit(0x00, 6, 0x5b)
        emit(0x09, actor, frame)
    end
    emit(0x00, 0, 0)
    emit(0x01, 7, 0)
end

---------------------------------------------------------------------------
-- mevent (0x46397c): map events reached from handan raw 0x07.  Floor
-- switch 0x4639b6 / 0x4639e8.  Only the floor-3 case (0x463a2c, the opening
-- Zeno trigger) is ported.
--
-- Floor 3: the player steps onto the trigger tile (idou), the tile is
-- cleared 0x07 -> 0x06 (the one-shot), sub-event 10 (selector 0x14) is
-- queued with four waits, then Zeno appears at actor index 0x46.

builders.mevent = function()
    itemdel()
    local floor = st(ST_FLOOR)
    if floor ~= 3 then
        fail(string.format("mevent floor %d", floor))
    end
    call("idou")
    timer2on()
    set_tile(st(ST_Y) * 11 + st(ST_X), RAW_FLOOR)
    present("message", 0xda)
    emit(0x0a, 10, 0)
    emit(0x00, 0, 0)
    emit(0x00, 0, 0)
    emit(0x00, 0, 0)
    emit(0x00, 0, 0)
    aux.event_sprite = 0x46
    call("zenolivework")
    if st(ST_SOUND) ~= 0 then
        emit(0x00, 0, 0)
        emit(0x01, 6, 0)
    end
end

---------------------------------------------------------------------------
-- handan (0x444490): tile-action dispatcher on the raw tile at
-- aux.target_index (switch 0x4444e3).  Ported cases: 0x06 floor (walk),
-- 0x07 map event and 0x19/0x1a old man (roujin).  Everything else is still
-- handled by the legacy C path.

builders.handan = function()
    local raw = get_tile(aux.target_index)
    if raw == RAW_FLOOR then
        return call("idou")
    elseif raw == RAW_ZENO_TRIGGER then
        return call("mevent")
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
            emit(0x00, 0, 0)
            emit(0x02, 0, 0)
            emit(0x00, 0, 0)
            emit(0x01, 2, 0)
            emit(0x00, 0, 0)
            emit(0x00, 0, 0)
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
        emit(0x00, 0, 0)
        emit(0x0a, 0x3d, 0)
        emit(0x00, 0, 0)
        emit(0x00, 0, 0)
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
        emit(0x00, 0, 0)
        emit(0x08, 0, 0)
        emit(0x00, 0, 0)
        emit(0x01, 2, 0)
        emit(0x00, 0, 0)
        emit(0x00, 0, 0)
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
            emit(0x00, 0, 0)
            emit(0x08, 0, 0)
            emit(0x00, 0, 0)
            emit(0x01, 2, 0)
            emit(0x00, 0, 0)
            emit(0x02, 0x0b, 0)
            timer2on()
        end
    elseif seen == 1 or seen == 2 then
        aux.message_selector = 0x18
        zyouout()
    end
end

---------------------------------------------------------------------------
-- Button1Click (0x44da10): the player confirms the zyouout window.
-- aux.roujin_aux is g_subevent_id (0x48c5a4).  Every confirmed sub-event
-- 10 < id < 0x44 is recorded as state[0x50 + id] = 1 (0x44da2a): that is
-- how the floor-10 area-1 flag 0x6a is set (sub-event 26, the fairy).
-- Dispatch: table 0x44da4f (id < 0x1b), table 0x44dad3 (0x1b..0x32), then
-- compares.  Presentation (Button1/RichEdit1 visibility, itemlive, disp,
-- mhyouji) goes through present(); Timer2 is real engine state.
-- Case 20 calls openning2, which is not ported yet: it hands over to the
-- transitional "subevent:20" coroutine.

local ST_DIFFICULTY = 159              -- option A (0x4b8904)
local ST_BATTLE_RENDER = 0x0c          -- set by openning31, cleared by 13

local function hide_button() present("button_hide") end
local function hide_dialog() present("dialog_hide") end
local function timer2(on) present("timer2", on and 1 or 0) end

local function confirm_resume()        -- case 0x17 (0x44e107)
    hide_button(); hide_dialog()
    timer2(true)
end

local function confirm_0x1a()          -- case 0x1a
    hide_button(); hide_dialog()
    aux.sprite = 0
    if st(ST_DIFFICULTY) == 0 then
        fail("button1click 0x1a: FUN_004237dc(DAT_0048a764) not ported")
    end
    timer2(true)
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
        present("mhyouji")
        hide_dialog()
        itemlive()
    end,
    [0x0e] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        emit(0x00, 0, 0); emit(0x0c, 0x42, 0x4d)
        emit(0x00, 0, 0); emit(0x0c, 0x43, 0x42)
        emit(0x00, 0, 0); emit(0x0c, 0x44, 0x43)
    end,
    [0x0f] = function()
        hide_button(); hide_dialog()
        timer2(false); timer2on()
        emit(0x00, 0, 0); emit(0x06, 99, 6)
        emit(0x00, 0, 0); emit(0x01, 2, 0)
        emit(0x00, 0, 0); emit(0x0c, 0x58, 99)
        emit(0x00, 0, 0); emit(0x0c, 0x4d, 0x58)
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
        present("transitional", 0x14)
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

