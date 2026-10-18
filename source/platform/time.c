/*
	Copyright (c) 2023 ByteBit/xtreme8000

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

#include "time.h"

#ifdef PLATFORM_PC

#include <stdlib.h>
#include <time.h>

void time_reset() {
	srand((unsigned)time(NULL));
}

ptime_t time_get() {
	#ifdef _WIN32
	ptime_t t;
	QueryPerformanceCounter(&t);
	return t;
	#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t;
	#endif
}

ptime_t time_add_ms(ptime_t t, unsigned int ms) {
	#ifdef _WIN32
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	t.QuadPart += (frequency.QuadPart * (LONGLONG)ms) / 1000;
	return t;
	#else
	uint64_t ns = t.tv_nsec + ms * 1000000LL;
	t.tv_sec += ns / 1000000000LL;
	t.tv_nsec = ns % 1000000000LL;
	return t;
	#endif
}

int32_t time_diff_ms(ptime_t f, ptime_t s) {
	#ifdef _WIN32
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return (int32_t)(((s.QuadPart - f.QuadPart) * 1000) / frequency.QuadPart);
	#else
	return (s.tv_sec - f.tv_sec) * 1000 + (s.tv_nsec - f.tv_nsec) / 1000000;
	#endif
}

float time_diff_s(ptime_t f, ptime_t s) {
	#ifdef _WIN32
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return (float)(s.QuadPart - f.QuadPart) / (float)frequency.QuadPart;
	#else
	return (float)(s.tv_sec - f.tv_sec)
		+ (float)(s.tv_nsec - f.tv_nsec) / 1000000000.0F;
	#endif
}

int get_random(int from, int to) {
	return (rand() % to + from);
}

#endif

#ifdef PLATFORM_WII

#include <ogc/lwp_watchdog.h>

#include <stdlib.h>
#include <ogc/system.h>

void time_reset() {
	settime(0);
	srand(time(NULL));
}

ptime_t time_get() {
	return gettime();
}

ptime_t time_add_ms(ptime_t t, unsigned int ms) {
	return t + TB_TIMER_CLOCK * ms;
}

int32_t time_diff_ms(ptime_t f, ptime_t s) {
	return (s - f) / TB_TIMER_CLOCK;
}

float time_diff_s(ptime_t f, ptime_t s) {
	return (float)(s - f) / (float)(1000UL * TB_TIMER_CLOCK);
}

int get_random(int from, int to) {
	return (rand() % to + from);
}
#endif
