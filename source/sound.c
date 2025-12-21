//#ifdef PLATFORM_WII
#include <ogc/audio.h>
#include <mp3player.h>
#include <asndlib.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <ogc/lwp.h>

#include "sound.h"
#include "config.h"
#include "game/game_state.h"

#include "network/server_comunication.h"
#include "my_text_renderer.h"

typedef struct {
    u8 *data;
    u32 size;
} wav_t;

static lwp_t t;

static float global_volume = 1.0f;      // 0.0 – 1.0
static enum mp3_sound current_playing = -1;
static enum mp3_sound bg_playlist[16];
static int bg_playlist_num = 0;
//static int pcm_playlist[16];
//static int pcm_playlist_num = 0;
static int *pcm_playlist = NULL;
static int pcm_playlist_num = 0;
static int pcm_playlist_cap = 0;
static bool music_run = false;



void pcm_playlist_init(int capacity) {
    pcm_playlist = malloc(sizeof(int) * capacity);
    pcm_playlist_cap = capacity;
    pcm_playlist_num = 0;
}

bool pcm_playlist_add(int voice) {
    if (pcm_playlist_num >= pcm_playlist_cap)
        return false;

    pcm_playlist[pcm_playlist_num++] = voice;
    return true;
}

void pcm_playlist_remove(int index) {
    if (index < 0 || index >= pcm_playlist_num)
        return;

    for (int i = index; i < pcm_playlist_num - 1; i++) {
        pcm_playlist[i] = pcm_playlist[i + 1];
    }

    pcm_playlist_num--;
}

/*
static int* remove_element(int *list, int length, int index) {
    int new_length;
    if(index < 0 || index >= length) {
        new_length = length;
        return list; // Index ungültig, originale Liste zurückgeben
    }

    new_length = length - 1;
    int *new_list = malloc(sizeof(int) * (new_length));
    if (!new_list) {
        new_length = 0;
        return NULL; // Speicherfehler
    }

    for (int i = 0, j = 0; i < length; i++) {
        if (i == index) continue; // Element überspringen
        new_list[j++] = list[i];
    }

    return new_list;
}
*/

//--------------------------------
//paths
//--------------------------------
static wav_t load_file(const char *path) {
    wav_t w = {0};
    
    FILE *f = fopen(path, "rb");
    if(!f) {
        debug_send(" data is NULL   ");
        return w;
    }

    debug_send("no return in load file");
    
    fseek(f, 0, SEEK_END);
    w.size = ftell(f);
    rewind(f);
    debug_send("2");
    w.data = memalign(32, w.size);
    fread(w.data, 1, w.size, f);
    DCFlushRange(w.data, w.size);
    fclose(f);

    debug_send("return w");

    return w;
}

static const char* sound_get_pcm_path(enum pcm_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.sounds", "mp32/sound");
    if(!base) return NULL;

    switch(sound) {
        case pcm_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/click.pcm", base);
            return fullpath;

        case pcm_chest_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chest_close.pcm", base);
            return fullpath;

        case pcm_chest_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chest_open.pcm", base);
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
        default:
            return NULL;
    }
}

static const char* sound_get_mp3_path(enum mp3_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.MP3", "mp32");
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


void sound_init() {
    debug_send("initializing mp3 sound system...                ");
    pcm_playlist_init(16);
    
    ASND_Init();
    ASND_Pause(0);

    MP3Player_Init();
    MP3Player_Volume(255);

    SND_Init(INIT_RATE_48000); // oder 48000
    SND_Pause(0);
    debug_send("alle initalisiert                  ");
}

enum mp3_sound w_sound;

void* worker(void* arg) {
    char* path = sound_get_mp3_path(w_sound);
    debug_send(path);

    FILE* musicFile = fopen(path, "rb");
    if(!musicFile) {
        debug_send("Failed to open MP3 file!");
        //return false;
    }
    debug_send("MP3 file opened");

    // Dateigröße ermitteln
    fseek(musicFile, 0, SEEK_END);
    long musicFileSize = ftell(musicFile);
    rewind(musicFile);

    // Speicher für MP3
    char* musicBuffer = (char*) malloc(musicFileSize);
    if(!musicBuffer) {
        debug_send("Failed to allocate memory for MP3!");
        fclose(musicFile);
        //return false;
    }

    size_t bytesRead = fread(musicBuffer, 1, musicFileSize, musicFile);
    fclose(musicFile);
    debug_send("MP3 file loaded into buffer");

    MP3Player_PlayBuffer(musicBuffer, bytesRead, NULL);
    return NULL;
}

static bool st_sound_play_bg(enum mp3_sound sound) {
    w_sound = sound;

    LWP_CreateThread(&t, worker, NULL, NULL, 0, 64);
    debug_send("MP3 playback started");
    return true;
}

void sound_stop_bg() {
    MP3Player_Stop();
    music_run = false;
}


void sound_set_volume_bg(float volume) {
    if(volume < 0.0f) volume = 0.0f;
    if(volume > 1.0f) volume = 1.0f;

    global_volume = volume;
    MP3Player_Volume((u32)(volume * 255.0f));
}


void sound_update() {
    if (music_run) {
        if (!MP3Player_IsPlaying()) {
            debug_send("MP3 ist nicht am spielen, nächster Titel wird gestartet");
            bg_playlist_num++;
            if (bg_playlist[bg_playlist_num] == NULL)
             bg_playlist_num = 0;
            debug_send(&bg_playlist_num);
            debug_send(&bg_playlist[bg_playlist_num]);
            debug_send("st_sound_play_bg wird aufgerufen");
            st_sound_play_bg(bg_playlist[bg_playlist_num]);
        }
    }
  

    for(int i = 0; i < pcm_playlist_num; i++) {
        if (!ASND_StatusVoice(pcm_playlist[i])) {
            pcm_playlist_remove(i);
            debug_send("remove");
            i--;
        }
    }
}
    //bg_playlist = 0;


bool sound_play_bg(enum mp3_sound sound[16]) {
    debug_send("sound_play_bg aufgerufen");
    if (!sound)
     return false;
    debug_send("sound array ist nicht null");
    music_run = true;
    for (int i = 0; i < 16; i++) {
        
        bg_playlist[i] = sound[i];
     }
    debug_send("bg_playlist gesetzt");
    return true;
}

bool sound_play(enum pcm_sound sound) {
    debug_send("start!!!!!!!!!!!!!!1    ");
    //debug_send("nicht sound NULL");
    if (pcm_playlist_num > 15)
     return false;
    debug_send("liste nicht zu lang    ");
    const char* path = sound_get_pcm_path(sound);
    debug_send(path);
    wav_t sound_data = load_file(path);

    int voice = SND_GetFirstUnusedVoice();

    pcm_playlist_add(voice);
    
    debug_send("pcm_playlist");

    if(sound_data.data) {
		SND_SetVoice(
	        voice, // freie Stimme
	        VOICE_STEREO_16BIT_LE,        // 16-bit Stereo
		    44100,                     // Sample-Rate
	        0,                         // Startoffset
	        sound_data.data,           // Header überspringen
	        sound_data.size,           // Größe ohne Header
	        255, 255,                  // Lautstärke L/R
	        NULL                        // Callback optional
	    );
	} else {
        return false;
    }

    //pcm_playlist_num++;
    return true;
}

//#endif