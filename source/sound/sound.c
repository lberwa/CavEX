/*
	Copyright (c) 2022 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef PLATFORM_WII
#include <ogc/audio.h>
#include <ogc/cache.h>
#include <asndlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#define __XSI_VISIBLE 600
#define __POSIX_VISIBLE 200112
#include <unistd.h>
#include <time.h>

#include "sound.h"
#include "config.h"
#include "game/game_state.h"

#include "network/server_comunication.h"
#include "platform/thread.h"
#include "sound/mp3/play_sound.h"

/* ---- SOUND DEBUG ---- Auf 1 setzen, neu kompilieren ---- */
#define SOUND_DEBUG 1
/* -------------------------------------------------------- */
#if SOUND_DEBUG
#include <stdarg.h>
static void _sdbg(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    debug_send(buf);
}
#define SDBG(...) _sdbg(__VA_ARGS__)
#else
#define SDBG(...) ((void)0)
#endif

typedef struct {
    u8 *data;
    u32 size;
} wav_t;

// ── Sound-Cache ───────────────────────────────────────────────────────────────
#define PCM_COUNT ((int)(pcm_orb) + 1)
#define CACHE_MAX (1400 * 1024)

typedef struct {
    u8  *data;
    u32  size;
    int  ref_count;
    u32  last_used;
    bool loading;
} snd_cache_t;

static snd_cache_t snd_cache[PCM_COUNT];
static u32         cache_used = 0;
static u32         frame_tick = 0;

#define MAX_VOICES 16
typedef struct {
    int voice;
    int sound_id;
    u8 *anon_data;
} voice_slot_t;

static voice_slot_t voices[MAX_VOICES];
static int          voices_n = 0;

typedef struct {
    bool  valid;
    bool  has_pos;
    float wx, wy, wz, vol_scale;
} pending_t;
static pending_t pending[PCM_COUNT];

typedef struct {
    int   sound_id;
    bool  has_pos;
    float wx, wy, wz, vol_scale;
} load_req_t;

typedef struct {
    int   sound_id;
    u8   *data;
    u32   size;
    bool  has_pos;
    float wx, wy, wz, vol_scale;
} load_res_t;

static struct thread         snd_loader_thread;
static struct thread_channel snd_req_chan;
static struct thread_channel snd_res_chan;

static float global_volume = 1.0f;
static enum mp3_sound bg_playlist[16];
static int bg_playlist_num = 0;
static bool music_run = false;

//--------------------------------
//paths
//--------------------------------
static wav_t load_file(const char *path) {
    wav_t w = {0};
    if(!path) return w;

    FILE *f = fopen(path, "rb");
    if(!f) {
        return w;
    }

    
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if(sz <= 0) { fclose(f); return w; }
    w.size = (size_t)sz;
    w.data = memalign(32, w.size);
    if(!w.data) { fclose(f); return w; }
    fread(w.data, 1, w.size, f);
    DCFlushRange(w.data, w.size);
    fclose(f);


    return w;
}

