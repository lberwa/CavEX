/*
 * play_sound.h — C-Wrapper für SoundHandler/SoundDecoder (Wii, Dimok)
 *
 * Nutzung von C:
 *   #include "sound/mp3/play_sound.h"
 *   soundhandler_init();
 *   soundhandler_add_file(0, "sd:/test.mp3");
 *   soundhandler_play_start(0, 200, 200);
 *   while (soundhandler_is_playing(0)) VIDEO_WaitVSync();
 *   soundhandler_stop(0);
 *   soundhandler_destroy();
 *
 * Die Implementierung der Funktionen muss in einer .cpp-Datei kompiliert
 * werden (z.B. play_sound.cpp), die ebenfalls dieses Header einbindet.
 * Von C-Dateien werden nur die Deklarationen gesehen (extern "C").
 */

#ifndef PLAY_SOUND_H_
#define PLAY_SOUND_H_

#ifdef PLATFORM_WII

#include <gctypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── SoundHandler-Singleton ───────────────────────────────────────────────── */

/* Startet den Decoder-Thread. Muss vor allen anderen Calls aufgerufen werden.
 * ASND_Init() / ASND_Pause(0) müssen vorher aufgerufen worden sein. */
void soundhandler_init(void);

/* Stoppt den Decoder-Thread und gibt alle Ressourcen frei. */
void soundhandler_destroy(void);

/* Weckt den Decoder-Thread (z.B. aus dem ASND-Callback). */
void soundhandler_signal(void);

/* Gibt true zurück, wenn der Decoder-Thread gerade aktiv dekodiert. */
bool soundhandler_is_decoding(void);

/* ── Decoder-Verwaltung (voice = Slot 0..MAX_DECODERS-1) ─────────────────── */

/* Lädt eine Audiodatei in den Slot voice. Vorhandener Decoder wird ersetzt. */
void soundhandler_add_file(int voice, const char *path);

/* Lädt Audio aus einem RAM-Buffer in den Slot voice. */
void soundhandler_add_buffer(int voice, const u8 *buf, int len);

/* Entfernt den Decoder im Slot voice und gibt ihn frei. */
void soundhandler_remove(int voice);

/* ── Decoder-Status ───────────────────────────────────────────────────────── */

/* Gibt true zurück, wenn der erste Puffer dekodiert und abspielbereit ist. */
bool soundhandler_is_ready(int voice);

/* Gibt true zurück, wenn die Datei vollständig dekodiert wurde (EOF). */
bool soundhandler_is_eof(int voice);

/* ASND-Format (VOICE_STEREO_16BIT etc.) des Decoders. */
u8   soundhandler_get_format(int voice);

/* Samplerate des Decoders (z.B. 44100). */
u16  soundhandler_get_samplerate(int voice);

/* Zeiger auf den aktuell abzuspielenden Puffer (DMA-aligned, 32 Byte). */
u8  *soundhandler_get_buffer(int voice);

/* Größe des aktuellen Puffers in Bytes. */
u32  soundhandler_get_buffersize(int voice);

/* Wechselt intern auf den nächsten Puffer (wird aus dem ASND-Callback gerufen). */
void soundhandler_load_next(int voice);

/* Aktiviert/deaktiviert Endlosloop. */
void soundhandler_set_loop(int voice, bool loop);

/* ── Hochniveau-Wiedergabe ────────────────────────────────────────────────── */

/* Wartet bis der erste Puffer bereit ist, dann startet den ASND-Voice.
 * vol_l / vol_r: Lautstärke 0–255 (links/rechts).
 * Gibt false zurück wenn kein Decoder vorhanden oder EOF sofort erreicht. */
bool soundhandler_play_start(int voice, int vol_l, int vol_r);

/* Wie soundhandler_play_start, aber OHNE Warte-Schleife.
 * Gibt true zurück wenn der erste Puffer bereit war und ASND gestartet wurde,
 * false wenn noch dekodiert wird (einfach nächsten Frame erneut aufrufen). */
bool soundhandler_play_if_ready(int voice, int vol_l, int vol_r);

/* Gibt true zurück wenn der ASND-Voice noch aktiv spielt. */
bool soundhandler_is_playing(int voice);

/* Stoppt ASND-Voice sofort und entfernt den Decoder. */
void soundhandler_stop(int voice);

#ifdef __cplusplus
} /* extern "C" */


/* ══════════════════════════════════════════════════════════════════════════════
 * Implementierung (nur sichtbar wenn als C++ kompiliert)
 * ══════════════════════════════════════════════════════════════════════════════
 * Diese Datei kann als "Header-only"-Implementierung verwendet werden:
 * Genau eine .cpp-Datei muss PLAY_SOUND_IMPL definieren bevor sie diesen
 * Header einbindet.  Alle anderen .cpp/.c-Dateien sehen nur die Deklarationen.
 */
#ifdef PLAY_SOUND_IMPL

