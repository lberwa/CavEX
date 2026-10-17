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
	strncpy(c->filename, filename, sizeof(c->filename) - 1);
	c->filename[sizeof(c->filename) - 1] = '\0';
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

bool config_read_int_chords(struct config* c, const char* key,
							int (*dest)[8], size_t* chord_count) {
	assert(c && key && dest && chord_count);
	JSON_Array* entry = json_object_dotget_array(json_object(c->root), key);
	if(!entry)
		return false;

	/* A flat array is intentionally not accepted here. This keeps old mappings
	 * working unchanged while allowing a mapping to explicitly contain chords. */
	size_t count = json_array_get_count(entry);
	if(count > *chord_count)
		count = *chord_count;
	for(size_t i = 0; i < count; i++) {
		JSON_Array* chord = json_array_get_array(entry, i);
		if(!chord)
			return false;
		size_t keys = json_array_get_count(chord);
		if(keys == 0 || keys > 8)
			return false;
		for(size_t k = 0; k < keys; k++)
			dest[i][k] = (int)json_array_get_number(chord, k);
		for(size_t k = keys; k < 8; k++)
			dest[i][k] = -1;
	}
	*chord_count = count;
	return count > 0;
}

void config_destroy(struct config* c) {
	assert(c && c->root);
	json_value_free(c->root);
}


void settings_init() {
	gstate.settings.debug = true;
	gstate.settings.chunk_render_log = false;
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
	gstate.settings.render_scale_pct = 100; /* Standard: Minimum-Auflösung (802×480) */
	gstate.settings.view_bob = true;
	gstate.settings.max_volume  = 2.0f;
	gstate.settings.step_volume = 0.3f;
	gstate.settings.dig_volume  = 0.3f;
	gstate.settings.bg_music_enabled = true;

	if(gstate.config_user.root) {
		JSON_Object* root = json_object(gstate.config_user.root);
		if(json_object_dothas_value_of_type(root, "settings.debug", JSONBoolean))
			gstate.settings.debug = json_object_dotget_boolean(root, "settings.debug");
		if(json_object_dothas_value_of_type(root, "settings.chunk_render_log", JSONBoolean))
			gstate.settings.chunk_render_log = json_object_dotget_boolean(root, "settings.chunk_render_log");
		if(json_object_dothas_value_of_type(root, "settings.chunk_build_per_tick", JSONNumber))
			gstate.settings.chunk_build_per_tick = (int)json_object_dotget_number(root, "settings.chunk_build_per_tick");
		if(json_object_dothas_value_of_type(root, "settings.chunk_build_budget_ms", JSONNumber))
			gstate.settings.chunk_build_budget_ms = (int)json_object_dotget_number(root, "settings.chunk_build_budget_ms");
		if(json_object_dothas_value_of_type(root, "settings.start_fullscreen", JSONBoolean))
			gstate.settings.start_fullscreen = json_object_dotget_boolean(root, "settings.start_fullscreen");
		if(json_object_dothas_value_of_type(root, "settings.view_distance", JSONNumber))
			gstate.settings.view_distance = (int)json_object_dotget_number(root, "settings.view_distance");
		if(json_object_dothas_value_of_type(root, "settings.render_scale_pct", JSONNumber))
			gstate.settings.render_scale_pct = (int)json_object_dotget_number(root, "settings.render_scale_pct");
		if(json_object_dothas_value_of_type(root, "settings.view_bob", JSONBoolean))
			gstate.settings.view_bob = json_object_dotget_boolean(root, "settings.view_bob");
		if(json_object_dothas_value_of_type(root, "settings.max_volume", JSONNumber))
			gstate.settings.max_volume = (float)json_object_dotget_number(root, "settings.max_volume");
		if(json_object_dothas_value_of_type(root, "settings.step_volume", JSONNumber))
			gstate.settings.step_volume = (float)json_object_dotget_number(root, "settings.step_volume");
		if(json_object_dothas_value_of_type(root, "settings.dig_volume", JSONNumber))
			gstate.settings.dig_volume = (float)json_object_dotget_number(root, "settings.dig_volume");
		if(json_object_dothas_value_of_type(root, "settings.bg_music_enabled", JSONBoolean))
			gstate.settings.bg_music_enabled = json_object_dotget_boolean(root, "settings.bg_music_enabled");
	}
}

void config_save_settings(struct config* c) {
	if(!c || !c->root || !c->filename[0])
		return;

	JSON_Object* root = json_object(c->root);
	json_object_dotset_boolean(root, "settings.debug", gstate.settings.debug);
	json_object_dotset_boolean(root, "settings.chunk_render_log", gstate.settings.chunk_render_log);
	json_object_dotset_number(root, "settings.chunk_build_per_tick", gstate.settings.chunk_build_per_tick);
	json_object_dotset_number(root, "settings.chunk_build_budget_ms", gstate.settings.chunk_build_budget_ms);
	json_object_dotset_boolean(root, "settings.start_fullscreen", gstate.settings.start_fullscreen);
	json_object_dotset_number(root, "settings.view_distance", gstate.settings.view_distance);
	json_object_dotset_number(root, "settings.render_scale_pct", gstate.settings.render_scale_pct);
	json_object_dotset_boolean(root, "settings.view_bob", gstate.settings.view_bob);
	json_object_dotset_number(root, "settings.max_volume", gstate.settings.max_volume);
	json_object_dotset_number(root, "settings.step_volume", gstate.settings.step_volume);
	json_object_dotset_number(root, "settings.dig_volume", gstate.settings.dig_volume);
	json_object_dotset_boolean(root, "settings.bg_music_enabled", gstate.settings.bg_music_enabled);
	json_serialize_to_file_pretty(c->root, c->filename);
}