static const char* sound_get_pcm_path(enum pcm_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.sounds", "assets/sound/sound");
    if(!base) return NULL;

    switch(sound) {
        case pcm_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/click.pcm", base);
            return fullpath;

        case pcm_chest_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chestclosed.pcm", base);
            return fullpath;

        case pcm_chest_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chestopen.pcm", base);
            return fullpath;

        case pcm_door_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/door_close.pcm", base);
            return fullpath;

        case pcm_door_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/door_open.pcm", base);
            return fullpath;

        case pcm_drink:
            snprintf(fullpath, sizeof(fullpath), "%s/random/drink.pcm", base);
            return fullpath;

        case pcm_eat1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat1.pcm", base);
            return fullpath;

        case pcm_eat2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat2.pcm", base);
            return fullpath;

        case pcm_eat3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat3.pcm", base);
            return fullpath;

        case pcm_fuse:
            snprintf(fullpath, sizeof(fullpath), "%s/random/fuse.pcm", base);
            return fullpath;

        case pcm_enderman_portal:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/endermen/portal.pcm", base);
            return fullpath;

        case pcm_sheep_say2:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say2.pcm", base);
            return fullpath;

        case pcm_villager_idle2:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/villager/idle2.pcm", base);
            return fullpath;

        case pcm_zombie_say3:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/zombie/say3.pcm", base);
            return fullpath;

        case pcm_dig_sand1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/sand1.pcm", base);
            return fullpath;

        case pcm_dig_stone3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone3.pcm", base);
            return fullpath;

        case pcm_dig_wood2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood2.pcm", base);
            return fullpath;

        case pcm_mob_hit2:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit2.pcm", base);
            return fullpath;

        case pcm_cave1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave1.pcm", base);
            return fullpath;

        // tile
        case pcm_piston_out:
            snprintf(fullpath, sizeof(fullpath), "%s/tile/piston/out.pcm", base);
            return fullpath;
        case pcm_piston_in:
            snprintf(fullpath, sizeof(fullpath), "%s/tile/piston/in.pcm", base);
            return fullpath;

        // note
        case pcm_note_harp:
            snprintf(fullpath, sizeof(fullpath), "%s/note/harp.pcm", base);
            return fullpath;
        case pcm_note_pling:
            snprintf(fullpath, sizeof(fullpath), "%s/note/pling.pcm", base);
            return fullpath;
        case pcm_note_bass:
            snprintf(fullpath, sizeof(fullpath), "%s/note/bass.pcm", base);
            return fullpath;

        // damage
        case pcm_mob_hit1:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit1.pcm", base);
            return fullpath;
        case pcm_mob_hit3:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit3.pcm", base);
            return fullpath;
        case pcm_fall_big:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/fallbig.pcm", base);
            return fullpath;
        case pcm_fall_small:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/fallsmall.pcm", base);
            return fullpath;

        // dig
        case pcm_dig_grass1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass1.pcm", base);
            return fullpath;
        case pcm_dig_grass2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass2.pcm", base);
            return fullpath;
        case pcm_dig_grass3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass3.pcm", base);
            return fullpath;
        case pcm_dig_grass4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass4.pcm", base);
            return fullpath;
        case pcm_dig_cloth1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth1.pcm", base);
            return fullpath;
        case pcm_dig_cloth2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth2.pcm", base);
            return fullpath;
        case pcm_dig_cloth3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth3.pcm", base);
            return fullpath;
        case pcm_dig_cloth4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth4.pcm", base);
            return fullpath;
        case pcm_dig_gravel1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel1.pcm", base);
            return fullpath;
        case pcm_dig_gravel2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel2.pcm", base);
            return fullpath;
        case pcm_dig_gravel3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel3.pcm", base);
            return fullpath;
        case pcm_dig_gravel4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel4.pcm", base);
            return fullpath;
        case pcm_dig_wood1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood1.pcm", base);
            return fullpath;
        case pcm_dig_wood3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood3.pcm", base);
            return fullpath;
        case pcm_dig_wood4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood4.pcm", base);
            return fullpath;
        case pcm_dig_stone1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone1.pcm", base);
            return fullpath;
        case pcm_dig_stone2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone2.pcm", base);
            return fullpath;
        case pcm_dig_stone4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone4.pcm", base);
            return fullpath;

        // step
        case pcm_step_grass1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass1.pcm", base);
            return fullpath;
        case pcm_step_grass2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass2.pcm", base);
            return fullpath;
        case pcm_step_grass3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass3.pcm", base);
            return fullpath;
        case pcm_step_grass4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass4.pcm", base);
            return fullpath;
        case pcm_step_cloth1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth1.pcm", base);
            return fullpath;
        case pcm_step_cloth2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth2.pcm", base);
            return fullpath;
        case pcm_step_cloth3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth3.pcm", base);
            return fullpath;
        case pcm_step_cloth4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth4.pcm", base);
            return fullpath;
        case pcm_step_gravel1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel1.pcm", base);
            return fullpath;
        case pcm_step_gravel2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel2.pcm", base);
            return fullpath;
        case pcm_step_gravel3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel3.pcm", base);
            return fullpath;
        case pcm_step_gravel4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel4.pcm", base);
            return fullpath;
        case pcm_step_sand1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand1.pcm", base);
            return fullpath;
        case pcm_step_sand2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand2.pcm", base);
            return fullpath;
        case pcm_step_sand3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand3.pcm", base);
            return fullpath;
        case pcm_step_sand4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand4.pcm", base);
            return fullpath;
        case pcm_step_stone1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone1.pcm", base);
            return fullpath;
        case pcm_step_stone2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone2.pcm", base);
            return fullpath;
        case pcm_step_stone3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone3.pcm", base);
            return fullpath;
        case pcm_step_stone4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone4.pcm", base);
            return fullpath;
        case pcm_step_snow1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow1.pcm", base);
            return fullpath;
        case pcm_step_snow2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow2.pcm", base);
            return fullpath;
        case pcm_step_snow3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow3.pcm", base);
            return fullpath;
        case pcm_step_snow4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow4.pcm", base);
            return fullpath;
        case pcm_step_wood1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood1.pcm", base);
            return fullpath;
        case pcm_step_wood2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood2.pcm", base);
            return fullpath;
        case pcm_step_wood3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood3.pcm", base);
            return fullpath;
        case pcm_step_wood4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood4.pcm", base);
            return fullpath;

        // fire
        case pcm_fire:
            snprintf(fullpath, sizeof(fullpath), "%s/fire/fire.pcm", base);
            return fullpath;

        // mob
        case pcm_sheep_say1:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say1.pcm", base);
            return fullpath;
        case pcm_sheep_say3:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say3.pcm", base);
            return fullpath;
        case pcm_creeper_death:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/creeper/death.pcm", base);
            return fullpath;

        // portal
        case pcm_portal:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/portal.pcm", base);
            return fullpath;
        case pcm_portal_trigger:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/trigger.pcm", base);
            return fullpath;
        case pcm_portal_travel:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/travel.pcm", base);
            return fullpath;

        // cave
        case pcm_cave2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave2.pcm", base);
            return fullpath;
        case pcm_cave3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave3.pcm", base);
            return fullpath;
        case pcm_cave4:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave4.pcm", base);
            return fullpath;
        case pcm_cave5:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave5.pcm", base);
            return fullpath;
        case pcm_cave6:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave6.pcm", base);
            return fullpath;
        case pcm_cave7:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave7.pcm", base);
            return fullpath;
        case pcm_cave8:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave8.pcm", base);
            return fullpath;
        case pcm_cave9:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave9.pcm", base);
            return fullpath;
        case pcm_cave10:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave10.pcm", base);
            return fullpath;
        case pcm_cave11:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave11.pcm", base);
            return fullpath;
        case pcm_cave12:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave12.pcm", base);
            return fullpath;
        case pcm_cave13:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave13.pcm", base);
            return fullpath;

        // weather
        case pcm_rain1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain1.pcm", base);
            return fullpath;
        case pcm_rain2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain2.pcm", base);
            return fullpath;
        case pcm_rain3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain3.pcm", base);
            return fullpath;
        case pcm_rain4:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain4.pcm", base);
            return fullpath;
        case pcm_thunder1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder1.pcm", base);
            return fullpath;
        case pcm_thunder2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder2.pcm", base);
            return fullpath;
        case pcm_thunder3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder3.pcm", base);
            return fullpath;

        // liquid
        case pcm_splash:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/splash.pcm", base);
            return fullpath;
        case pcm_splash2:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/splash2.pcm", base);
            return fullpath;
        case pcm_water:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/water.pcm", base);
            return fullpath;

        // random
        case pcm_explode1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode1.pcm", base);
            return fullpath;
        case pcm_explode2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode2.pcm", base);
            return fullpath;
        case pcm_explode3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode3.pcm", base);
            return fullpath;
        case pcm_explode4:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode4.pcm", base);
            return fullpath;
        case pcm_bowhit1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit1.pcm", base);
            return fullpath;
        case pcm_bowhit2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit2.pcm", base);
            return fullpath;
        case pcm_bowhit3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit3.pcm", base);
            return fullpath;
        case pcm_bowhit4:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit4.pcm", base);
            return fullpath;
        case pcm_pop:
            snprintf(fullpath, sizeof(fullpath), "%s/random/pop.pcm", base);
            return fullpath;
        case pcm_levelup:
            snprintf(fullpath, sizeof(fullpath), "%s/random/levelup.pcm", base);
            return fullpath;
        case pcm_glass1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass1.pcm", base);
            return fullpath;
        case pcm_glass2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass2.pcm", base);
            return fullpath;
        case pcm_glass3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass3.pcm", base);
            return fullpath;
        case pcm_successful_hit:
            snprintf(fullpath, sizeof(fullpath), "%s/random/successful_hit.pcm", base);
            return fullpath;
        case pcm_classic_hurt:
            snprintf(fullpath, sizeof(fullpath), "%s/random/classic_hurt.pcm", base);
            return fullpath;
        case pcm_wood_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/wood_click.pcm", base);
            return fullpath;
        case pcm_fizz:
            snprintf(fullpath, sizeof(fullpath), "%s/random/fizz.pcm", base);
            return fullpath;
        case pcm_orb:
            snprintf(fullpath, sizeof(fullpath), "%s/random/orb.pcm", base);
            return fullpath;

        default:
            return NULL;
    }
}

