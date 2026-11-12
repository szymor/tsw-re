-- Phase 3 event scripts for the floors 1-10 vertical slice.
-- State indices follow src/state.h.
local S_LIFE = 0
local S_ATTACK = 1
local S_DEFENSE = 2
local S_GOLD = 3
local S_KEY_YELLOW = 8
local S_KEY_RED = 9
local S_KEY_BLUE = 10
local S_SPECIAL0 = 17

-- The old men (raw 0x19/0x1a) run on the roujin builder
-- (src/events/builders.lua) with golden-vector parity; their text comes
-- from the original ListBox2 (data/text/messages.tsv).
-- Floor-10 handler guard 0x4b8830 -> state index 0x6a.
local F_FLOOR10 = 0x6a

events = {}

-- Floor-3 opening (Zeno).  Ported: the trigger (handan raw 0x07 -> mevent
-- case 0x463a2c -> idou/zenolivework builders on the command VM), Zeno's
-- speech (op 0x0a sub-event 20 -> zyouout with ListBox2[9]) and its confirm
-- (Button1Click: state[0x64] = 1).  Button1Click case 20 then calls
-- openning2, which is not ported yet; this transitional coroutine stands in
-- for it.  The capture values are recovered by the P3b execution oracle
-- (16-event-pilot.md section 8): openning31 (0x4655e0..) sets floor 2,
-- floor limit 2, (2,7), life 400, attack/defense 10, the weapon/shield
-- display levels to 0 and clears state 36/41.  Text is the captured
-- original; actor positions/timing are provisional.
events["subevent:20"] = function()
    spawn_actor(0x19, 2, 5)
    spawn_actor(0x19, 3, 6)
    spawn_actor(0x19, 4, 7)
    spawn_actor(0x19, 5, 6)
    message("What? Oh!")
    cutscene_image(3, 180)       -- open2.bmp already contains the text
    for i = 1, 4 do remove_actor(i) end
    change_floor(2)
    set_state(5, 2)              -- floor limit is set to 2 (0x4655ea)
    teleport(79)                 -- floor 2, (2,7)
    set_state(S_LIFE, 400)
    set_state(S_ATTACK, 10)
    set_state(S_DEFENSE, 10)
    set_state(0x0f, 0)           -- weapon display level (0x465632)
    set_state(0x10, 0)           -- shield display level (0x465639)
    set_state(0x24, 0)           -- equipment flag 36 (0x465640)
    set_state(0x29, 0)           -- equipment flag 41 (0x465647)
    set_tile(68, 0x15)           -- prisoner marker (already in the initial map)
end

events["2:68:21"] = function()       -- floor-2 prisoner, first dialogue
    message("What a relief you're okay! You were caught and thrown into this prison by the Magic Sergeant. You were unconscious when you got here. Nevertheless, you are so lucky that you just woke up. I've finished the escape tunnel. Let's break out.")
    set_tile(68, 6)
    set_tile(77, 0x15)           -- prisoner moves to (0,7)
end

events["2:77:21"] = function()       -- floor-2 prisoner, second dialogue
    message("O.K. Well, excuse my going first. Your weapon and shield were stolen by the guards. It's not wise to wander about without them. I believe there is a sword on the 5th floor and a shield on the 9th, you had better get them first of all. See you later!")
    set_tile(77, 6)
end

events["5:118:9"] = function()        -- floor 5 secret wall at (8,10)
    message("The wall collapsed!")
    set_tile(118, 6)
    teleport(118)
end

events["9:53:9"] = function()         -- floor 9 secret wall at (9,4)
    message("The wall collapsed!")
    set_tile(53, 6)
    teleport(53)
end

events["2:43:19"] = function()       -- floor 2 merchant
    local c = choice("Buy five yellow keys for 50 gold?", "Yes", "No")
    if c == 1 then
        if get_state(S_GOLD) < 50 then
            message("Merchant: You do not have enough gold.")
            return
        end
        add_gold(-50)
        add_state(S_KEY_YELLOW, 5)
        message("Merchant: Five yellow keys. A fine choice.")
    else
        message("Merchant: Come back later.")
    end
end

events["6:40:19"] = function()       -- floor 6 merchant
    local c = choice("Buy a blue key for 200 gold?", "Yes", "No")
    if c == 1 then
        if get_state(S_GOLD) < 200 then
            message("Merchant: You do not have enough gold.")
            return
        end
        add_gold(-200)
        add_state(S_KEY_BLUE, 1)
        message("Merchant: One blue key, as promised.")
    else
        message("Merchant: Another time.")
    end
end

events["7:5:19"] = function()        -- floor 7 merchant
    local c = choice("Increase life by 2000 for 1000 gold?", "Yes", "No")
    if c == 1 then
        if get_state(S_GOLD) < 1000 then
            message("Merchant: You do not have enough gold.")
            return
        end
        add_gold(-1000)
        heal(2000)
        message("Merchant: Your life force has grown.")
    else
        message("Merchant: A wise decision, perhaps.")
    end
end

events["10:floor"] = function()      -- area-1 boss / fairy / checkpoint
    if get_state(F_FLOOR10) ~= 0 then
        return
    end
    message("The Golden Knight blocks the way!")
    local won = battle(20)
    if not won then
        message("The Golden Knight remains victorious.")
        return
    end
    message("The fairy appears and records your progress.")
    set_state(F_FLOOR10, 1)
    local score = rank_score()
    message("Area 1 score: " .. score)
    local ok = checkpoint()
    if ok then
        message("Checkpoint saved.")
    else
        message("The checkpoint could not be saved; use the Save menu.")
    end
    message("Ranking update is disabled until the sixth field/footer is decoded.")
end
