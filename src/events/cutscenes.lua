-- cutscenes.lua: Cutscenes: Zeno's appearance (zenolivework), the opening after
-- his speech (openning2, openning31, madoushi2) and the ending's last
-- picture (append).
-- Loaded by builders.lua (include); see it for the API.

-- zenolivework (0x461a0c): Zeno's appearance.  17 three-sprite frames
-- 0xb6 -> 0xa6 at the actor index in aux.work_558, then sound 7.
-- Also reached as timeline sub-event 0x0b/2.

builders.zenolivework = function()
    present("zeno_setup")
    timer2on()
    local actor = aux.work_558 & 0xffff
    for frame = 0xb6, 0xa6, -1 do
        emit(OP.NOP, 6, 0x5b)
        emit(OP.MASKED_TILE, actor, frame)
    end
    tick(OP.SOUND, SND.F3, 0)
end


---------------------------------------------------------------------------
-- The opening after Zeno's speech.  Button1Click case 20 -> openning2,
-- whose tail runs madoushilivework (the four Magic Sergeants) and whose
-- 0x0b/1 step runs openning31 (the capture).  P3b execution oracle:
-- docs/re/16-event-pilot.md section 8.


-- madoushilivework (0x461ef4): animate the actors at the four map indices
-- held in work_564 (0x48c564), work_560 (0x48c560),
-- event_selector (0x48c55c) and work_558 (0x48c558), frames
-- 0xb6, 0xaf, 0xab, 0xa9, 0xa7; then sound 7.
-- madoushi2 (0x462560): splice 0x0b/3, floor 49.  Puts the Magic Sergeant
-- (0x5d) on the eight cells around index 0x1b (Zeno, from zenolivework) and
-- fades them in: eight mask frames 0xb6 .. 0xa7, each over the eight cells.
-- The original ends with one more count increment than it writes, so the
-- slot above the last command (left over from an earlier, longer timeline)
-- is consumed first.
local MADOUSHI2_CELLS = { 0x1a, 0x25, 0x26, 0x27, 0x1c, 0x11, 0x10, 0x0f }
builders.madoushi2 = function()
    for _, i in ipairs({ 0x0f, 0x10, 0x11, 0x1c, 0x27, 0x26, 0x25, 0x1a }) do
        set_tile(i, RAW.MAGIC_SERGEANT)
    end
    present("madoushi_setup")          -- off-screen bitmaps (0x48c51c..524)
    timer2on()
    for _, mask in ipairs({ 0xb6, 0xb3, 0xb1, 0xaf, 0xad, 0xab, 0xa9, 0xa7 }) do
        for _, index in ipairs(MADOUSHI2_CELLS) do
            emit(OP.NOP, 6, 0x5d)
            emit(OP.MASKED_TILE, index, mask)
        end
    end
    set_count(get_count() + 1)         -- 0x462560 tail: count++ without a write
end

builders.madoushilivework = function()
    present("madoushi_setup")          -- off-screen bitmaps (0x48c51c..524)
    timer2on()
    local actors = {
        aux.work_564 & 0xffff, aux.work_560 & 0xffff,
        aux.event_selector & 0xffff, aux.work_558 & 0xffff,
    }
    for _, frame in ipairs({ 0xb6, 0xaf, 0xab, 0xa9, 0xa7 }) do
        for _, index in ipairs(actors) do
            emit(OP.NOP, 6, 0x5d)
            emit(OP.MASKED_TILE, index, frame)
        end
    end
    tick(OP.SOUND, SND.F3, 0)
end

-- openning2 (0x464048): the capture cutscene.  openning9 frames
-- (0x0b/0xc; phase = previous command's arg1), title, the player's facing
-- frames at (4,8), sub-event 21, then the sergeants.
builders.openning2 = function()
    timer2on()
    present("button_caption", 0xdb)    -- Button1.Caption := ListBox2[0xdb]
    emit(OP.SUBDISPATCH, SUB.OPENNING31, 0)                   -- openning31 (runs last)
    emit(OP.NOP, 1, 0)
    local frames = TILE_PX == 0x20 and 400 or 500
    emit(OP.SUBDISPATCH, SUB.OPENNING9, frames)
    local n = TILE_PX == 0x20 and 0x28 or 0x32
    for i = n, 0, -1 do
        local base = i * 6
        for k = 5, 0, -1 do
            emit(OP.NOP, 1, 0)
            emit(OP.SUBDISPATCH, SUB.OPENNING9, (base + 0x64 + k) & 0xffff)
        end
    end
    emit(OP.NOP, 1, 0)
    emit(OP.SUBDISPATCH, SUB.OPENNING9, 0)
    wait(3)
    for frame = 0xf7, 0xe8, -1 do      -- open1 frames, each with sound 4
        emit(OP.SUBDISPATCH, SUB.OPENNING9, frame)
        tick(OP.SOUND, SND.ZENO, 0)
        wait(1)
    end
    emit(OP.SUBDISPATCH, SUB.HIDE_IMAGES, 0)                -- hide event images
    wait(25)
    emit(OP.SUBDISPATCH, SUB.TITLE, 0)                -- title
    tick(OP.SHOW_ITEMS, 0, 0)
    tick(OP.SET_TILE, 0x5c, 6)                -- (4,8) is floor again
    wait(4)
    local facing = st(ST.FACING)
    local f1 = ({ [1] = 0xbf, [2] = 0xc7, [3] = 0xcf, [4] = 0xd7 })[facing]
    local f2 = ({ [1] = 0xc0, [2] = 0xc8, [3] = 0xd0, [4] = 0xd8 })[facing]
    if not f1 then
        fail(string.format("openning2: facing %d", facing))
    end
    emit(OP.NOP, 6, f1)
    emit(OP.MASKED_TILE, 0x5c, f2)
    tick(OP.SOUND, SND.ZENO, 0)
    wait(5)
    emit(OP.START_SUBEVENT, 0x0b, 0)                -- sub-event 21: "What? Oh!"
    aux.work_558 = 0x51            -- 0x48c558: (4,7)
    aux.event_selector = 0x67          -- 0x48c55c: (4,9)
    aux.work_560 = 0x5b             -- 0x48c560: (3,8)
    aux.work_564 = 0x5d         -- 0x48c564: (5,8)
    call("madoushilivework")
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
    tick(OP.SUBDISPATCH, SUB.PLAYER_UPDATE, 0)
    tick(OP.START_SUBEVENT, 3, 0)
    tick(OP.START_SUBEVENT, 2, 0)
    tick(OP.START_SUBEVENT, 1, 0)
    tick(OP.START_SUBEVENT, 1, 0)
    emit(OP.SHOW_ITEMS, 0, 0)
    emit(OP.SPRITE, 0, 0)
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
