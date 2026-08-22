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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "config_frozen.h"
#include "game/game_state.h"
#include "network/server_local.h" /* MAX_VIEW_DISTANCE */

/* Legt alle Verzeichnisebenen von path an (wie "mkdir -p"); path ist eine
   DATEI -> alles bis zum letzten '/' wird als Ordner erstellt. Best effort. */
static void config_make_parent_dirs(const char* path) {
	char tmp[512];
	size_t n = strlen(path);
	if(n == 0 || n >= sizeof(tmp))
		return;
	memcpy(tmp, path, n + 1);

	for(char* p = tmp + 1; *p; p++) {
		if(*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755); /* Fehler (z.B. EEXIST) ignorieren */
			*p = '/';
		}
	}
}

/* Schreibt die eingebettete ("frozen") Default-Config nach filename. Der Inhalt
   ist plattformabhaengig (config_wii.json bzw. config_pc.json). Best effort. */
static void config_write_frozen(const char* filename) {
#ifdef PLATFORM_WII
	const char* content = FROZEN_CONFIG_WII;
#else
	const char* content = FROZEN_CONFIG_PC;
#endif
	config_make_parent_dirs(filename);
	FILE* f = fopen(filename, "wb");
	if(f) {
		fwrite(content, 1, strlen(content), f);
		fclose(f);
	}
}

bool config_create(struct config* c, const char* filename) {
	assert(c && filename);
	c->root = json_parse_file(filename);

	if(!c->root) {
		/* Config fehlt (oder ist kaputt) -> frozen Default dorthin schreiben und
		   erneut laden, damit das Spiel auf beiden Plattformen startet. */
		config_write_frozen(filename);
		c->root = json_parse_file(filename);
	}

	if(!c->root)
		return false;

	if(json_value_get_type(c->root) != JSONObject) {
		config_destroy(c);
		return false;
	}

	return true;
}

const char* config_read_string(struct config* c, const char* key,
							   const char* fallback) {
	assert(c && key);
	// TODO: only give out copy
	const char* res = json_object_dotget_string(json_object(c->root), key);
	return res ? res : fallback;
}

bool config_read_int_array(struct config* c, const char* key, int* dest,
						   size_t* length) {
	assert(c && key && dest);

	JSON_Array* entry = json_object_dotget_array(json_object(c->root), key);

	if(!entry)
		return false;

	*length = *length < json_array_get_count(entry) ?
		*length :
		json_array_get_count(entry);

	for(size_t k = 0; k < *length; k++)
		dest[k] = json_array_get_number(entry, k);

	return true;
}

void config_destroy(struct config* c) {
	assert(c && c->root);
	json_value_free(c->root);
}


void settings_init() {
	gstate.settings.debug = true;
	/* IN-GAME chunk generation speed (applies while playing, NOT on the loading
	 * screen -- loading always runs at full speed):
	 *   chunk_build_budget_ms = milliseconds of each ~50ms game tick spent
	 *                       generating. This is the main knob. Going much above
	 *                       ~40ms eats the whole tick and the game clock starts to
	 *                       run slow (the mesher still preempts, so rendering stays
	 *                       fine -- only movement/logic slows down).
	 *   chunk_build_per_tick = hard cap on generation steps per tick; left high so
	 *                       the time budget above is the real limit. */
#ifdef PLATFORM_WII
	gstate.settings.chunk_build_per_tick = 1000;
	gstate.settings.chunk_build_budget_ms = 60;
#else
	gstate.settings.chunk_build_per_tick = 1000;
	gstate.settings.chunk_build_budget_ms = 40;
#endif

	gstate.settings.start_fullscreen = false;

	/* Standard: maximale (hardware-sichere) Sichtweite. Der Spieler kann sie im
	 * Spielmenue kleiner stellen -> weniger RAM-Druck, beide Splitscreen-Spieler
	 * passen zuverlaessig in die Chunk-Grenze. */
	gstate.settings.view_distance = 3;
}