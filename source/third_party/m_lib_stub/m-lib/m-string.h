#ifndef M_LIB_STRING_H
#define M_LIB_STRING_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct m_string_s {
	char* data;
} string_s;

typedef string_s string_t[1];

static inline void string_init(string_t s) {
	s->data = NULL;
}

static inline void string_clear(string_t s) {
	if(s->data) {
		free(s->data);
		s->data = NULL;
	}
}

static inline void string_reset(string_t s) {
	string_clear(s);
}

static inline const char* string_get_cstr(string_t s) {
	return s->data ? s->data : "";
}

static inline void string_set_str(string_t s, const char* str) {
	string_clear(s);
	if(str) {
		s->data = strdup(str);
	}
}

static inline void string_init_set_str(string_t s, const char* str) {
	string_init(s);
	string_set_str(s, str);
}

static inline void string_set(string_t s, string_t other) {
	string_set_str(s, other->data);
}

static inline void string_init_set(string_t s, string_t other) {
	string_init(s);
	string_set(s, other);
}

static inline void string_printf(string_t s, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	if(len < 0) return;

	char* buf = malloc((size_t)len + 1);
	if(!buf) return;

	va_start(args, fmt);
	vsnprintf(buf, (size_t)len + 1, fmt, args);
	va_end(args);

	string_clear(s);
	s->data = buf;
}

static inline void string_init_printf(string_t s, const char* fmt, ...) {
	string_init(s);
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	if(len < 0) return;

	char* buf = malloc((size_t)len + 1);
	if(!buf) return;

	va_start(args, fmt);
	vsnprintf(buf, (size_t)len + 1, fmt, args);
	va_end(args);

	s->data = buf;
}

#endif