static const char* sound_get_mp3_path(enum mp3_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.MP3", "assets/sound");
    if(!base) return NULL;

    switch(sound) {
        case mp3_bg1:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg1.mp3", base);
            return fullpath;
        case mp3_bg2:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg2.mp3", base);
            return fullpath;
        case mp3_bg3:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg3.mp3", base);
            return fullpath;
        case mp3_bg4:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg4.mp3", base);
            return fullpath;
        case mp3_bg5:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg5.mp3", base);
            return fullpath;
        case mp3_bg6:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg6.mp3", base);
            return fullpath;
        case mp3_bg7:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg7.mp3", base);
            return fullpath;
        case mp3_bg8:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg8.mp3", base);
            return fullpath;
        case mp3_bg9:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg9.mp3", base);
            return fullpath;
        case mp3_bg10:
            snprintf(fullpath, sizeof(fullpath), "%s/bg/bg10.mp3", base);
            return fullpath;
        default:
            return NULL;
    }
}

// ── Loader-Thread ─────────────────────────────────────────────────────────────
static void* snd_loader_worker(void *arg) {
    (void)arg;
    for (;;) {
        load_req_t *req = NULL;
        tchannel_receive(&snd_req_chan, (void**)&req, true);
        if (!req) break;

        const char *path = sound_get_pcm_path((enum pcm_sound)req->sound_id);
        wav_t w = {0};
        if (path) w = load_file(path);

        load_res_t *res = malloc(sizeof(load_res_t));
        if (res) {
            res->sound_id  = req->sound_id;
            res->data      = w.data;
            res->size      = w.size;
            res->has_pos   = req->has_pos;
            res->wx        = req->wx;
            res->wy        = req->wy;
            res->wz        = req->wz;
            res->vol_scale = req->vol_scale;
            tchannel_send(&snd_res_chan, res, true);
        } else {
            if (w.data) free(w.data);
        }
        free(req);
    }
    return NULL;
}

static void cache_evict(u32 needed) {
    while (cache_used + needed > CACHE_MAX) {
        int oldest = -1;
        u32 oldest_tick = (u32)-1;
        for (int i = 0; i < PCM_COUNT; i++) {
            if (snd_cache[i].data && snd_cache[i].ref_count == 0
                && !snd_cache[i].loading
                && snd_cache[i].last_used < oldest_tick) {
                oldest = i;
                oldest_tick = snd_cache[i].last_used;
            }
        }
        if (oldest < 0) break;
        cache_used -= snd_cache[oldest].size;
        free(snd_cache[oldest].data);
        snd_cache[oldest].data = NULL;
        snd_cache[oldest].size = 0;
        SDBG("[SND] evict id=%d total=%lu", oldest, (unsigned long)cache_used);
    }
}

void sound_init() {
    memset(snd_cache, 0, sizeof(snd_cache));
    memset(pending,   0, sizeof(pending));
    voices_n   = 0;
    cache_used = 0;
    frame_tick = 0;

    tchannel_init(&snd_req_chan, 32);
    tchannel_init(&snd_res_chan, 32);
    thread_create(&snd_loader_thread, snd_loader_worker, NULL, 64);

    soundhandler_init();
    ASND_Init();
    ASND_Pause(0);
    SND_Init(INIT_RATE_48000);
    SND_Pause(0);
}

/* ── Hintergrundmusik via SoundHandler (MP3-Streaming) ──────────────────── */
#define BG_MUSIC
#define BG_VOICE 0   /* ASND-Voice-Slot für BG-Musik */

static bool bg_pending = false;  /* Decoder geladen, warte auf ersten Puffer */
static int  bg_vol     = 200;    /* Lautstärke 0-255 */

static bool st_sound_play_bg(enum mp3_sound sound) {
    const char *path = sound_get_mp3_path(sound);
    if (!path) return false;
    soundhandler_stop(BG_VOICE);
    soundhandler_add_file(BG_VOICE, path);
    bg_pending = true;
    return true;
}

void sound_stop_bg() {
    soundhandler_stop(BG_VOICE);
    music_run  = false;
    bg_pending = false;
}

void sound_resume_bg() {
#ifdef BG_MUSIC
    if (music_run) return;
    music_run      = true;
    bg_playlist_num = 0;
    st_sound_play_bg(bg_playlist[0]);
#endif
}
#ifdef BG_MUSIC
void sound_set_volume_bg(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    global_volume = volume;
    bg_vol = (int)(volume * 255.0f);
}
#endif


static void sound_calc_spatial_wii(float sx, float sy, float sz, struct camera *cam,
                                    float *out_L, float *out_R);
static int  wii_vol_clamp(float v);
static void do_play_wii(int sound_id, u8 *data, u32 size,
                        bool has_pos, float wx, float wy, float wz,
                        float vol_scale, bool anon);

void sound_update() {
    frame_tick++;

    // Fertig geladene Sounds vom Loader-Thread abholen
    load_res_t *res = NULL;
    while (tchannel_receive(&snd_res_chan, (void**)&res, false)) {
        int id = res->sound_id;
        snd_cache[id].loading = false;

        if (res->data) {
            cache_evict(res->size);
            if (cache_used + res->size <= CACHE_MAX) {
                snd_cache[id].data      = res->data;
                snd_cache[id].size      = res->size;
                snd_cache[id].ref_count = 0;
                snd_cache[id].last_used = frame_tick;
                cache_used += res->size;

                if (pending[id].valid && voices_n < MAX_VOICES)
                    do_play_wii(id, res->data, res->size,
                                pending[id].has_pos,
                                pending[id].wx, pending[id].wy, pending[id].wz,
                                pending[id].vol_scale, false);
            } else {
                // Kein Platz im Cache → Sound einmalig ohne Caching abspielen
                if (pending[id].valid && voices_n < MAX_VOICES)
                    do_play_wii(id, res->data, res->size,
                                pending[id].has_pos,
                                pending[id].wx, pending[id].wy, pending[id].wz,
                                pending[id].vol_scale, true);
                else
                    free(res->data);
            }
        }
        pending[id].valid = false;
        free(res);
    }

    // Fertig abgespielte Stimmen bereinigen und ref_count dekrementieren
    for (int i = 0; i < voices_n; ) {
        if (!ASND_StatusVoice(voices[i].voice)) {
            int sid = voices[i].sound_id;
            if (sid >= 0 && sid < PCM_COUNT)
                snd_cache[sid].ref_count--;
            else if (voices[i].anon_data)
                free(voices[i].anon_data);
            voices[i] = voices[--voices_n];
        } else {
            i++;
        }
    }

    if (music_run) {
        if (bg_pending) {
            /* Noch nicht fertig dekodiert — jeden Frame prüfen */
            if (soundhandler_play_if_ready(BG_VOICE, bg_vol, bg_vol))
                bg_pending = false;
        } else if (!soundhandler_is_playing(BG_VOICE)) {
            /* Track zu Ende — nächsten in der Playlist starten */
            bg_playlist_num++;
            if (bg_playlist_num >= 16)
                bg_playlist_num = 0;
            st_sound_play_bg(bg_playlist[bg_playlist_num]);
        }
    }
}


bool sound_play_bg(enum mp3_sound sound[16]) {
#ifdef BG_MUSIC
    if (!sound) return false;
    music_run = true;
    for (int i = 0; i < 16; i++)
        bg_playlist[i] = sound[i];
    return true;
#else
    return false;
#endif
}

