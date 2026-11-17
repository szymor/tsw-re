-- cutscenes.lua: Cutscenes: Zeno's appearance (zenolivework), the opening after
-- his speech (openning2, openning31, madoushi2) and the ending's last
-- picture (append).
-- Loaded by builders.lua (include); see it for the API.

-- The timelines here are written in play order (play(); common.lua).
--
-- zenolivework (0x461a0c): Zeno's appearance.  Sound 7, then 17 masked
-- frames 0xa6 -> 0xb6 of Zeno (0x5b) over floor (6) at the actor index in
-- aux.work_558.  Also reached as timeline sub-event 0x0b/2.

builders.zenolivework = function()
    present("zeno_setup")
    timer2on()
    local actor = aux.work_558 & 0xffff
    play(function(t)
        t:tick(OP.SOUND, SND.F3, 0)
        for frame = 0xa6, 0xb6 do
            t:cmd(OP.MASKED_TILE, actor, frame)
            t:cmd(OP.NOP, 6, 0x5b)           -- the frame's floor and sprite
        end
    end)
end


---------------------------------------------------------------------------
-- The opening after Zeno's speech.  Button1Click case 20 -> openning2,
-- whose tail runs madoushilivework (the four Magic Sergeants) and whose
-- 0x0b/1 step runs openning31 (the capture).  P3b execution oracle:
-- docs/re/16-event-pilot.md section 8.


-- madoushilivework (0x461ef4): sound 7, then the Magic Sergeants (0x5d)
-- fade in at the four map indices held in work_558 (0x48c558),
-- event_selector (0x48c55c), work_560 (0x48c560) and work_564 (0x48c564):
-- mask frames 0xa7, 0xa9, 0xab, 0xaf, 0xb6, each over the four.
-- madoushi2 (0x462560): splice 0x0b/3, floor 49.  Puts the Magic Sergeant
-- (0x5d) on the eight cells around index 27 (Zeno, from zenolivework) and
-- fades them in: eight mask frames 0xa7 .. 0xb6, each over the eight cells.
-- The original ends with one more count increment than it writes, so the
-- slot above the last command (left over from an earlier, longer timeline)
-- is consumed first.
local MADOUSHI2_CELLS = { 15, 16, 17, 28, 39, 38, 37, 26 }  -- round Zeno
builders.madoushi2 = function()
    for _, i in ipairs({ 0x0f, 0x10, 0x11, 0x1c, 0x27, 0x26, 0x25, 0x1a }) do
        set_tile(i, RAW.MAGIC_SERGEANT)
    end
    present("madoushi_setup")          -- off-screen bitmaps (0x48c51c..524)
    timer2on()
    play(function(t)
        for _, mask in ipairs({ 0xa7, 0xa9, 0xab, 0xad, 0xaf, 0xb1, 0xb3, 0xb6 }) do
            for _, index in ipairs(MADOUSHI2_CELLS) do
                t:cmd(OP.MASKED_TILE, index, mask)
                t:cmd(OP.NOP, 6, 0x5d)
            end
        end
    end)
    set_count(get_count() + 1)         -- 0x462560 tail: count++ without a write
end

builders.madoushilivework = function()
    present("madoushi_setup")          -- off-screen bitmaps (0x48c51c..524)
    timer2on()
    local actors = {
        aux.work_558 & 0xffff, aux.event_selector & 0xffff,
        aux.work_560 & 0xffff, aux.work_564 & 0xffff,
    }
    play(function(t)
        t:tick(OP.SOUND, SND.F3, 0)
        for _, frame in ipairs({ 0xa7, 0xa9, 0xab, 0xaf, 0xb6 }) do
            for _, index in ipairs(actors) do
                t:cmd(OP.MASKED_TILE, index, frame)
                t:cmd(OP.NOP, 6, 0x5d)
            end
        end
    end)
end

