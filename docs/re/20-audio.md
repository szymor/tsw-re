# Sound and music

The original plays its sounds with `TMediaPlayer` (MCI): five WAV players and
one MIDI sequencer.  `syokidata2` opens the WAV players once:

| Player | Form offset | File | Op `0x01` index |
|---|---|---|---|
| `MediaPlayer1` | `+0x2d4` | `\wav\open.wav` (doors) | 1 |
| `MediaPlayer2` | `+0x460` | `\wav\get.wav` (items) | 2 |
| `MediaPlayer3` | `+0x464` | `\wav\taisen.wav` (battle) | 3 |
| `MediaPlayer4` | `+0x468` | `\wav\zeno.wav` (blows) | 4 |
| `MediaPlayer6` | `+0x46c` | opened per use: `3f`, `20f`, `kai`, `kai2.wav` | 7..10 |
| `MediaPlayer5` | `+0x2d8` | the tune (`dtSequencer`) | 5 play, 6 stop |

Op `0x01` (`0x452762`, jump table `0x45277a`): indices 1..4 and 7..10 play
only while `state[0x59]` (`0x4b87ec`) is 0; 5 (`soundplay`) and 6
(`MediaPlayer5.Stop`) act only while `state[0x5a]` (`0x4b87f0`) is not 0.

## The switches

* `wavon1Click` (`0x47c20c`), Options > "Wav Sound On": `state[0x59]`
  0 = sounds on (checked), 1 = off; option byte 7 the same.
* `BGMON1Click` (`0x47c258`), Options > "Background Music On": off stops
  the tune and clears `state[0x5a]` and option byte 6; on runs
  `soundcheck` + `soundplay`.  `state[0x5a]` is the tune playing, 0 when
  the music is off.

## The tunes

`soundcheck` (`0x47c8f8`) picks the floor's tune into `state[0x5a]`: floor 0
`0x15`, 1..10 `5`, 11..20 `6`, 21..30 `0x11`, 31..40 `8`, 41..49 `9`, 50
`10`.  Events set others (`0x0e` the death jingle, `0x10`, `7`, `0x13`,
`0x12` in `mevent`, `0x0b` the title).  `soundplay` (`0x47c2bc`) stops
`MediaPlayer5`, opens `\midi\<file>` and plays it from the start:

| Id | File | Id | File | Id | File |
|---:|---|---:|---|---:|---|
| 5 | `b_067xgw.mid` | 0xc | `a_085xgw.mid` | 0x13 | `b_117xgw.mid` |
| 6 | `b_058xgw.mid` | 0xd | `a_032xgw.mid` | 0x14 | `b_014xgw.mid` |
| 7 | `a_008xgw.mid` | 0xe | `a_046xgw.mid` | 0x15 | `A_027xgw.mid` |
| 8 | `a_118xgw.mid` | 0xf | `a_005xgw.mid` | | |
| 9 | `a_019xgw.mid` | 0x10 | `a_007xgw.mid` | | |
| 0xa | `b_018xgw.mid` | 0x11 | `b_110xgw.mid` | | |
| 0xb | `a_050xgw.mid` | 0x12 | `a_023xgw.mid` | | |

`mediaplayer5onnotify` (`0x482a80`) calls `soundplay` again when the tune
ends, so it loops.  `soundcheck` + `soundplay` also run on the stairs, on
landing with the Orb of Flying, after a load (`loadwork`), a new game
(`syokidata2`) and game over (`gameover`, with the menu item checked).  The
ending's phase 7 (`0x484693`) plays `b_014xgw.mid` with the music on.  The
tunes are XG files whose first bar holds only set-up messages, so each one
starts after about 3 seconds of silence (also on every loop).

## The remake

`tools/convert_assets.py` copies the WAVs and renders the tunes to Ogg
Vorbis (FluidSynth with a General MIDI SoundFont, ffmpeg); the game plays
those with SDL3_mixer (`src/audio.c`), one track per MediaPlayer.  The event
engine queues what the original would play, with the switches applied
(`event_engine_take_audio`); the app plays the queue each frame.  Until the
options file exists both switches are on (`--no-wav`, `--no-bgm`).
