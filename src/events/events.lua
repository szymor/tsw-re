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

-- Floor-3 opening (Zeno): fully ported to builders on the command VM
-- (handan -> mevent -> zenolivework, Zeno's speech, Button1Click case 20 ->
-- openning2 -> madoushilivework, openning31, the wake-up dialogues).

-- The floor-2 prisoner (raw 0x15) runs on the touzoku builder: sub-event
-- 14 in the cell, sub-event 15 at the tunnel (Button1Click cases 0x0e/0x0f
-- walk him out).

-- Secret walls (raw 0x09, floors 5 and 9) run on handan case 9 in
-- builders.lua: status text 0x89, the wall slides down, the player stays.

-- Merchants (raw 0x13/0x14) run on the syounin builder with the Yes/No
-- answers Button2Click / Button7Click (builders.lua).

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
