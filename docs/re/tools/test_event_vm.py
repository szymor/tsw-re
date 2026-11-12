#!/usr/bin/env python3
"""Unit tests for the reference event VM."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import event_vm as vm  # noqa: E402


class EventVMTests(unittest.TestCase):
    def make(self) -> vm.EventVM:
        v = vm.EventVM(state=vm.default_state())
        v.monster = vm.Monster(hp=100, attack=20, defense=10, gold=5)
        v.monster_hp = 100
        v.monster_index = 0
        return v

    def test_tail_execution_order(self):
        v = self.make()
        v.load([
            vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 1),
            vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 2),
            vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 3),
        ])
        self.assertEqual(v.count, 3)
        v.run()
        self.assertEqual(v.state[vm.ST_GOLD], 1)
        self.assertEqual(v.count, 0)

    def test_opcode_0_and_12_are_one_tick_nops(self):
        v = self.make()
        v.load([vm.Command(vm.OP_NOP), vm.Command(vm.OP_NOP_12)])
        self.assertEqual(v.run(), 2)
        self.assertEqual([e["kind"] for e in v.log], ["nop", "nop"])

    def test_set_state_is_direct_index(self):
        v = self.make()
        v.load([vm.Command(vm.OP_SET_STATE, vm.ST_ATTACK, 42)])
        v.run()
        self.assertEqual(v.state[vm.ST_ATTACK], 42)

    def test_set_tile_and_silent_set_tile(self):
        v = self.make()
        v.state[vm.ST_FLOOR] = 7
        v.load([
            vm.Command(vm.OP_SET_TILE, 10, 0x06),
            vm.Command(vm.OP_SET_TILE_SILENT, 11, 0x07),
        ])
        v.run()
        self.assertEqual(v.tile(7, 10), 0x06)
        self.assertEqual(v.tile(7, 11), 0x07)
        self.assertEqual([e["kind"] for e in v.log], ["set_tile", "set_tile"])
        # Tail execution: the silent command runs first.
        self.assertTrue(v.log[0]["silent"])
        self.assertFalse(v.log[1]["silent"])

    def test_life_delta_only_applies_damage(self):
        v = self.make()
        v.state[vm.ST_LIFE] = 100
        v.state[vm.ST_DEFENSE] = 10
        v.monster.attack = 25
        v.load([vm.Command(vm.OP_LIFE_DELTA)])
        v.run()
        self.assertEqual(v.state[vm.ST_LIFE], 85)

        v = self.make()
        v.state[vm.ST_LIFE] = 100
        v.state[vm.ST_DEFENSE] = 50
        v.monster.attack = 10
        v.load([vm.Command(vm.OP_LIFE_DELTA)])
        v.run()
        self.assertEqual(v.state[vm.ST_LIFE], 100)

    def test_monster_delta_and_cross_multiplier(self):
        v = self.make()
        v.state[vm.ST_ATTACK] = 13
        v.monster.defense = 10
        v.monster_hp = 100
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 97)

        v = self.make()
        v.state[vm.ST_ATTACK] = 6
        v.monster.defense = 10
        v.monster_index = 12
        v.state[vm.ST_SPECIAL_FIRST + vm.SLOT_CROSS] = 1
        v.monster_hp = 100
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 98)

        # A monster whose defense exceeds the player's attack is undamaged.
        v = self.make()
        v.state[vm.ST_ATTACK] = 3
        v.monster.defense = 10
        v.monster_hp = 100
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 100)

    def test_lucky_gold_multiplier(self):
        v = self.make()
        v.state[vm.ST_GOLD] = 0
        v.state[vm.ST_SPECIAL_FIRST + vm.SLOT_LUCKY_GOLD] = 1
        v.load([vm.Command(vm.OP_ADD_GOLD, 10)])
        v.run()
        self.assertEqual(v.state[vm.ST_GOLD], 20)

    def test_halve_life_and_gameover(self):
        v = self.make()
        v.state[vm.ST_LIFE] = 101
        v.load([vm.Command(vm.OP_HALVE_LIFE)])
        v.run()
        self.assertEqual(v.state[vm.ST_LIFE], 50)
        self.assertFalse(v.gameover)

        v = self.make()
        v.state[vm.ST_LIFE] = 0
        v.load([vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 9), vm.Command(vm.OP_HALVE_LIFE)])
        v.run()
        self.assertTrue(v.gameover)
        self.assertEqual(v.state[vm.ST_GOLD], 0)
        self.assertEqual(v.count, 0)

    def test_subevent_splice_overwrites_current_command(self):
        v = self.make()

        def handler(interp: vm.EventVM, sub: int, arg: int) -> None:
            interp.append(vm.OP_SET_STATE, vm.ST_GOLD, 77)

        v.subevent_handler = handler
        # Tail execution means the 0xb command must be last for the splice to
        # run before the normal command at index 1.
        v.load([vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 5), vm.Command(vm.OP_SUBDISPATCH, 1, 0)])
        v.run()
        self.assertEqual(v.state[vm.ST_GOLD], 5)
        self.assertEqual([e["kind"] for e in v.log].count("subdispatch"), 1)
        set_logs = [e for e in v.log if e["kind"] == "set_state"]
        self.assertEqual([e["value"] for e in set_logs], [77, 5])

    def test_floor_change(self):
        v = self.make()
        v.state[vm.ST_FLOOR] = 3
        v.load([vm.Command(vm.OP_SUBDISPATCH, 0x0B, 20)])
        v.run()
        self.assertEqual(v.state[vm.ST_FLOOR], 20)
        self.assertIn({"kind": "change_floor", "floor": 20}, v.log)

    def test_actor_chain_consumes_previous_draw(self):
        v = self.make()
        v.load([
            vm.Command(vm.OP_DRAW_ACTOR, 1, 0x10),
            vm.Command(vm.OP_DRAW_ACTOR, 1, 0x11),
        ])
        v.run()
        draws = [e for e in v.log if e["kind"] == "draw_actor"]
        self.assertEqual(len(draws), 2)
        self.assertTrue(any(e.get("chained") for e in draws))

    def test_three_sprite_animation_reads_previous_args(self):
        v = self.make()
        v.load([
            vm.Command(vm.OP_NOP, 7, 8),
            vm.Command(vm.OP_THREE_SPRITE_ANIM, 1, 2),
        ])
        v.run()
        entry = [e for e in v.log if e["kind"] == "three_sprite_anim"][0]
        self.assertEqual(entry["previous"], (7, 8))
        self.assertEqual(entry["current"], (1, 2))

    def test_add_life_zero_truncates(self):
        v = self.make()
        v.state[vm.ST_LIFE] = 10
        v.load([
            vm.Command(vm.OP_SET_STATE, vm.ST_GOLD, 99),
            vm.Command(vm.OP_ADD_LIFE, -10),
        ])
        v.run()
        self.assertTrue(v.gameover)
        self.assertEqual(v.state[vm.ST_GOLD], 0)

    def test_zero_state_is_not_overwritten(self):
        v = vm.EventVM(state=[0] * vm.STATE_INT_COUNT)
        self.assertEqual(v.state[vm.ST_LIFE], 0)
        self.assertEqual(v.state[vm.ST_ATTACK], 0)

    def test_all_opcodes_have_a_handler(self):
        v = self.make()
        for opcode in range(0x17):
            handler = getattr(v, f"_op_{opcode:02x}", None)
            self.assertIsNotNone(handler, f"missing opcode {opcode:#04x}")
            v.load([vm.Command(opcode)])
            v.run()
            self.assertFalse(
                any(e["kind"] == "unknown_opcode" for e in v.log),
                f"opcode {opcode:#04x} hit the unknown path",
            )
            v.log.clear()

    def test_presentation_opcodes_are_logged(self):
        cases = {
            vm.OP_SOUND: "sound",
            vm.OP_SET_SPRITE: "sprite",
            vm.OP_ALTAR: "altar",
            vm.OP_SHOW_ITEMS: "show_items",
            vm.OP_START_SUBEVENT: "start_subevent",
            vm.OP_MONSTER_MOVE: "monster_move",
            vm.OP_DOOR_OPEN: "door_open",
            vm.OP_STOP_BGM: "stop_bgm",
            vm.OP_RESET: "reset",
        }
        for opcode, kind in cases.items():
            v = self.make()
            v.load([vm.Command(opcode, 1, 2)])
            v.run()
            self.assertIn(kind, [e["kind"] for e in v.log], f"opcode {opcode:#04x}")

    def test_opcode_0e_damage_ignores_cross_multiplier(self):
        v = self.make()
        v.state[vm.ST_ATTACK] = 13
        v.monster.defense = 10
        v.monster_hp = 100
        v.monster_index = 12
        v.state[vm.ST_SPECIAL_FIRST + vm.SLOT_CROSS] = 1
        v.load([vm.Command(vm.OP_BATTLE_HIT, 1, 2)])
        v.run()
        self.assertEqual(v.monster_hp, 97)

    def test_dragon_slayer_multiplier(self):
        v = self.make()
        v.state[vm.ST_ATTACK] = 6
        v.monster.defense = 10
        v.monster_hp = 100
        v.monster_index = 19
        v.state[vm.ST_SPECIAL_FIRST + vm.SLOT_DRAGON_SLAYER] = 1
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 98)

    def test_difficulty_multiplier(self):
        v = self.make()
        v.state[vm.ST_ATTACK] = 20
        v.state[vm.ST_OPTION_A] = 1
        v.monster.defense = 10
        v.monster_hp = 100
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 100)

        v = self.make()
        v.state[vm.ST_ATTACK] = 22
        v.state[vm.ST_OPTION_A] = 1
        v.monster.defense = 10
        v.monster_hp = 100
        v.load([vm.Command(vm.OP_MONSTER_DELTA)])
        v.run()
        self.assertEqual(v.monster_hp, 98)

    def test_timer2_tick_runs_at_most_two_commands(self):
        v = self.make()
        v.load([
            vm.Command(vm.OP_NOP),
            vm.Command(vm.OP_NOP),
            vm.Command(vm.OP_NOP),
        ])
        self.assertEqual(vm.timer2_tick(v), 2)
        self.assertEqual(v.count, 1)
        self.assertEqual(vm.timer2_tick(v), 1)
        self.assertEqual(v.count, 0)
        self.assertEqual(vm.timer2_tick(v), 0)

    def test_subevent_splice_can_append_multiple_commands(self):
        v = self.make()

        def handler(interp: vm.EventVM, sub: int, arg: int) -> None:
            interp.append(vm.OP_SET_STATE, vm.ST_GOLD, 10)
            interp.append(vm.OP_SET_STATE, vm.ST_GOLD, 20)

        v.subevent_handler = handler
        v.load([vm.Command(vm.OP_NOP), vm.Command(vm.OP_SUBDISPATCH, 1, 0)])
        v.run()
        values = [e["value"] for e in v.log if e["kind"] == "set_state"]
        self.assertEqual(values, [20, 10])

    def test_actor_chain_requires_same_mode(self):
        v = self.make()
        v.load([
            vm.Command(vm.OP_DRAW_ACTOR, 1, 0x10),
            vm.Command(vm.OP_DRAW_ACTOR, 0, 0x11),
        ])
        v.run()
        draws = [e for e in v.log if e["kind"] == "draw_actor"]
        self.assertFalse(any(e.get("chained") for e in draws))
        self.assertEqual(v.count, 0)

    def test_signed_command_words(self):
        v = self.make()
        v.load([vm.Command(vm.OP_ADD_GOLD, 0xFFFF)])
        v.run()
        self.assertEqual(v.state[vm.ST_GOLD], -1)

        v = self.make()
        v.load([vm.Command(vm.OP_SET_STATE, vm.ST_ATTACK, 0x8000)])
        v.run()
        self.assertEqual(v.state[vm.ST_ATTACK], -32768)

    def test_reset_clears_options(self):
        v = self.make()
        v.state[vm.ST_OPTION_A] = 7
        v.state[vm.ST_OPTION_B] = 8
        v.load([vm.Command(vm.OP_RESET)])
        v.run()
        self.assertEqual(v.state[vm.ST_OPTION_A], 0)
        self.assertEqual(v.state[vm.ST_OPTION_B], 0)
        self.assertTrue(v.gameover)

    def test_door_open_writes_tile_and_interval(self):
        v = self.make()
        v.state[vm.ST_FLOOR] = 4
        v.load([vm.Command(vm.OP_NOP, 0, 5), vm.Command(vm.OP_DOOR_OPEN, 30, 1)])
        v.run()
        self.assertEqual(v.tile(4, 30), 6)
        self.assertEqual(v.anim_progress, 5)
        self.assertEqual(v.interval_ms, 10)
        v = self.make()
        v.speed = vm.SPEED_HIGH
        v.state[vm.ST_FLOOR] = 4
        v.set_tile(4, 33, 0x22)
        v.load([vm.Command(vm.OP_DOOR_OPEN, 31, 5), vm.Command(vm.OP_DOOR_OPEN, 32, 8),
                vm.Command(vm.OP_DOOR_OPEN, 33, 0)])
        v.run()
        self.assertEqual((v.tile(4, 31), v.tile(4, 32), v.tile(4, 33)), (4, 8, 0x22))
        self.assertEqual(v.interval_ms, 8)

    def test_subdispatch_all_variants_are_logged(self):
        expected = {
            1: "subdispatch", 2: "subdispatch", 3: "subdispatch",
            4: "vampire", 5: "subdispatch", 6: "gameover",
            7: "title", 8: "player_update", 9: "player_animation",
            0x0A: "subdispatch", 0x0B: "change_floor",
            0x0C: "opening9", 0x0D: "hide_message",
            0x0E: "hide_event_images", 0x0F: "fade",
        }
        for sub, kind in expected.items():
            v = self.make()
            v.state[vm.ST_FLOOR] = 5
            v.load([vm.Command(vm.OP_SUBDISPATCH, sub, 6)])
            v.run()
            self.assertIn(kind, [e["kind"] for e in v.log],
                          f"subdispatch {sub:#x}")

    def test_monster_move_moves_the_tile(self):
        v = self.make()
        v.state[vm.ST_FLOOR] = 10
        v.set_tile(10, 38, 0x2a)
        v.load([vm.Command(vm.OP_MONSTER_MOVE, 38, 27)])
        v.run()
        self.assertEqual(v.tile(10, 27), 0x2a)
        self.assertEqual(v.tile(10, 38), 0x06)

    def test_opening9_phase_and_interval(self):
        v = self.make()
        v.speed = vm.SPEED_MIDDLE
        # Phase is the previous command's arg1: (0,1,0) then 0xb/12 frame 0.
        v.append(vm.OP_NOP, 1, 0)
        v.append(vm.OP_SUBDISPATCH, 0x0C, 0)
        v.step()
        entry = [e for e in v.log if e["kind"] == "opening9"][0]
        self.assertEqual((entry["phase"], entry["frame"]), (1, 0))
        self.assertEqual(v.interval_ms, 50)
        self.assertEqual(vm.opening9_interval_ms(vm.SPEED_HIGH, 1, 400), 130)
        self.assertEqual(vm.opening9_interval_ms(vm.SPEED_LOW, 0, 0xF7), 350)
        self.assertEqual(vm.opening9_interval_ms(vm.SPEED_MIDDLE, 0, 0xF6), 0)
        self.assertEqual(vm.opening9_interval_ms(vm.SPEED_MIDDLE, 6, 1), 450)


if __name__ == "__main__":
    unittest.main()