static void sound_calc_spatial_wii(float sx, float sy, float sz, struct camera *cam,
                                    float *out_L, float *out_R) {
    float dx = sx - cam->x;
    float dy = sy - cam->y;
    float dz = sz - cam->z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);

    float vol = 1.0f - dist / 16.0f;
    if(vol < 0.0f) vol = 0.0f;

    float pan = 0.0f;
    float hd = sqrtf(dx*dx + dz*dz);
    if(hd > 0.001f) {
        float rx = cosf(cam->rx);
        float rz = -sinf(cam->rx);
        pan = (dx * rx + dz * rz) / hd;
        if(pan >  1.0f) pan =  1.0f;
        if(pan < -1.0f) pan = -1.0f;
    }

    float fL = 1.0f - pan; if(fL > 1.0f) fL = 1.0f; if(fL < 0.0f) fL = 0.0f;
    float fR = 1.0f + pan; if(fR > 1.0f) fR = 1.0f; if(fR < 0.0f) fR = 0.0f;
    *out_L = vol * fL;
    *out_R = vol * fR;
}

static int wii_vol_clamp(float v) {
    int i = (int)(v * 255.0f);
    if(i < 0)   i = 0;
    if(i > 255) i = 255;
    return i;
}

// Interne Hilfsfunktion: Stimme starten (Puffer muss bereits im Speicher sein)
static void do_play_wii(int sound_id, u8 *data, u32 size,
                        bool has_pos, float wx, float wy, float wz,
                        float vol_scale, bool anon) {
    if (voices_n >= MAX_VOICES) return;

    float vol_L = 1.0f, vol_R = 1.0f;
    if (has_pos) {
        vol_L = 0.0f; vol_R = 0.0f;
        int actual = 0;
#ifdef SPLITSCREEN
        int cnt = splitscreen_player_count();
        for (int i = 0; i < cnt; i++) {
            if (!gstate.local_players[i]) continue;
            struct camera *cam = (i == gstate.active_player)
                ? &gstate.camera : &gstate.cameras[i];
            float pL, pR;
            sound_calc_spatial_wii(wx, wy, wz, cam, &pL, &pR);
            vol_L += pL; vol_R += pR; actual++;
        }
        if (actual > 0) { vol_L /= actual; vol_R /= actual; }
        else            { vol_L = 1.0f;    vol_R = 1.0f;    }
#else
        sound_calc_spatial_wii(wx, wy, wz, &gstate.camera, &vol_L, &vol_R);
#endif
        float mv = gstate.settings.max_volume * vol_scale;
        if (mv > 1.0f) mv = 1.0f;
        vol_L *= mv; vol_R *= mv;
    } else {
        float mv = gstate.settings.max_volume;
        if (mv > 1.0f) mv = 1.0f;
        vol_L = mv; vol_R = mv;
    }

    int voice = SND_GetFirstUnusedVoice();
    voices[voices_n].voice     = voice;
    voices[voices_n].sound_id  = anon ? -1 : sound_id;
    voices[voices_n].anon_data = anon ? data : NULL;
    voices_n++;

    if (!anon && sound_id >= 0 && sound_id < PCM_COUNT) {
        snd_cache[sound_id].ref_count++;
        snd_cache[sound_id].last_used = frame_tick;
    }

    SDBG("[SND] play id=%d anon=%d L=%d R=%d", sound_id, (int)anon,
         wii_vol_clamp(vol_L), wii_vol_clamp(vol_R));
    SND_SetVoice(voice, VOICE_STEREO_16BIT_LE, 44100, 0,
                 data, size,
                 wii_vol_clamp(vol_L), wii_vol_clamp(vol_R), NULL);
}

bool sound_play(enum pcm_sound sound) {
    int id = (int)sound;
    if (id < 0 || id >= PCM_COUNT || voices_n >= MAX_VOICES) return false;

    if (snd_cache[id].data) {
        do_play_wii(id, snd_cache[id].data, snd_cache[id].size,
                    false, 0, 0, 0, 1.0f, false);
        return true;
    }
    if (snd_cache[id].loading) {
        pending[id].valid     = true;
        pending[id].has_pos   = false;
        pending[id].vol_scale = 1.0f;
        return true;
    }
    load_req_t *req = malloc(sizeof(load_req_t));
    if (!req) return false;
    req->sound_id  = id;
    req->has_pos   = false;
    req->vol_scale = 1.0f;
    req->wx = req->wy = req->wz = 0.0f;
    snd_cache[id].loading = true;
    pending[id].valid     = true;
    pending[id].has_pos   = false;
    pending[id].vol_scale = 1.0f;
    if (!tchannel_send(&snd_req_chan, req, false)) {
        free(req);
        snd_cache[id].loading = false;
        pending[id].valid = false;
        return false;
    }
    return true;
}

bool sound_play_at_vol(enum pcm_sound sound, float wx, float wy, float wz,
                       float vol_scale) {
    int id = (int)sound;
    if (id < 0 || id >= PCM_COUNT || voices_n >= MAX_VOICES) return false;

    if (snd_cache[id].data) {
        do_play_wii(id, snd_cache[id].data, snd_cache[id].size,
                    true, wx, wy, wz, vol_scale, false);
        return true;
    }
    if (snd_cache[id].loading) {
        pending[id].valid     = true;
        pending[id].has_pos   = true;
        pending[id].wx        = wx;
        pending[id].wy        = wy;
        pending[id].wz        = wz;
        pending[id].vol_scale = vol_scale;
        return true;
    }
    load_req_t *req = malloc(sizeof(load_req_t));
    if (!req) return false;
    req->sound_id  = id;
    req->has_pos   = true;
    req->wx = wx; req->wy = wy; req->wz = wz;
    req->vol_scale = vol_scale;
    snd_cache[id].loading = true;
    pending[id].valid     = true;
    pending[id].has_pos   = true;
    pending[id].wx        = wx;
    pending[id].wy        = wy;
    pending[id].wz        = wz;
    pending[id].vol_scale = vol_scale;
    if (!tchannel_send(&snd_req_chan, req, false)) {
        free(req);
        snd_cache[id].loading = false;
        pending[id].valid = false;
        return false;
    }
    return true;
}

bool sound_play_at(enum pcm_sound sound, float wx, float wy, float wz) {
    return sound_play_at_vol(sound, wx, wy, wz, 1.0f);
}


#endif

#ifdef PLATFORM_PC
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include "pc_sound/include/portaudio.h"
#include "sound.h"
#include "config.h"
#include "game/game_state.h"
#include <mpg123.h>

#define MAX_PCM_PLAYLIST 16
#define OUT_CHANNELS 2      // Stream ist stereo (siehe Pa_OpenDefaultStream)

typedef struct {
    uint8_t *data;
    size_t size;
    size_t pos;
    int channels;
    int sample_rate;
    float vol_left;
    float vol_right;
} wav_t;

// PCM playlist
static wav_t pcm_playlist[MAX_PCM_PLAYLIST];
static int pcm_playlist_num = 0;