-- openning2 (0x464048): the capture, after Zeno's speech.  The four Magic
-- Sergeants appear round the hero at (4,8) (madoushilivework), sub-event
-- 21 ("What? Oh!"), the hit sound and the hero's hit frame, (4,8) floor
-- again, the item panel and the title (sub-dispatch 7); then open1's 16
-- dissolve frames 0xe8 .. 0xf7 with the hit sound each, the shrinking
-- frames (openning9 phase 1, from the empty command's arg1 before each)
-- and openning31.
builders.openning2 = function()
    timer2on()
    present("button_caption", 0xdb)    -- Button1.Caption := ListBox2[0xdb]
    local facing = st(ST.FACING)
    local f1 = ({ [1] = 0xbf, [2] = 0xc7, [3] = 0xcf, [4] = 0xd7 })[facing]
    local f2 = ({ [1] = 0xc0, [2] = 0xc8, [3] = 0xd0, [4] = 0xd8 })[facing]
    if not f1 then
        fail(string.format("openning2: facing %d", facing))
    end
    local frames = TILE_PX == 0x20 and 400 or 500
    local n = TILE_PX == 0x20 and 0x28 or 0x32
    play(function(t)
        t:call("madoushilivework", function()
            aux.work_558 = cell(4, 7)
            aux.event_selector = cell(4, 9)
            aux.work_560 = cell(3, 8)
            aux.work_564 = cell(5, 8)
        end)
        t:cmd(OP.START_SUBEVENT, 0x0b, 0)        -- sub-event 21
        t:wait(5)
        t:tick(OP.SOUND, SND.ZENO, 0)
        t:cmd(OP.MASKED_TILE, cell(4, 8), f2)    -- the hero hit
        t:cmd(OP.NOP, 6, f1)
        t:wait(4)
        t:tick(OP.SET_TILE, cell(4, 8), 6)       -- (4,8) is floor again
        t:tick(OP.SHOW_ITEMS, 0, 0)
        t:cmd(OP.SUBDISPATCH, SUB.TITLE, 0)
        t:wait(25)
        t:cmd(OP.SUBDISPATCH, SUB.HIDE_IMAGES, 0)
        for frame = 0xe8, 0xf7 do                -- open1, each with sound 4
            t:wait(1)
            t:tick(OP.SOUND, SND.ZENO, 0)
            t:cmd(OP.SUBDISPATCH, SUB.OPENNING9, frame)
        end
        t:wait(3)
        t:cmd(OP.SUBDISPATCH, SUB.OPENNING9, 0)
        t:cmd(OP.NOP, 1, 0)
        for i = 0, n do
            for k = 0, 5 do
                t:cmd(OP.SUBDISPATCH, SUB.OPENNING9, (i * 6 + 0x64 + k) & 0xffff)
                t:cmd(OP.NOP, 1, 0)
            end
        end
        t:cmd(OP.SUBDISPATCH, SUB.OPENNING9, frames)
        t:cmd(OP.NOP, 1, 0)
        t:cmd(OP.SUBDISPATCH, SUB.OPENNING31, 0)
    end)
end

-- openning31 (0x4653b8): the capture, timeline sub-dispatch 0x0b/1.  Normal
-- run: life/attack/defense 400/10/10; second ("R") run: all special items
-- are taken instead.  Always: Sacred gear and display levels cleared,
-- floor 2 at (2,7) with floor limit 2, state[0xc] = 1; then the wake-up
-- dialogues (sub-events 11, 11, 12, 13) and a player redraw.
builders.openning31 = function()
    if st(ST.BGM) ~= 0 then present("media_stop") end
    present("panels_restore", st(ST.OPTION_A))   -- center/left(r)/right(r)
    present("button_caption")          -- Button1.Caption := 'OK' (0x4655cc)
    if st(ST.OPTION_A) == 0 then
        set_state(ST.LIFE, 400)
        set_state(ST.ATTACK, 10)
        set_state(ST.DEFENSE, 10)
    else
        for s = 0, 14 do set_state(ST.SPECIAL0 + s, 0) end
    end
    set_state(ST.SHIELD_SACRED, 0)
    set_state(ST.SWORD_SACRED, 0)
    set_state(ST.SHIELD_LEVEL, 0)
    set_state(ST.WEAPON_LEVEL, 0)
    set_state(ST.BATTLE_RENDER, 1)
    set_state(ST.Y, 7)
    set_state(ST.X, 2)
    set_state(ST.FLOOR_LIMIT, 2)
    set_state(ST.FLOOR, 2)
    timer2on()
    play(function(t)
        t:cmd(OP.SPRITE, 0, 0)
        t:cmd(OP.SHOW_ITEMS, 0, 0)
        t:tick(OP.START_SUBEVENT, 1, 0)          -- sub-events 11, 11, 12, 13
        t:tick(OP.START_SUBEVENT, 1, 0)
        t:tick(OP.START_SUBEVENT, 2, 0)
        t:tick(OP.START_SUBEVENT, 3, 0)
        t:tick(OP.SUBDISPATCH, SUB.PLAYER_UPDATE, 0)
    end)
    present("disp")
    present("timer1", 0)
end


---------------------------------------------------------------------------
-- image34click (0x484de0) -> append (0x484de8): the ending's last picture
-- clicked.  BGM stopped, the play panels and Panel2/Image34 hidden, Timer2
-- on (TTimer.SetEnabled, not timer2on): the timeline's rest -- 0x0b/6 --
-- sets option A (or B) and runs gameover.
builders.append = function()
    if st(ST.BGM) ~= 0 then present("media_stop") end
    present("ending_hide")
    present("timer2", 1)
end
