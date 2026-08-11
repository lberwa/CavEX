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
#include <mp3player.h>
#include <asndlib.h>
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
        return w;
    }

    
    fseek(f, 0, SEEK_END);
    w.size = ftell(f);
    rewind(f);
    w.data = memalign(32, w.size);
    fread(w.data, 1, w.size, f);
    DCFlushRange(w.data, w.size);
    fclose(f);


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
    pcm_playlist_init(16);
    
    ASND_Init();
    ASND_Pause(0);

    MP3Player_Init();
    MP3Player_Volume(255);

    SND_Init(INIT_RATE_48000); // oder 48000
    SND_Pause(0);
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
    if(!f) { return NULL; }

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

    FILE* musicFile = fopen(path, "rb");
    if(!musicFile) {
        //return false;
    }

    // Dateigröße ermitteln
    fseek(musicFile, 0, SEEK_END);
    long musicFileSize = ftell(musicFile);
    rewind(musicFile);

    // Speicher für MP3
    char* musicBuffer = (char*) malloc(musicFileSize);
    if(!musicBuffer) {
        fclose(musicFile);
        //return false;
    }

    size_t bytesRead = fread(musicBuffer, 1, musicFileSize, musicFile);
    //fclose(musicFile);

    MP3Player_PlayBuffer(musicBuffer, bytesRead, NULL);
    return NULL;
}



static bool st_sound_play_bg(enum mp3_sound sound) {
    w_sound = sound;

    LWP_CreateThread(&t, worker, NULL, NULL, 0, 64);
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
            bg_playlist_num++;
            if (bg_playlist[bg_playlist_num] == NULL)
             bg_playlist_num = 0;
            st_sound_play_bg(bg_playlist[bg_playlist_num]);
            music_run = false;
        }
    }
    #endif

    for(int i = 0; i < pcm_playlist_num; i++) {
        if (!ASND_StatusVoice(pcm_playlist[i])) {
            pcm_playlist_remove(i);
            i--;
        }
    }
}
    //bg_playlist = 0;


bool sound_play_bg(enum mp3_sound sound[16]) {
    #ifdef BG_MUSIC
    if (!sound)
     return false;
    music_run = true;
    for (int i = 0; i < 16; i++) {
        
        bg_playlist[i] = sound[i];
     }
    return true;
    #endif
}

bool sound_play(enum pcm_sound sound) {
    //debug_send("nicht sound NULL");
    if (pcm_playlist_num > 15)
     return false;
    const char* path = sound_get_pcm_path(sound);
    wav_t sound_data = load_file(path);

    int voice = SND_GetFirstUnusedVoice();

    pcm_playlist_add(voice);
    

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
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include "pc_sound/include/portaudio.h"
#include "sound.h"
#include "config.h"
#include "game/game_state.h"

#define MAX_PCM_PLAYLIST 16
#define OUT_CHANNELS 2      // Stream ist stereo (siehe Pa_OpenDefaultStream)

typedef struct {
    uint8_t *data;
    size_t size;            // Groesse der PCM-Daten in Bytes
    size_t pos;             // aktuelle Abspielposition in Bytes
    int channels;
    int sample_rate;
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
            float v = out[j] + (data16[s] / 32768.0f) * global_volume;
            if (v > 1.0f) v = 1.0f;
            else if (v < -1.0f) v = -1.0f;
            out[j] = v;
        }
        w->pos = s * sizeof(int16_t); // Position fuer naechsten Callback merken
    }
    pthread_mutex_unlock(&pcm_mutex);

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
    const char *path = sound_get_pcm_path(sound);
    if (!path) return false;

    // Laden ausserhalb des Locks (Disk-I/O), damit der Callback nicht wartet.
    wav_t w = load_wav_file(path);
    if (!w.data) return false;

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
}

bool sound_play_bg(enum mp3_sound sound[16]) {
    #ifdef BG_MUSIC
    if (!sound)
     return false;
    music_run = true;
    for (int i = 0; i < 16; i++) {
        
        bg_playlist[i] = sound[i];
     }
    return true;
    #endif
}
#endif
