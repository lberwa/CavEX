#ifdef PLATFORM_WII
#include <ogc/audio.h>
#include <mp3player.h>
#include <asndlib.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <ogc/lwp.h>

#define __XSI_VISIBLE 600
#define __POSIX_VISIBLE 200112
#include <unistd.h>
#include <time.h>

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
/*
#define BUFFER_SIZE 2048  // Beispielgröße
static u8 buffer1[BUFFER_SIZE];
static u8 buffer2[BUFFER_SIZE];

void worker(MP3Player* player) {
    u8* current = buffer1;
    u8* next = buffer2;

    MP3Player_Play(player);

    while (MP3Player_IsPlaying(player)) {
        // Fülle den nächsten Buffer, während der aktuelle läuft
        if (MP3Player_NeedsData(player)) {
            size_t read = MP3Player_Read(player, next, BUFFER_SIZE);
            if (read == 0) break; // Ende erreicht
            MP3Player_SubmitBuffer(player, next, read);
        }

        // Buffer wechseln
        u8* tmp = current;
        current = next;
        next = tmp;

        // Sehr kurz schlafen, damit CPU nicht blockiert
        LWP_YieldThread();
    }

    // Wenn Ende erreicht, stoppen
    MP3Player_Stop(player);
    return;
}
*/
/*#define MP3_CHUNK_SIZE (64*1024) // 64 KB

void* worker(void* arg) {
    const char* path = sound_get_mp3_path(w_sound);
    FILE* f = fopen(path, "rb");
    if(!f) { debug_send("Failed to open MP3"); return NULL; }

    char* buffer = memalign(32, MP3_CHUNK_SIZE);
    size_t read;

    while((read = fread(buffer, 1, MP3_CHUNK_SIZE, f)) > 0) {
        DCFlushRange(buffer, read);
        MP3Player_PlayBuffer(buffer, read, NULL);
        // Warte bis Chunk gespielt ist (z.B. in Schleife prüfen MP3Player_IsPlaying)
        while(MP3Player_IsPlaying()) usleep(50 * 1000);
;

    }

    free(buffer);
    fclose(f);
    return NULL;
}
*/

#ifdef BG_MUSIC
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
    //fclose(musicFile);
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
#endif

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
    #ifdef BG_MUSIC
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
            music_run = false;
        }
    }
    #endif

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
    #ifdef BG_MUSIC
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
    #endif
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

#endif

#ifdef PLATFORM_PC
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pc_sound/include/portaudio.h"
#include "sound.h"
#include "config.h"
#include "game/game_state.h"

#define MAX_PCM_PLAYLIST 16

typedef struct {
    uint8_t *data;
    size_t size;
    int channels;
    int sample_rate;
} wav_t;

// PCM playlist
static wav_t pcm_playlist[MAX_PCM_PLAYLIST];
static int pcm_playlist_num = 0;

static float global_volume = 1.0f;

// PortAudio stream
static PaStream *pa_stream = NULL;

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

    // Einfacher Loader: Header überspringen, nur PCM 16-bit Stereo 44.1kHz
    fseek(f, 44, SEEK_SET); // skip WAV header
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    fseek(f, 44, SEEK_SET); // header überspringen
    size_t data_size = file_size - 44;

    w.data = malloc(data_size);
    if (!w.data) {
        fclose(f);
        return w;
    }

    fread(w.data, 1, data_size, f);
    w.size = data_size;
    w.channels = 2;
    w.sample_rate = 44100;

    fclose(f);
    return w;
}

//----------------------
// Sound Pfade
//----------------------
static const char* sound_get_pcm_path(enum pcm_sound sound) {
    static char fullpath[256];

    // Basis-Pfad aus der Config
    const char* base = config_read_string(&gstate.config_user, "paths.sounds", "mp32/sound");
    if(!base) return NULL;

    switch(sound) {
        case pcm_click:
            snprintf(fullpath, sizeof(fullpath), "%s/random/click.wav", base);
            return fullpath;

        case pcm_chest_close:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chest_close.wav", base);
            return fullpath;

        case pcm_chest_open:
            snprintf(fullpath, sizeof(fullpath), "%s/random/chest_open.wav", base);
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

        default:
            return NULL;
    }
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
    size_t frames_remaining = framesPerBuffer;

    for (int i = 0; i < pcm_playlist_num; i++) {
        wav_t *w = &pcm_playlist[i];
        size_t samples = w->size / sizeof(int16_t);
        int16_t *data16 = (int16_t*)w->data;

        for (size_t j = 0; j < frames_remaining * w->channels && j < samples; j++) {
            out[j] = data16[j] / 32768.0f * global_volume;
        }
    }

    return paContinue;
}

//----------------------
// Init / Cleanup
//----------------------
void sound_init(void) {
    printf("start\n");
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
        return;
    }
    /*
    int numDevices = Pa_GetDeviceCount();
    printf("numDevices: %d", numDevices);
    for(int i=0;i<numDevices;i++){
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        printf("Device %d: %s, maxOut: %d\n", i, info->name, info->maxOutputChannels);
    }*/
    int numDevices = Pa_GetDeviceCount();
    for(int i=0;i<numDevices;i++){
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        printf("Device %d: %s, maxOutChannels=%d, defaultSampleRate=%f\n",
           i, info->name, info->maxOutputChannels, info->defaultSampleRate);
    }
    /*PaStream* stream;
    err = Pa_OpenStream(&stream,
                            NULL,       // kein Input
                            &outputParams, // hier explizit DeviceIndex=HDMI
                            44100, 256, paClipOff,
                            NULL, NULL);
*/

    err = Pa_OpenDefaultStream(&pa_stream,
                               0,          // input channels
                               2,          // output channels
                               paFloat32,  // 32-bit float output
                               44100,      // sample rate
                               256,        // frames per buffer
                               pa_callback,
                               NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio open stream failed: %s\n", Pa_GetErrorText(err));
        return;
    }

    Pa_StartStream(pa_stream);

    pcm_playlist_num = 0;
    return;
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
bool sound_play(enum pcm_sound sound) {
    if (pcm_playlist_num >= MAX_PCM_PLAYLIST) return false;

    const char *path = sound_get_pcm_path(sound);
    if (!path) return false;

    wav_t w = load_wav_file(path);
    if (!w.data) return false;

    pcm_playlist[pcm_playlist_num++] = w;
    return true;
}

// Entfernt abgespielte PCM-Dateien
void sound_update(void) {
    // Einfach: Wenn ein Sound abgespielt wurde, danach freigeben
    for (int i = 0; i < pcm_playlist_num; i++) {
        free(pcm_playlist[i].data);
    }
    pcm_playlist_num = 0;
}

bool sound_play_bg(enum mp3_sound sound[16]) {
    #ifdef BG_MUSIC
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
    #endif
}
#endif