// Schuetzt pcm_playlist: der PortAudio-Callback laeuft in einem eigenen Thread,
// sound_play()/sound_update() im Main-Thread.
static pthread_mutex_t pcm_mutex = PTHREAD_MUTEX_INITIALIZER;

static float global_volume = 1.0f;

// PortAudio stream
static PaStream *pa_stream = NULL;

// Hintergrundmusik (PC)
static enum mp3_sound bg_playlist[16];
static int  bg_playlist_num = 0;
static bool music_run = false;
static wav_t bg_wav = {0};
static float bg_volume = 1.0f;
static pthread_mutex_t bg_mutex = PTHREAD_MUTEX_INITIALIZER;

//----------------------
// WAV Loader
//----------------------
static wav_t load_wav_file(const char *path) {
    wav_t w = {0};

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open WAV file: %s\n", path);
        return w;
    }

    // RIFF/WAVE-Kopf pruefen
    unsigned char riff[12];
    if (fread(riff, 1, 12, f) != 12
        || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a RIFF/WAVE file: %s\n", path);
        fclose(f);
        return w;
    }

    int channels = 2, sample_rate = 44100, bits = 16;

    // Chunks der Reihe nach durchgehen und gezielt "fmt " und "data" lesen.
    // WICHTIG: es koennen andere Chunks (z.B. LIST/INFO) VOR "data" liegen --
    // deshalb NICHT einfach 44 Bytes ueberspringen.
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8)
            break;
        uint32_t csize = ch[4] | (ch[5] << 8) | (ch[6] << 16)
                         | ((uint32_t)ch[7] << 24);

        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            uint32_t n = csize < 16 ? csize : 16;
            if (fread(fmt, 1, n, f) != n)
                break;
            channels = fmt[2] | (fmt[3] << 8);
            sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16)
                          | ((uint32_t)fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            // Rest des fmt-Chunks + evtl. Pad-Byte ueberspringen
            if (csize > n)
                fseek(f, (long)(csize - n), SEEK_CUR);
            if (csize & 1)
                fseek(f, 1, SEEK_CUR);
        } else if (memcmp(ch, "data", 4) == 0) {
            w.data = malloc(csize);
            if (!w.data) {
                fclose(f);
                return w;
            }
            w.size = fread(w.data, 1, csize, f);
            break;
        } else {
            // unbekannter Chunk -> ueberspringen (+ Pad-Byte bei ungerader Groesse)
            fseek(f, (long)(csize + (csize & 1)), SEEK_CUR);
        }
    }
    fclose(f);

    w.pos = 0;
    w.channels = channels;
    w.sample_rate = sample_rate;

    // Der Callback erwartet 16-bit-PCM. Andere Formate lieber ablehnen als
    // Rauschen abspielen. (Das Konvertierungsskript erzeugt pcm_s16le/stereo/44100.)
    if (bits != 16 || !w.data) {
        if (bits != 16)
            fprintf(stderr, "WAV %s: erwartet 16-bit, ist %d-bit\n", path, bits);
        free(w.data);
        w.data = NULL;
        w.size = 0;
    }
    return w;
}

