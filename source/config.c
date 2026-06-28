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

#include "config.h"
#include "game/game_state.h"

bool config_create(struct config* c, const char* filename) {
	assert(c && filename);
	c->root = json_parse_file(filename);

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
}