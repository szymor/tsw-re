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

-- Floor 10: the boss room runs on ichicheck (builders.lua, from the
-- trigger tile via mevent) and the fairy on yousei.
