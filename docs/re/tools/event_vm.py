#!/usr/bin/env python3
"""Reference model of the original 6-byte event timeline VM.

The original stores a command count/index at 0x48c5ac and commands at
0x48c74c:

    u16 opcode
    u16 arg1
    u16 arg2

``stackwork`` (0x4526c0) reads the command at the current count, executes it,
and decrements the count.  Handlers append by incrementing the count and
writing the three words.  Sub-events in opcode 0xb temporarily decrement the
count, append a replacement sequence, then increment it again; there is no
separate call stack.

This module models the *observable* state effects and records presentation
calls instead of trying to emulate VCL controls, images or audio.  It is a
test/reference model for the recovered timeline data, not a replacement for
the reimplementation's event runtime.

State indices follow ``src/state.h``.  The model is dependency-free; run the
unit tests with:

    python3 docs/re/tools/test_event_vm.py
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

# State-block indices (offset / 4) from src/state.h and 03-state-and-save.md.
ST_LIFE = 0
ST_ATTACK = 1
ST_DEFENSE = 2
ST_GOLD = 3
ST_FLOOR = 4
ST_FLOOR_LIMIT = 5
ST_X = 6
ST_Y = 7
ST_KEY_YELLOW = 8
ST_KEY_RED = 9
ST_KEY_BLUE = 10
ST_WEAPON_LEVEL = 0x03C // 4
ST_SHIELD_LEVEL = 0x040 // 4
ST_SPECIAL_FIRST = 0x044 // 4
ST_SPECIAL_LAST = 0x07C // 4
ST_OPTION_A = 0x27C // 4
ST_OPTION_B = 0x280 // 4
ST_FACING = 0x160 // 4
ST_MAGIC = 0x274 // 4
ST_VERSION = 0x2A8 // 4
STATE_INT_COUNT = 171

# Special-item slots used by the battle formulas.
SLOT_CROSS = 3
SLOT_DRAGON_SLAYER = 10
SLOT_LUCKY_GOLD = 14

# Opcodes.
OP_NOP = 0x00
OP_SOUND = 0x01
OP_SET_SPRITE = 0x02
OP_ALTAR = 0x03
OP_LIFE_DELTA = 0x04
OP_MONSTER_DELTA = 0x05
OP_SET_TILE = 0x06
OP_ADD_GOLD = 0x07
OP_SHOW_ITEMS = 0x08
OP_THREE_SPRITE_ANIM = 0x09
OP_START_SUBEVENT = 0x0A
OP_SUBDISPATCH = 0x0B
OP_MONSTER_MOVE = 0x0C
OP_DOOR_OPEN = 0x0D
OP_BATTLE_HIT = 0x0E
OP_STOP_BGM = 0x0F
ST_BGM = 0x5A  # BGM/sound state that op 0x0f rewrites through soundcheck


def floor_bgm(floor: int) -> int | None:
    """soundcheck (0x47c8f8): the floor's BGM id, None when no case."""
    if floor == 0:
        return 0x15
    for lo, hi, bgm in ((1, 10, 5), (11, 20, 6), (21, 30, 0x11),
                        (31, 40, 8), (41, 49, 9), (50, 50, 10)):
        if lo <= floor <= hi:
            return bgm
    return None
OP_ADD_LIFE = 0x10
OP_SET_TILE_SILENT = 0x11
OP_NOP_12 = 0x12
OP_HALVE_LIFE = 0x13
OP_SET_STATE = 0x14
OP_DRAW_ACTOR = 0x15
OP_RESET = 0x16


# Timer2 interval (12-event-vm.md section 10).  Speed = option.dat byte 3.
SPEED_HIGH, SPEED_MIDDLE, SPEED_LOW = 1, 2, 3
SPEED_INTERVAL_MS = {SPEED_HIGH: 150, SPEED_MIDDLE: 250, SPEED_LOW: 350}
DOOR_INTERVAL_MS = {SPEED_HIGH: 8, SPEED_MIDDLE: 10, SPEED_LOW: 12}
STAIRS_INTERVAL_MS = {SPEED_HIGH: 2, SPEED_MIDDLE: 6, SPEED_LOW: 10}