#include <asndlib.h>
#include <unistd.h>
#include "SoundHandler.hpp"

/* ASND-Callback: wird aufgerufen wenn ein Puffer fertig abgespielt wurde.
 * Reihenfolge:
 *   1. LoadNext() → BufferCircle-Zeiger vorrücken (gibt alten Puffer frei)
 *   2. ThreadSignal() → Decoder-Thread wecken (füllt nächsten Puffer)
 *   3. IsBufferReady() prüfen → nächsten Puffer in ASND einreihen */
static void snd_next_buffer(s32 voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return;

    SoundDecoder *dec = h->Decoder(voice);
    if (!dec) return;

    dec->LoadNext();
    h->ThreadSignal();

    if (dec->IsBufferReady())
        ASND_AddVoice(voice, dec->GetBuffer(), dec->GetBufferSize());
}

extern "C" {

void soundhandler_init(void)
{
    SoundHandler::Instance();
}

void soundhandler_destroy(void)
{
    SoundHandler::DestroyInstance();
}

void soundhandler_signal(void)
{
    SoundHandler *h = SoundHandler::Instance();
    if (h) h->ThreadSignal();
}

bool soundhandler_is_decoding(void)
{
    SoundHandler *h = SoundHandler::Instance();
    return h ? h->IsDecoding() : false;
}

void soundhandler_add_file(int voice, const char *path)
{
    SoundHandler *h = SoundHandler::Instance();
    if (h) h->AddDecoder(voice, path);
}

void soundhandler_add_buffer(int voice, const u8 *buf, int len)
{
    SoundHandler *h = SoundHandler::Instance();
    if (h) h->AddDecoder(voice, buf, len);
}

void soundhandler_remove(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (h) h->RemoveDecoder(voice);
}

bool soundhandler_is_ready(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return false;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->IsBufferReady() : false;
}

bool soundhandler_is_eof(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return true;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->IsEOF() : true;
}

u8 soundhandler_get_format(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return 0;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->GetFormat() : 0;
}

u16 soundhandler_get_samplerate(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return 44100;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->GetSampleRate() : 44100;
}

u8 *soundhandler_get_buffer(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return NULL;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->GetBuffer() : NULL;
}

u32 soundhandler_get_buffersize(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return 0;
    SoundDecoder *d = h->Decoder(voice);
    return d ? d->GetBufferSize() : 0;
}

void soundhandler_load_next(int voice)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return;
    SoundDecoder *d = h->Decoder(voice);
    if (d) d->LoadNext();
}

void soundhandler_set_loop(int voice, bool loop)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return;
    SoundDecoder *d = h->Decoder(voice);
    if (d) d->SetLoop(loop);
}

/* Gemeinsame Hilfsfunktion: ASND-Voice mit bereits-bereitem Puffer starten. */
static bool snd_do_start(SoundHandler *h, SoundDecoder *dec,
                         int voice, int vol_l, int vol_r)
{
    if (vol_l < 0) vol_l = 0; else if (vol_l > 255) vol_l = 255;
    if (vol_r < 0) vol_r = 0; else if (vol_r > 255) vol_r = 255;

    SND_SetVoice(voice,
                 dec->GetFormat(),
                 dec->GetSampleRate(),
                 0,
                 dec->GetBuffer(),
                 dec->GetBufferSize(),
                 vol_l, vol_r,
                 snd_next_buffer);

    /* Zweiten Puffer vorplanen für nahtlosen Übergang. */
    dec->LoadNext();
    h->ThreadSignal();
    if (dec->IsBufferReady())
        ASND_AddVoice(voice, dec->GetBuffer(), dec->GetBufferSize());

    return true;
}

bool soundhandler_play_start(int voice, int vol_l, int vol_r)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return false;
    SoundDecoder *dec = h->Decoder(voice);
    if (!dec) return false;

    /* Warte bis der erste Puffer dekodiert ist (maximal ~1 s). */
    int tries = 200;
    while (!dec->IsBufferReady() && tries-- > 0)
        usleep(5000);

    if (!dec->IsBufferReady()) return false;
    return snd_do_start(h, dec, voice, vol_l, vol_r);
}

bool soundhandler_play_if_ready(int voice, int vol_l, int vol_r)
{
    SoundHandler *h = SoundHandler::Instance();
    if (!h) return false;
    SoundDecoder *dec = h->Decoder(voice);
    if (!dec || !dec->IsBufferReady()) return false;
    return snd_do_start(h, dec, voice, vol_l, vol_r);
}

bool soundhandler_is_playing(int voice)
{
    return ASND_StatusVoice(voice) != SND_UNUSED;
}

void soundhandler_stop(int voice)
{
    ASND_StopVoice(voice);
    soundhandler_remove(voice);
}

} /* extern "C" */

#endif /* PLAY_SOUND_IMPL */
#endif /* __cplusplus */

#endif /* PLATFORM_WII */
#endif /* PLAY_SOUND_H_ */