//----------------------
// Sound Pfade
//----------------------
static const char* sound_get_pcm_path(enum pcm_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.sounds", "assets/sound/sound");
    if(!base) return NULL;

    switch(sound) {
        case pcm_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/click.wav", base);
            return fullpath;

        case pcm_chest_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chestclosed.wav", base);
            return fullpath;

        case pcm_chest_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chestopen.wav", base);
            return fullpath;

        case pcm_door_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/door_close.wav", base);
            return fullpath;

        case pcm_door_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/door_open.wav", base);
            return fullpath;

        case pcm_drink:
            snprintf(fullpath, sizeof(fullpath), "%s/random/drink.wav", base);
            return fullpath;

        case pcm_eat1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat1.wav", base);
            return fullpath;

        case pcm_eat2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat2.wav", base);
            return fullpath;

        case pcm_eat3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/eat3.wav", base);
            return fullpath;

        case pcm_fuse:
            snprintf(fullpath, sizeof(fullpath), "%s/random/fuse.wav", base);
            return fullpath;

        case pcm_enderman_portal:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/endermen/portal.wav", base);
            return fullpath;

        case pcm_sheep_say2:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say2.wav", base);
            return fullpath;

        case pcm_villager_idle2:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/villager/idle2.wav", base);
            return fullpath;

        case pcm_zombie_say3:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/zombie/say3.wav", base);
            return fullpath;

        case pcm_dig_sand1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/sand1.wav", base);
            return fullpath;

        case pcm_dig_stone3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone3.wav", base);
            return fullpath;

        case pcm_dig_wood2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood2.wav", base);
            return fullpath;

        case pcm_mob_hit2:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit2.wav", base);
            return fullpath;

        case pcm_cave1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave1.wav", base);
            return fullpath;

        // tile
        case pcm_piston_out:
            snprintf(fullpath, sizeof(fullpath), "%s/tile/piston/out.wav", base);
            return fullpath;
        case pcm_piston_in:
            snprintf(fullpath, sizeof(fullpath), "%s/tile/piston/in.wav", base);
            return fullpath;

        // note
        case pcm_note_harp:
            snprintf(fullpath, sizeof(fullpath), "%s/note/harp.wav", base);
            return fullpath;
        case pcm_note_pling:
            snprintf(fullpath, sizeof(fullpath), "%s/note/pling.wav", base);
            return fullpath;
        case pcm_note_bass:
            snprintf(fullpath, sizeof(fullpath), "%s/note/bass.wav", base);
            return fullpath;

        // damage
        case pcm_mob_hit1:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit1.wav", base);
            return fullpath;
        case pcm_mob_hit3:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/hit3.wav", base);
            return fullpath;
        case pcm_fall_big:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/fallbig.wav", base);
            return fullpath;
        case pcm_fall_small:
            snprintf(fullpath, sizeof(fullpath), "%s/damage/fallsmall.wav", base);
            return fullpath;

        // dig
        case pcm_dig_grass1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass1.wav", base);
            return fullpath;
        case pcm_dig_grass2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass2.wav", base);
            return fullpath;
        case pcm_dig_grass3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass3.wav", base);
            return fullpath;
        case pcm_dig_grass4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/grass4.wav", base);
            return fullpath;
        case pcm_dig_cloth1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth1.wav", base);
            return fullpath;
        case pcm_dig_cloth2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth2.wav", base);
            return fullpath;
        case pcm_dig_cloth3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth3.wav", base);
            return fullpath;
        case pcm_dig_cloth4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/cloth4.wav", base);
            return fullpath;
        case pcm_dig_gravel1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel1.wav", base);
            return fullpath;
        case pcm_dig_gravel2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel2.wav", base);
            return fullpath;
        case pcm_dig_gravel3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel3.wav", base);
            return fullpath;
        case pcm_dig_gravel4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/gravel4.wav", base);
            return fullpath;
        case pcm_dig_wood1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood1.wav", base);
            return fullpath;
        case pcm_dig_wood3:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood3.wav", base);
            return fullpath;
        case pcm_dig_wood4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/wood4.wav", base);
            return fullpath;
        case pcm_dig_stone1:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone1.wav", base);
            return fullpath;
        case pcm_dig_stone2:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone2.wav", base);
            return fullpath;
        case pcm_dig_stone4:
            snprintf(fullpath, sizeof(fullpath), "%s/dig/stone4.wav", base);
            return fullpath;

        // step
        case pcm_step_grass1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass1.wav", base);
            return fullpath;
        case pcm_step_grass2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass2.wav", base);
            return fullpath;
        case pcm_step_grass3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass3.wav", base);
            return fullpath;
        case pcm_step_grass4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/grass4.wav", base);
            return fullpath;
        case pcm_step_cloth1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth1.wav", base);
            return fullpath;
        case pcm_step_cloth2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth2.wav", base);
            return fullpath;
        case pcm_step_cloth3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth3.wav", base);
            return fullpath;
        case pcm_step_cloth4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/cloth4.wav", base);
            return fullpath;
        case pcm_step_gravel1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel1.wav", base);
            return fullpath;
        case pcm_step_gravel2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel2.wav", base);
            return fullpath;
        case pcm_step_gravel3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel3.wav", base);
            return fullpath;
        case pcm_step_gravel4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/gravel4.wav", base);
            return fullpath;
        case pcm_step_sand1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand1.wav", base);
            return fullpath;
        case pcm_step_sand2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand2.wav", base);
            return fullpath;
        case pcm_step_sand3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand3.wav", base);
            return fullpath;
        case pcm_step_sand4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/sand4.wav", base);
            return fullpath;
        case pcm_step_stone1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone1.wav", base);
            return fullpath;
        case pcm_step_stone2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone2.wav", base);
            return fullpath;
        case pcm_step_stone3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone3.wav", base);
            return fullpath;
        case pcm_step_stone4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/stone4.wav", base);
            return fullpath;
        case pcm_step_snow1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow1.wav", base);
            return fullpath;
        case pcm_step_snow2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow2.wav", base);
            return fullpath;
        case pcm_step_snow3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow3.wav", base);
            return fullpath;
        case pcm_step_snow4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/snow4.wav", base);
            return fullpath;
        case pcm_step_wood1:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood1.wav", base);
            return fullpath;
        case pcm_step_wood2:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood2.wav", base);
            return fullpath;
        case pcm_step_wood3:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood3.wav", base);
            return fullpath;
        case pcm_step_wood4:
            snprintf(fullpath, sizeof(fullpath), "%s/step/wood4.wav", base);
            return fullpath;

        // fire
        case pcm_fire:
            snprintf(fullpath, sizeof(fullpath), "%s/fire/fire.wav", base);
            return fullpath;

        // mob
        case pcm_sheep_say1:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say1.wav", base);
            return fullpath;
        case pcm_sheep_say3:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/sheep/say3.wav", base);
            return fullpath;
        case pcm_creeper_death:
            snprintf(fullpath, sizeof(fullpath), "%s/mob/creeper/death.wav", base);
            return fullpath;

        // portal
        case pcm_portal:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/portal.wav", base);
            return fullpath;
        case pcm_portal_trigger:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/trigger.wav", base);
            return fullpath;
        case pcm_portal_travel:
            snprintf(fullpath, sizeof(fullpath), "%s/portal/travel.wav", base);
            return fullpath;

        // cave
        case pcm_cave2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave2.wav", base);
            return fullpath;
        case pcm_cave3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave3.wav", base);
            return fullpath;
        case pcm_cave4:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave4.wav", base);
            return fullpath;
        case pcm_cave5:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave5.wav", base);
            return fullpath;
        case pcm_cave6:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave6.wav", base);
            return fullpath;
        case pcm_cave7:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave7.wav", base);
            return fullpath;
        case pcm_cave8:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave8.wav", base);
            return fullpath;
        case pcm_cave9:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave9.wav", base);
            return fullpath;
        case pcm_cave10:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave10.wav", base);
            return fullpath;
        case pcm_cave11:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave11.wav", base);
            return fullpath;
        case pcm_cave12:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave12.wav", base);
            return fullpath;
        case pcm_cave13:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/cave/cave13.wav", base);
            return fullpath;

        // weather
        case pcm_rain1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain1.wav", base);
            return fullpath;
        case pcm_rain2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain2.wav", base);
            return fullpath;
        case pcm_rain3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain3.wav", base);
            return fullpath;
        case pcm_rain4:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/rain4.wav", base);
            return fullpath;
        case pcm_thunder1:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder1.wav", base);
            return fullpath;
        case pcm_thunder2:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder2.wav", base);
            return fullpath;
        case pcm_thunder3:
            snprintf(fullpath, sizeof(fullpath), "%s/ambient/weather/thunder3.wav", base);
            return fullpath;

        // liquid
        case pcm_splash:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/splash.wav", base);
            return fullpath;
        case pcm_splash2:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/splash2.wav", base);
            return fullpath;
        case pcm_water:
            snprintf(fullpath, sizeof(fullpath), "%s/liquid/water.wav", base);
            return fullpath;

        // random
        case pcm_explode1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode1.wav", base);
            return fullpath;
        case pcm_explode2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode2.wav", base);
            return fullpath;
        case pcm_explode3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode3.wav", base);
            return fullpath;
        case pcm_explode4:
            snprintf(fullpath, sizeof(fullpath), "%s/random/explode4.wav", base);
            return fullpath;
        case pcm_bowhit1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit1.wav", base);
            return fullpath;
        case pcm_bowhit2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit2.wav", base);
            return fullpath;
        case pcm_bowhit3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit3.wav", base);
            return fullpath;
        case pcm_bowhit4:
            snprintf(fullpath, sizeof(fullpath), "%s/random/bowhit4.wav", base);
            return fullpath;
        case pcm_pop:
            snprintf(fullpath, sizeof(fullpath), "%s/random/pop.wav", base);
            return fullpath;
        case pcm_levelup:
            snprintf(fullpath, sizeof(fullpath), "%s/random/levelup.wav", base);
            return fullpath;
        case pcm_glass1:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass1.wav", base);
            return fullpath;
        case pcm_glass2:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass2.wav", base);
            return fullpath;
        case pcm_glass3:
            snprintf(fullpath, sizeof(fullpath), "%s/random/glass3.wav", base);
            return fullpath;
        case pcm_successful_hit:
            snprintf(fullpath, sizeof(fullpath), "%s/random/successful_hit.wav", base);
            return fullpath;
        case pcm_classic_hurt:
            snprintf(fullpath, sizeof(fullpath), "%s/random/classic_hurt.wav", base);
            return fullpath;
        case pcm_wood_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/wood_click.wav", base);
            return fullpath;
        case pcm_fizz:
            snprintf(fullpath, sizeof(fullpath), "%s/random/fizz.wav", base);
            return fullpath;
        case pcm_orb:
            snprintf(fullpath, sizeof(fullpath), "%s/random/orb.wav", base);
            return fullpath;

        default:
            return NULL;
    }
}


