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

-- Original roujin flags are state indices.  F2 is confirmed at 0x62
-- (0x4b8810); 0x43..0x45 are also DAT_0048c55c selectors, so F3/F4/F6 stay
-- provisional until the roujin branches are lifted.
local F_OLD_MAN_F2 = 0x62
local F_OLD_MAN_F3 = 0x43
local F_OLD_MAN_F4 = 0x44
local F_OLD_MAN_F6 = 0x45
-- Floor-10 handler guard 0x4b8830 -> state index 0x6a.
local F_FLOOR10 = 0x6a

events = {}

events["2:76:25"] = function()       -- floor 2 old man
    if get_state(F_OLD_MAN_F2) ~= 0 then
        message("I have done what I can. Good luck!")
        return
    end
    local a = get_state(S_ATTACK)
    local d = get_state(S_DEFENSE)
    set_state(S_ATTACK, a + math.floor(a * 3 / 100))
    set_state(S_DEFENSE, d + math.floor(d * 3 / 100))
    set_state(F_OLD_MAN_F2, 1)
    message("Thank you for your help. I will raise your offensive and defensive power 3 percent as a token of my gratitude. [You begin feeling stronger]")
end

events["3:43:25"] = function()       -- floor 3 old man: Orb of the Hero
    if get_state(F_OLD_MAN_F3) ~= 0 then
        message("I have done what I can. Good luck!")
        return
    end
    set_state(S_SPECIAL0, 2)
    set_state(F_OLD_MAN_F3, 1)
    message("I will grant you the orb of the hero. You will now be able to save your game, and to forecast the results of fighting monsters on the current floor. Use it often.")
end

-- Floor-3 opening (Zeno).  The trigger itself is ported: handan raw 0x07
-- -> mevent case 0x463a2c -> zenolivework runs as builders on the command
-- VM (src/events/builders.lua); it moves the player onto (4,8), clears the
-- tile 0x07 -> 0x06 (the one-shot, no flag) and animates Zeno.  The VM then
-- reaches op 0x0a sub-event 20 (zyouout), whose dialogue and the capture
-- are not recovered from the binary yet (P3b execution oracle).  This
-- transitional coroutine covers that sub-event only.  The text is the
-- captured original; actor positions/timing are provisional.  The capture
-- itself is recovered by the P3b execution oracle (16-event-pilot.md
-- section 8): confirming sub-event 20 runs Button1Click case 20, which
-- appends the openning2 timeline; its 0x0b/1 step (openning31, 0x4655e0..)
-- sets floor 2, floor limit 2, (2,7), life 400, attack/defense 10, the
-- weapon/shield display levels to 0 and clears state 36/41.  Button1Click
-- also marks every confirmed sub-event as state[0x50 + id] = 1.
events["subevent:20"] = function()
    spawn_actor(0x19, 2, 5)
    spawn_actor(0x19, 3, 6)
    spawn_actor(0x19, 4, 7)
    spawn_actor(0x19, 5, 6)
    message("The Magic Sergeant, Zeno, says \"Welcome to The Magic Tower. You are the hundredth man who dared to enter my tower. You will now face my guards. Should you happen to survive, I will fight with you in person specially. You should accept your fate.\"")
    message("What? Oh!")
    cutscene_image(3, 180)       -- open2.bmp already contains the text
    for i = 1, 4 do remove_actor(i) end
    set_state(0x50 + 20, 1)      -- Button1Click 0x44da2a: sub-event 20 seen
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

events["4:9:25"] = function()        -- floor 4 old man
    if get_state(F_OLD_MAN_F4) ~= 0 then
        message("I have done what I can. Good luck!")
        return
    end
    set_state(F_OLD_MAN_F4, 1)
    message("There are doors which cannot be opened with a key; these doors can only open by beating the monsters which guard them.")
end

events["6:80:25"] = function()       -- floor 6 old man
    if get_state(F_OLD_MAN_F6) ~= 0 then
        message("I have done what I can. Good luck!")
        return
    end
    set_state(F_OLD_MAN_F6, 1)
    message("If you buy goods from merchants, you will be able to hear important information later.")
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
