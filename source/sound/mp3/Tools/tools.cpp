/* Stub: ShowError/ShowMsg/ThrowMsg — im reinen Audio-Code nie aufgerufen,
 * aber als Linker-Symbole vorhanden (SoundDecoder.hpp → tools.h). */
#include <stdarg.h>
#include <stdio.h>

extern "C" void ShowError(const char *fmt, ...) { (void)fmt; }
extern "C" void ShowMsg(const char *title, const char *fmt, ...) {
    (void)title; (void)fmt;
}
extern "C" void ThrowMsg(const char *title, const char *fmt, ...) {
    (void)title; (void)fmt;
}