static const char* sound_get_bg_mp3_path(enum mp3_sound sound) {
    static char fullpath[256];
    const char* base = config_read_string(&gstate.config_user, "paths.MP3", "assets/sound");
    if (!base) return NULL;
    int n = (int)sound + 1; // mp3_bg1 → bg1.mp3, ...
    snprintf(fullpath, sizeof(fullpath), "%s/bg/bg%d.mp3", base, n);
    return fullpath;
}

static wav_t load_mp3_file(const char *path) {
    wav_t w = {0};
    if (!path) return w;

    mpg123_handle *mh = mpg123_new(NULL, NULL);
    if (!mh) return w;

    if (mpg123_open(mh, path) != MPG123_OK) {
        mpg123_delete(mh);
        return w;
    }

    long rate;
    int channels, encoding;
    if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
        mpg123_close(mh);
        mpg123_delete(mh);
        return w;
    }

    // Immer als signed 16-bit ausgeben (passt zum PortAudio-Callback)
    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    size_t buf_size = mpg123_outblock(mh);
    uint8_t *buf = malloc(buf_size);
    if (!buf) {
        mpg123_close(mh);
        mpg123_delete(mh);
        return w;
    }

    size_t capacity = buf_size * 16;
    uint8_t *pcm = malloc(capacity);
    if (!pcm) {
        free(buf);
        mpg123_close(mh);
        mpg123_delete(mh);
        return w;
    }
    size_t total = 0;

    size_t done;
    int err;
    while ((err = mpg123_read(mh, buf, buf_size, &done)) == MPG123_OK || done > 0) {
        if (total + done > capacity) {
            capacity = (total + done) * 2;
            uint8_t *tmp = realloc(pcm, capacity);
            if (!tmp) { free(pcm); pcm = NULL; break; }
            pcm = tmp;
        }
        memcpy(pcm + total, buf, done);
        total += done;
        if (err != MPG123_OK) break;
    }

    free(buf);
    mpg123_close(mh);
    mpg123_delete(mh);

    if (!pcm) return w;

    w.data        = pcm;
    w.size        = total;
    w.pos         = 0;
    w.channels    = channels;
    w.sample_rate = (int)rate;
    return w;
}

//----------------------
// PortAudio Callback
//----------------------
static int pa_callback(const void *inputBuffer, void *outputBuffer,
                       unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags,
                       void *userData) {
    float *out = (float*)outputBuffer;
    (void)inputBuffer;
    (void)timeInfo;
    (void)statusFlags;
    (void)userData;

    unsigned long total = framesPerBuffer * OUT_CHANNELS;

    // Immer zuerst mit Stille fuellen -- sonst spielt PortAudio bei leerer
    // Playlist den uninitialisierten Puffer ab (Dauerrauschen).
    for (unsigned long i = 0; i < total; i++)
        out[i] = 0.0f;

    pthread_mutex_lock(&pcm_mutex);
    for (int i = 0; i < pcm_playlist_num; i++) {
        wav_t *w = &pcm_playlist[i];
        if (!w->data || w->channels != OUT_CHANNELS)
            continue; // nur 16-bit-Stereo (siehe load_wav_file)

        const int16_t *data16 = (const int16_t*)w->data;
        size_t total_samples = w->size / sizeof(int16_t);
        size_t s = w->pos / sizeof(int16_t); // aktueller Sample-Index (alle Kanaele)

        // Ab aktueller Position ins Ausgabe-Frame mischen (additiv + clampen).
        for (unsigned long j = 0; j < total && s < total_samples; j++, s++) {
            float gain = (j % 2 == 0) ? w->vol_left : w->vol_right;
            float v = out[j] + (data16[s] / 32768.0f) * gain;
            if (v > 1.0f) v = 1.0f;
            else if (v < -1.0f) v = -1.0f;
            out[j] = v;
        }
        w->pos = s * sizeof(int16_t); // Position fuer naechsten Callback merken
    }
    pthread_mutex_unlock(&pcm_mutex);

    pthread_mutex_lock(&bg_mutex);
    if (bg_wav.data && bg_wav.channels == OUT_CHANNELS) {
        const int16_t *data16 = (const int16_t*)bg_wav.data;
        size_t total_samples = bg_wav.size / sizeof(int16_t);
        size_t s = bg_wav.pos / sizeof(int16_t);
        for (unsigned long j = 0; j < total && s < total_samples; j++, s++) {
            float gain = (j % 2 == 0) ? bg_wav.vol_left : bg_wav.vol_right;
            float v = out[j] + (data16[s] / 32768.0f) * gain;
            if (v > 1.0f) v = 1.0f;
            else if (v < -1.0f) v = -1.0f;
            out[j] = v;
        }
        bg_wav.pos = s * sizeof(int16_t);
    }
    pthread_mutex_unlock(&bg_mutex);

    return paContinue;
}

//----------------------
// Init / Cleanup
//----------------------
void sound_init(void) {
    // Schon initialisiert? Sonst wuerde ein zweiter Stream auf demselben
    // Geraet geoeffnet -> zwei Callbacks, Chaos.
    if (pa_stream)
        return;

    mpg123_init();

    // PortAudio/ALSA/JACK/BlueALSA schreiben beim Initialisieren jede Menge
    // Backend-Gemecker direkt nach stderr ("unable to open slave", "jack server
    // is not running", ...). Waehrend der Init stderr temporaer nach /dev/null
    // umleiten und danach wiederherstellen -> Konsole bleibt sauber.
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull != -1) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    PaError err = Pa_Initialize();
    if (err == paNoError) {
        err = Pa_OpenDefaultStream(&pa_stream,
                                   0,          // input channels
                                   2,          // output channels
                                   paFloat32,  // 32-bit float output
                                   44100,      // sample rate
                                   256,        // frames per buffer
                                   pa_callback,
                                   NULL);
        if (err == paNoError)
            Pa_StartStream(pa_stream);
    }

    // stderr wiederherstellen (VOR eventuellen Fehlermeldungen)
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }

    if (err != paNoError) {
        fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
        pa_stream = NULL;
        return;
    }

    pcm_playlist_num = 0;
}

void sound_shutdown(void) {
    if (pa_stream) {
        Pa_StopStream(pa_stream);
        Pa_CloseStream(pa_stream);
        pa_stream = NULL;
    }
    Pa_Terminate();

    for (int i = 0; i < pcm_playlist_num; i++) {
        free(pcm_playlist[i].data);
    }
    pcm_playlist_num = 0;

    pthread_mutex_lock(&bg_mutex);
    free(bg_wav.data);
    bg_wav.data = NULL;
    pthread_mutex_unlock(&bg_mutex);

    mpg123_exit();
}

