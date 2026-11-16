#ifndef TSW_AUDIO_H
#define TSW_AUDIO_H

/* Sound output (SDL3_mixer): the original's WAV sounds and its MIDI tunes
 * pre-rendered to Ogg Vorbis by tools/convert_assets.py (data/audio/).
 * One track per original MediaPlayer, so a sound replayed restarts as the
 * original's did; the BGM loops (mediaplayer5onnotify replays it). */

#include <stdbool.h>

typedef struct Audio Audio;

/* NULL when no audio device or no data/audio: the game runs silent. */
Audio *audio_init(const char *data_dir);
void audio_shutdown(Audio *audio);
/* Op 0x01's sound index: 1..4 open/get/taisen/zeno.wav, 7..10
 * 3f/20f/kai/kai2.wav. */
void audio_play_sound(Audio *audio, int index);
/* soundplay's tune id 5..0x15, from its start, looping. */
void audio_play_bgm(Audio *audio, int id);
void audio_stop_bgm(Audio *audio);
/* The tune file soundplay (0x47c2bc) opens for `id` (lower case, no
 * extension), or NULL. */
const char *audio_bgm_name(int id);

#endif
