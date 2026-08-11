/*
	Zentrale Versions-/Namensmakros. Bewusst ohne weitere Includes, damit auch
	run_python.c sie nutzen kann, ohne die schwere Include-Kette von
	game_state.h (items.h, world.h, ...) hereinzuziehen.
*/

#ifndef VERSION_H
#define VERSION_H

#define GAME_NAME     "CavEX"
#define VERSION_MAJOR 0
#define VERSION_MINOR 3
#define VERSION_PATCH 8
#define VERSION_FORK  3
#define VERSION_IMPL  "B1.7.3"
#define LICENSE       "Licensed under GPLv3"
#define COPYRIGHT     "Copyright (c) 2023-2026 ByteBit/xtreme8000, lberwa"

#endif