//----------------------
// Volume
//----------------------
void sound_set_volume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    global_volume = volume;
}

//----------------------
// PCM Playback
//----------------------
#define SOUND_MAX_DIST 16.0f

static void sound_calc_spatial(float sx, float sy, float sz, struct camera *cam,
                                float *out_L, float *out_R) {
    float dx = sx - cam->x;
    float dy = sy - cam->y;
    float dz = sz - cam->z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);

    float vol = 1.0f - dist / SOUND_MAX_DIST;
    if(vol < 0.0f) vol = 0.0f;

    float pan = 0.0f;
    float hd = sqrtf(dx*dx + dz*dz);
    if(hd > 0.001f) {
        float rx = cosf(cam->rx);
        float rz = -sinf(cam->rx);
        pan = (dx * rx + dz * rz) / hd;
        if(pan >  1.0f) pan =  1.0f;
        if(pan < -1.0f) pan = -1.0f;
    }

    float fL = 1.0f - pan; if(fL > 1.0f) fL = 1.0f; if(fL < 0.0f) fL = 0.0f;
    float fR = 1.0f + pan; if(fR > 1.0f) fR = 1.0f; if(fR < 0.0f) fR = 0.0f;
    *out_L = vol * fL;
    *out_R = vol * fR;
}

bool sound_play(enum pcm_sound sound) {
    const char *path = sound_get_pcm_path(sound);
    if (!path) return false;

    wav_t w = load_wav_file(path);
    if (!w.data) return false;

    w.vol_left  = gstate.settings.max_volume;
    w.vol_right = gstate.settings.max_volume;

    pthread_mutex_lock(&pcm_mutex);
    if (pcm_playlist_num >= MAX_PCM_PLAYLIST) {
        pthread_mutex_unlock(&pcm_mutex);
        free(w.data);
        return false;
    }
    pcm_playlist[pcm_playlist_num++] = w;
    pthread_mutex_unlock(&pcm_mutex);
    return true;
}

bool sound_play_at_vol(enum pcm_sound sound, float wx, float wy, float wz,
                       float vol_scale) {
    const char *path = sound_get_pcm_path(sound);
    if(!path) return false;

    wav_t w = load_wav_file(path);
    if(!w.data) return false;

    float vol_L = 0.0f, vol_R = 0.0f;
#ifdef SPLITSCREEN
    int cnt = splitscreen_player_count();
    int actual = 0;
    for(int i = 0; i < cnt; i++) {
        if(!gstate.local_players[i]) continue;
        struct camera *cam = (i == gstate.active_player)
            ? &gstate.camera : &gstate.cameras[i];
        float pL, pR;
        sound_calc_spatial(wx, wy, wz, cam, &pL, &pR);
        vol_L += pL; vol_R += pR; actual++;
    }
    if(actual > 0) { vol_L /= (float)actual; vol_R /= (float)actual; }
#else
    sound_calc_spatial(wx, wy, wz, &gstate.camera, &vol_L, &vol_R);
#endif
    float mv = gstate.settings.max_volume * vol_scale;
    w.vol_left  = vol_L * mv;
    w.vol_right = vol_R * mv;

    pthread_mutex_lock(&pcm_mutex);
    if(pcm_playlist_num >= MAX_PCM_PLAYLIST) {
        pthread_mutex_unlock(&pcm_mutex);
        free(w.data);
        return false;
    }
    pcm_playlist[pcm_playlist_num++] = w;
    pthread_mutex_unlock(&pcm_mutex);
    return true;
}

bool sound_play_at(enum pcm_sound sound, float wx, float wy, float wz) {
    return sound_play_at_vol(sound, wx, wy, wz, 1.0f);
}

// Entfernt NUR fertig abgespielte PCM-Dateien (pos >= size). Vorher wurde hier
// jeden Frame ALLES freigegeben -> der Callback las freigegebenen Speicher
// (use-after-free) und kein Sound war je zu Ende hoerbar.
void sound_update(void) {
    pthread_mutex_lock(&pcm_mutex);
    int k = 0;
    for (int i = 0; i < pcm_playlist_num; i++) {
        wav_t *w = &pcm_playlist[i];
        if (w->data && w->pos < w->size) {
            pcm_playlist[k++] = *w;   // noch am Spielen -> behalten
        } else {
            free(w->data);            // fertig -> Speicher freigeben
        }
    }
    pcm_playlist_num = k;
    pthread_mutex_unlock(&pcm_mutex);

    if (music_run) {
        pthread_mutex_lock(&bg_mutex);
        bool bg_done = !bg_wav.data || bg_wav.pos >= bg_wav.size;
        pthread_mutex_unlock(&bg_mutex);
        if (bg_done) {
            bg_playlist_num = (bg_playlist_num + 1) % 16;
            const char *path = sound_get_bg_mp3_path(bg_playlist[bg_playlist_num]);
            if (path) {
                wav_t new_bg = load_mp3_file(path);
                new_bg.vol_left  = bg_volume;
                new_bg.vol_right = bg_volume;
                pthread_mutex_lock(&bg_mutex);
                free(bg_wav.data);
                bg_wav = new_bg;
                pthread_mutex_unlock(&bg_mutex);
            }
        }
    }
}

void sound_stop_bg(void) {
    music_run = false;
    pthread_mutex_lock(&bg_mutex);
    free(bg_wav.data);
    bg_wav.data = NULL;
    bg_wav.size = 0;
    bg_wav.pos  = 0;
    pthread_mutex_unlock(&bg_mutex);
    bg_playlist_num = 0;
}

void sound_set_volume_bg(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    bg_volume = volume;
    pthread_mutex_lock(&bg_mutex);
    bg_wav.vol_left  = volume;
    bg_wav.vol_right = volume;
    pthread_mutex_unlock(&bg_mutex);
}

void sound_resume_bg(void) {
    if (music_run) return;
    music_run       = true;
    bg_playlist_num = 0;
    const char *path = sound_get_bg_mp3_path(bg_playlist[0]);
    if (path) {
        wav_t new_bg = load_mp3_file(path);
        new_bg.vol_left  = bg_volume;
        new_bg.vol_right = bg_volume;
        pthread_mutex_lock(&bg_mutex);
        free(bg_wav.data);
        bg_wav = new_bg;
        pthread_mutex_unlock(&bg_mutex);
    }
}

bool sound_play_bg(enum mp3_sound sound[16]) {
    if (!sound) return false;
    music_run = true;
    bg_playlist_num = 0;
    for (int i = 0; i < 16; i++)
        bg_playlist[i] = sound[i];
    const char *path = sound_get_bg_mp3_path(bg_playlist[0]);
    if (path) {
        wav_t new_bg = load_mp3_file(path);
        new_bg.vol_left  = bg_volume;
        new_bg.vol_right = bg_volume;
        pthread_mutex_lock(&bg_mutex);
        free(bg_wav.data);
        bg_wav = new_bg;
        pthread_mutex_unlock(&bg_mutex);
    }
    return true;
}
#endif
