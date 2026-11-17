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
--   get_map_byte(offset)        raw byte of the map block (header included)
--   monster_stat(index, field)   monster table lookup (hp/attack/defense/gold)
--   aux.<name>                  auxiliary globals (see event_builders.h)
--   call(name)                  run another builder
--   present(kind, ...)          presentation side effect; the engine acts on
--                               timer2on, timer2(0|1), zyouout, dialog_hide, ...
--   get_count() / set_count(n)  command count (0x48c5ac); set_count shrinks,
--                               or grows by one onto the stale slot above
--   fail(msg)                   abort: path not ported
--   idiv(a, b)                  C integer division (truncates toward zero)
--
-- Named constants at the top cover the VM opcodes, sub-dispatch commands,
-- state indices and map geometry.  Handler-specific sprite/message/tile IDs
-- stay as annotated literals until P5 gives them verified names.
--
-- The handlers live in modules beside this file, run in this order by
-- include() (src/event_builders.c).  A name one module shares with others
-- is a global; the rest stay local to their module.  Reading an undefined
-- global is an error, so a missing name fails loudly instead of being nil.
--
--   common     constants, timeline and presentation helpers, shared movers
--   battle     moncheck, taisen, taisen2
--   floors     mevent, ichicheck
--   handan     the tile in front of the hero: doors, items, stairs
--   npcs       thief, fairy, old men, merchants, altar
--   confirm    Button1Click
--   cutscenes  Zeno's appearance, the opening, the ending's last picture
--   items      item use

setmetatable(_G, {
    __index = function(_, name)
        error("undefined global '" .. tostring(name) .. "'", 2)
    end,
})

for _, module in ipairs({ "common", "battle", "floors", "handan", "npcs",
                          "confirm", "cutscenes", "items" }) do
    include(module)
end
