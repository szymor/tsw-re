#include "audio.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PLAYER_WAV1, PLAYER_WAV2, PLAYER_WAV3, PLAYER_WAV4, PLAYER_WAV6,
       PLAYER_COUNT };

struct Audio {
    MIX_Mixer *mixer;
    MIX_Track *players[PLAYER_COUNT];   /* MediaPlayer1..4, 6 */
    MIX_Audio *sounds[11];              /* by op 0x01 index */
    MIX_Track *bgm;                     /* MediaPlayer5 */
    MIX_Audio *bgm_audio;
    int bgm_id;
    char dir[1024];
};

/* soundplay (0x47c2bc): state[0x5a] -> \midi\<name>.mid. */
const char *audio_bgm_name(int id)
{
    static const char *const NAMES[] = {
        [5] = "b_067xgw", [6] = "b_058xgw", [7] = "a_008xgw",
        [8] = "a_118xgw", [9] = "a_019xgw", [10] = "b_018xgw",
        [11] = "a_050xgw", [12] = "a_085xgw", [13] = "a_032xgw",
        [14] = "a_046xgw", [15] = "a_005xgw", [16] = "a_007xgw",
        [17] = "b_110xgw", [18] = "a_023xgw", [19] = "b_117xgw",
        [20] = "b_014xgw", [21] = "a_027xgw",
    };
    if (id < 5 || id > 21)
        return NULL;
    return NAMES[id];
}

/* Op 0x01's index -> the WAV its MediaPlayer holds (syokidata2 opens
 * MediaPlayer1..4; cases 7..10 open MediaPlayer6). */
static const char *sound_name(int index)
{
    switch (index) {
    case 1: return "open.wav";
    case 2: return "get.wav";
    case 3: return "taisen.wav";
    case 4: return "zeno.wav";
    case 7: return "3f.wav";
    case 8: return "20f.wav";
    case 9: return "kai.wav";
    case 10: return "kai2.wav";
    default: return NULL;
    }
}

static int sound_player(int index)
{
    return index >= 1 && index <= 4 ? PLAYER_WAV1 + index - 1 : PLAYER_WAV6;
}

Audio *audio_init(const char *data_dir)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return NULL;
    if (!MIX_Init()) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    Audio *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    snprintf(a->dir, sizeof(a->dir), "%s/audio", data_dir);
    a->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!a->mixer) {
        audio_shutdown(a);
        return NULL;
    }
    for (int i = 0; i < PLAYER_COUNT; i++)
        a->players[i] = MIX_CreateTrack(a->mixer);
    a->bgm = MIX_CreateTrack(a->mixer);
    int loaded = 0;
    for (int i = 1; i <= 10; i++) {
        const char *name = sound_name(i);
        if (!name)
            continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", a->dir, name);
        a->sounds[i] = MIX_LoadAudio(a->mixer, path, true);
        if (a->sounds[i])
            loaded++;
    }
    if (loaded == 0)
        SDL_Log("audio: no sounds in %s (run tools/convert_assets.py)",
                a->dir);
    a->bgm_id = 0;
    return a;
}

void audio_shutdown(Audio *a)
{
    if (!a)
        return;
    if (a->mixer) {
        MIX_DestroyMixer(a->mixer);     /* its tracks too */
        for (int i = 0; i < 11; i++)
            if (a->sounds[i])
                MIX_DestroyAudio(a->sounds[i]);
        if (a->bgm_audio)
            MIX_DestroyAudio(a->bgm_audio);
    }
    free(a);
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void audio_play_sound(Audio *a, int index)
{
    if (!a || index < 1 || index > 10 || !a->sounds[index])
        return;
    MIX_Track *t = a->players[sound_player(index)];
    if (!t)
        return;
    MIX_StopTrack(t, 0);
    MIX_SetTrackAudio(t, a->sounds[index]);
    MIX_PlayTrack(t, 0);
}

void audio_stop_bgm(Audio *a)
{
    if (!a || !a->bgm)
        return;
    MIX_StopTrack(a->bgm, 0);
    a->bgm_id = 0;
}

void audio_play_bgm(Audio *a, int id)
{
    if (!a || !a->bgm)
        return;
    const char *name = audio_bgm_name(id);
    if (!name)
        return;
    /* soundplay always starts the tune over (Stop, Open, Play), also on
     * the stairs between two floors with the same tune. */
    MIX_StopTrack(a->bgm, 0);
    if (a->bgm_audio) {
        MIX_SetTrackAudio(a->bgm, NULL);
        MIX_DestroyAudio(a->bgm_audio);
        a->bgm_audio = NULL;
    }
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s.ogg", a->dir, name);
    a->bgm_audio = MIX_LoadAudio(a->mixer, path, false);
    a->bgm_id = 0;
    if (!a->bgm_audio)
        return;
    MIX_SetTrackAudio(a->bgm, a->bgm_audio);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    MIX_PlayTrack(a->bgm, props);
    SDL_DestroyProperties(props);
    a->bgm_id = id;
}
