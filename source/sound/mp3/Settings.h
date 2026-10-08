#ifndef SETTINGS_STUB_H_
#define SETTINGS_STUB_H_

/* Minimaler Settings-Stub für SoundDecoder::Init().
 * Nur die drei Felder, die vom Audio-Code gelesen werden. */
struct AppSettings {
    int  SoundblockCount = 8;      // Pufferblöcke im Kreis-Puffer
    int  SoundblockSize  = 8192;   // Bytes pro Block (muss 32-Byte-aligned sein)
    bool ResampleTo48kHz = false;  // kein Upsample nötig (ASND läuft auf 48 kHz intern)
};

extern AppSettings Settings;

#endif /* SETTINGS_STUB_H_ */