def opening9_interval_ms(speed: int, phase: int, frame: int) -> int:
    """Timer2 interval openning9 (0x482e60) sets for (phase, frame), 0 if
    none.  The phase is the previous command's arg1."""
    i = {SPEED_HIGH: 0, SPEED_LOW: 2}.get(speed, 1)
    if phase in (0, 2):
        if (phase, frame) in ((0, 0xF7), (2, 0xF8)):
            return (150, 250, 350)[i]
        return 0
    if phase == 1:
        if frame == 0:
            return (30, 50, 70)[i]
        if frame == 400:
            return (130, 200, 300)[i]
        return 0
    return {4: 100, 5: 60, 6: 450}.get(phase, 0)


def door_tile(kind: int) -> int | None:
    """Tile ``dooropen`` (0x4770e0) writes at arg1 for door kind ``arg2``."""
    if not 1 <= kind <= 12:
        return None
    return {5: 4, 8: 8}.get(kind, 6)


def s16(value: int) -> int:
    """Interpret a stored u16 command word as a signed 16-bit value."""
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


@dataclass
class Command:
    opcode: int
    arg1: int = 0
    arg2: int = 0


@dataclass
class Monster:
    hp: int = 0
    attack: int = 0
    defense: int = 0
    gold: int = 0


@dataclass
class EventVM:
    """Small reference interpreter for one event timeline.

    ``commands`` is 1-based like the original: index 0 is a sentinel and
    ``count`` points at the current command.  Use ``load()`` to install a
    command list or ``append()`` to build one.
    """

    state: list[int] = field(default_factory=lambda: [0] * STATE_INT_COUNT)
    map_tiles: dict[tuple[int, int], int] = field(default_factory=dict)
    commands: list[Command] = field(default_factory=lambda: [Command(0, 0, 0)])
    count: int = 0
    monster: Monster = field(default_factory=Monster)
    monster_hp: int = 0
    monster_index: int = 0
    current_sprite: int = 0
    anim_progress: int = 0
    speed: int = SPEED_MIDDLE
    interval_ms: int = SPEED_INTERVAL_MS[SPEED_MIDDLE]
    log: list[dict[str, object]] = field(default_factory=list)
    subevent_handler: Callable[["EventVM", int, int], None] | None = None
    gameover: bool = False

    # ------------------------------------------------------------------
    # command storage

    def load(self, commands: list[Command]) -> None:
        self.commands = [Command(0, 0, 0)] + list(commands)
        self.count = len(commands)

    def append(self, opcode: int, arg1: int = 0, arg2: int = 0) -> None:
        self.count += 1
        while len(self.commands) <= self.count:
            self.commands.append(Command(0, 0, 0))
        self.commands[self.count] = Command(opcode & 0xFFFF, arg1 & 0xFFFF, arg2 & 0xFFFF)

    def current(self) -> Command | None:
        if self.count <= 0 or self.count >= len(self.commands):
            return None
        return self.commands[self.count]

    def previous(self) -> Command | None:
        if self.count <= 1 or self.count - 1 >= len(self.commands):
            return None
        return self.commands[self.count - 1]

    # ------------------------------------------------------------------
    # helpers

    def tile(self, floor: int, index: int) -> int:
        return self.map_tiles.get((floor, index), 0)

    def set_tile(self, floor: int, index: int, value: int) -> None:
        self.map_tiles[(floor, index)] = value & 0xFF

    def slot(self, index: int) -> int:
        return self.state[ST_SPECIAL_FIRST + index]

    def _record(self, kind: str, **kwargs: object) -> None:
        entry: dict[str, object] = {"kind": kind}
        entry.update(kwargs)
        self.log.append(entry)

    def _clamp_life(self) -> None:
        if self.state[ST_LIFE] < 0:
            self.state[ST_LIFE] = 0

    def _game_over_from_life(self) -> None:
        self.gameover = True
        self._record("gameover", reason="life=0")
        # The original truncates the rest of the timeline by setting the
        # cursor to 1; the normal end-of-step decrement then reaches 0.
        self.count = 1

    def _player_attack_multiplier(self) -> int:
        if self.monster_index in (12, 13, 17) and self.slot(SLOT_CROSS) == 1:
            return 2
        if self.monster_index == 19 and self.slot(SLOT_DRAGON_SLAYER) == 1:
            return 2
        return 1

    def _apply_monster_delta(self, defender_attack: int, allow_multiplier: bool) -> None:
        diff = self.state[ST_OPTION_A]
        mult = self._player_attack_multiplier() if allow_multiplier else 1
        delta = self.monster.defense * (diff + 1) - defender_attack * mult
        if delta > 0:
            delta = 0
        self.monster_hp += delta
        if self.monster_hp < 0:
            self.monster_hp = 0
        self._record("monster_hp", value=self.monster_hp, delta=delta)

    # ------------------------------------------------------------------
    # execution

    def step(self) -> bool:
        cmd = self.current()
        if cmd is None:
            return False
        handler = getattr(self, f"_op_{cmd.opcode:02x}", None)
        if handler is None:
            self._record("unknown_opcode", opcode=cmd.opcode, arg1=cmd.arg1, arg2=cmd.arg2)
        else:
            handler(cmd)
        self.count -= 1
        return True

    def run(self, max_steps: int = 10000) -> int:
        steps = 0
        while self.count > 0 and steps < max_steps:
            if not self.step():
                break
            steps += 1
        if steps >= max_steps:
            self._record("step_limit", limit=max_steps)
        return steps

    # ------------------------------------------------------------------
    # opcodes

    def _op_00(self, cmd: Command) -> None:
        # No-op that consumes one stackwork invocation; used as a frame wait.
        self._record("nop", arg1=cmd.arg1, arg2=cmd.arg2)

    def _op_01(self, cmd: Command) -> None:
        self._record("sound", index=cmd.arg1, arg2=cmd.arg2)

    def _op_02(self, cmd: Command) -> None:
        if cmd.arg1:
            self.current_sprite = cmd.arg1
        self._record("sprite", index=cmd.arg1)

    def _op_03(self, cmd: Command) -> None:
        # Altar/stat window; draws the player at the current tile and calls
        # zyouout (0x449e18).
        self._record("altar", arg1=cmd.arg1, arg2=cmd.arg2)

    def _op_04(self, cmd: Command) -> None:
        diff = self.state[ST_OPTION_A]
        delta = self.state[ST_DEFENSE] - self.monster.attack * (diff + 1)
        if delta > 0:
            delta = 0
        self.state[ST_LIFE] += delta
        self._clamp_life()
        self._record("life", value=self.state[ST_LIFE], delta=delta)

    def _op_05(self, cmd: Command) -> None:
        self._apply_monster_delta(self.state[ST_ATTACK], allow_multiplier=True)

    def _op_06(self, cmd: Command) -> None:
        index = s16(cmd.arg1)
        self.set_tile(self.state[ST_FLOOR], index, cmd.arg2)
        self._record("set_tile", index=index, value=cmd.arg2, silent=False)

    def _op_07(self, cmd: Command) -> None:
        amount = s16(cmd.arg1)
        self.state[ST_GOLD] += amount * (self.slot(SLOT_LUCKY_GOLD) + 1)
        self._record("gold", value=self.state[ST_GOLD])

    def _op_08(self, cmd: Command) -> None:
        self._record("show_items")

    def _op_09(self, cmd: Command) -> None:
        prev = self.previous()
        self._record(
            "three_sprite_anim",
            current=(cmd.arg1, cmd.arg2),
            previous=(prev.arg1, prev.arg2) if prev else (0, 0),
        )

    def _op_0a(self, cmd: Command) -> None:
        self._record("start_subevent", index=cmd.arg1, selector=cmd.arg1 + 10)
        if self.subevent_handler:
            self.subevent_handler(self, cmd.arg1, cmd.arg2)

    def _op_0b(self, cmd: Command) -> None:
        sub = cmd.arg1
        if sub in (1, 2, 3, 5, 10):
            # Splice: the sub-handler's appended commands overwrite the
            # current 0xb command and become the next command(s).
            self.count -= 1
            if sub == 2:
                self.set_tile(self.state[ST_FLOOR], 0x1B, 0x5B)
            self._record("subdispatch", sub=sub, arg2=cmd.arg2)
            if self.subevent_handler:
                self.subevent_handler(self, sub, cmd.arg2)
            self.count += 1
            return

        if sub == 4:
            stage = cmd.arg2
            if self.state[ST_OPTION_A] == 1:
                stage = (stage * 5) // 4
            self._record("vampire", stage=stage)
        elif sub == 6:
            self.count -= 1
            if self.state[ST_OPTION_A] == 0:
                self.state[ST_OPTION_A] = 0x2B
            else:
                self.state[ST_OPTION_B] = 1
            self.gameover = True
            self._record("gameover", reason="subdispatch_6")
            if self.subevent_handler:
                self.subevent_handler(self, sub, cmd.arg2)
            self.count += 1
        elif sub == 7:
            self._record("title", variant="R" if self.state[ST_OPTION_A] else "normal")
        elif sub == 8:
            self._record("player_update")
        elif sub == 9:
            self._record("player_animation")
        elif sub == 0xB:
            floor = s16(cmd.arg2)
            self.state[ST_FLOOR] = floor
            self._record("change_floor", floor=floor)
            if floor == 50:
                self._record("floor50_special")
        elif sub == 0xC:
            prev = self.previous()
            phase = s16(prev.arg1) if prev else 0
            frame = s16(cmd.arg2)
            iv = opening9_interval_ms(self.speed, phase, frame)
            if iv:
                self.interval_ms = iv
            self._record("opening9", phase=phase, frame=frame, interval=iv)
        elif sub == 0xD:
            self._record("hide_message")
        elif sub == 0xE:
            self._record("hide_event_images")
        elif sub == 0xF:
            self._record("fade")
        else:
            self._record("subdispatch_unknown", sub=sub, arg2=cmd.arg2)

    def _op_0c(self, cmd: Command) -> None:
        # monidou (0x480384): map[arg2] = map[arg1] (0x480a70), then
        # map[arg1] = 6 (0x480a83).
        floor = self.state[ST_FLOOR]
        src, dst = s16(cmd.arg1), s16(cmd.arg2)
        raw = self.tile(floor, src)
        self.set_tile(floor, dst, raw)
        self.set_tile(floor, src, 0x06)
        self._record("monster_move", src=src, dst=dst, raw=raw)

    def _op_0d(self, cmd: Command) -> None:
        # dooropen (0x4770e0): for door kinds 1..12 set the Timer2 interval
        # to 8/10/12 ms, take the animation progress from the previous
        # command's arg2 (DAT_0048c558; 0x10 calls timerin) and write the
        # final tile at map[floor][arg1].
        index = s16(cmd.arg1)
        tile = door_tile(cmd.arg2)
        if tile is not None:
            self.interval_ms = DOOR_INTERVAL_MS.get(self.speed, 10)
            prev = self.previous()
            self.anim_progress = s16(prev.arg2) if prev else 0
            self.set_tile(self.state[ST_FLOOR], index, tile)
            if self.anim_progress == 0x10:
                self._timerin()
        self._record("door_open", index=index, door_kind=cmd.arg2, tile=tile,
                     progress=self.anim_progress)

    def _op_0e(self, cmd: Command) -> None:
        prev = self.previous()
        self._record(
            "battle_hit",
            current=(cmd.arg1, cmd.arg2),
            previous=(prev.arg1, prev.arg2) if prev else (0, 0),
        )
        self._apply_monster_delta(self.state[ST_ATTACK], allow_multiplier=False)

    def _op_0f(self, cmd: Command) -> None:
        # 0x454038: stop the media player, soundcheck (0x47c8f8) puts the
        # floor's BGM id in state[0x5a], soundplay (0x47c2bc) plays it.
        bgm = floor_bgm(self.state[ST_FLOOR])
        if bgm is not None:
            self.state[ST_BGM] = bgm
        self._record("stop_bgm", bgm=self.state[ST_BGM])

    def _op_10(self, cmd: Command) -> None:
        delta = s16(cmd.arg1)
        self.state[ST_LIFE] += delta
        self._clamp_life()
        self._record("life", value=self.state[ST_LIFE], delta=delta)
        if self.state[ST_LIFE] == 0:
            self._game_over_from_life()

    def _op_11(self, cmd: Command) -> None:
        index = s16(cmd.arg1)
        self.set_tile(self.state[ST_FLOOR], index, cmd.arg2)
        self._record("set_tile", index=index, value=cmd.arg2, silent=True)

    def _op_12(self, cmd: Command) -> None:
        # Jump-table entry 0x12 points at the same default path as opcode 0.
        self._record("nop", arg1=cmd.arg1, arg2=cmd.arg2)

    def _op_13(self, cmd: Command) -> None:
        self.state[ST_LIFE] >>= 1
        self._clamp_life()
        self._record("life", value=self.state[ST_LIFE], delta="halve")
        if self.state[ST_LIFE] == 0:
            self._game_over_from_life()

    def _op_14(self, cmd: Command) -> None:
        index = s16(cmd.arg1)
        value = s16(cmd.arg2)
        if 0 <= index < len(self.state):
            self.state[index] = value
            self._record("set_state", index=index, value=value)
        else:
            self._record("set_state_oob", index=index, value=value)

    def _op_15(self, cmd: Command) -> None:
        mode = s16(cmd.arg1)
        map_index = s16(cmd.arg2)
        self._record("draw_actor", mode=mode, map_index=map_index)
        prev = self.previous()
        if prev and prev.opcode == OP_DRAW_ACTOR and s16(prev.arg1) == mode:
            # The original consumes the preceding 0x15 in the same
            # stackwork invocation to draw a consecutive actor.
            self.count -= 1
            self._record("draw_actor", mode=s16(prev.arg1), map_index=s16(prev.arg2),
                         chained=True)
        if map_index == 0x79:
            self._timerin()

    def _timerin(self) -> None:
        # timerin (0x47f5a8): Timer2 back to the speed-menu interval,
        # undoing dooropen's 8/10/12 ms.
        self.interval_ms = SPEED_INTERVAL_MS.get(self.speed, 250)
        self._record("timerin")

    def _op_16(self, cmd: Command) -> None:
        self.state[ST_OPTION_A] = 0
        self.state[ST_OPTION_B] = 0
        self.gameover = True
        self._record("reset")


def timer2_tick(vm: EventVM) -> int:
    """Model one ``timer2timer`` invocation: at most two stackwork steps."""
    steps = 0
    if vm.count >= 1:
        vm.step()
        steps += 1
    if vm.count >= 1:
        vm.step()
        steps += 1
    return steps


def default_state() -> list[int]:
    state = [0] * STATE_INT_COUNT
    state[ST_LIFE] = 1000
    state[ST_ATTACK] = 100
    state[ST_DEFENSE] = 100
    state[ST_FLOOR] = 1
    state[ST_FLOOR_LIMIT] = 1
    state[ST_X] = 5
    state[ST_Y] = 10
    state[ST_WEAPON_LEVEL] = 5
    state[ST_SHIELD_LEVEL] = 5
    state[ST_FACING] = 4
    state[ST_MAGIC] = -4
    state[ST_VERSION] = 12
    return state


__all__ = [
    "Command", "EventVM", "Monster", "default_state", "s16", "timer2_tick",
    "OP_NOP", "OP_SOUND", "OP_SET_SPRITE", "OP_ALTAR", "OP_LIFE_DELTA",
    "OP_MONSTER_DELTA", "OP_SET_TILE", "OP_ADD_GOLD", "OP_SHOW_ITEMS",
    "OP_THREE_SPRITE_ANIM", "OP_START_SUBEVENT", "OP_SUBDISPATCH",
    "OP_MONSTER_MOVE", "OP_DOOR_OPEN", "OP_BATTLE_HIT", "OP_STOP_BGM",
    "OP_ADD_LIFE", "OP_SET_TILE_SILENT", "OP_NOP_12", "OP_HALVE_LIFE",
    "OP_SET_STATE", "OP_DRAW_ACTOR", "OP_RESET",
    "ST_LIFE", "ST_ATTACK", "ST_DEFENSE", "ST_GOLD", "ST_FLOOR",
    "ST_FLOOR_LIMIT", "ST_X", "ST_Y", "ST_KEY_YELLOW", "ST_KEY_RED",
    "ST_KEY_BLUE", "ST_SPECIAL_FIRST", "ST_SPECIAL_LAST",
    "ST_OPTION_A", "ST_OPTION_B", "ST_FACING", "ST_MAGIC", "ST_VERSION",
    "SLOT_CROSS", "SLOT_DRAGON_SLAYER", "SLOT_LUCKY_GOLD",
]
