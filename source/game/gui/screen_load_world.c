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

#include "../../graphics/gui_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../network/client_interface.h"
#include "../../network/server_local.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"

#include <stdio.h>

static size_t screen_lworld_preload_target(void) {
	size_t players = splitscreen_enabled() ? (size_t)splitscreen_player_count() : 1u;
	size_t target = MAX_HIGH_DETAIL_CHUNKS * players;

	size_t max_target = MAX_CHUNKS * players;
	if(target > max_target)
		target = max_target;
	return target;
}

static void screen_lworld_reset(struct screen* s, int width, int height) {
	input_pointer_enable(true);

	gstate_set_capture_input_all(false);
}

static void screen_lworld_update(struct screen* s, float dt) {
	size_t preload_target = screen_lworld_preload_target();
	if(gstate.world_loaded
	   && clin_pending_chunk_count() == 0
	   && world_loaded_chunks(&gstate.world) >= preload_target)
		screen_set(&screen_ingame);
}

static void screen_lworld_render2D(struct screen* s, int width, int height) {
	char dbg[96];
	gutil_bg();

	gutil_text((width - gutil_font_width("Generating level", 8 * GFX_GUI_SCALE)) / 2,
			   height / 2 - 20 * GFX_GUI_SCALE, "Generating level", 8 * GFX_GUI_SCALE, true);

	gutil_text((width - gutil_font_width("Building terrain", 8 * GFX_GUI_SCALE)) / 2,
			   height / 2 + 4 * GFX_GUI_SCALE, "Building terrain", 8 * GFX_GUI_SCALE, true);

	gutil_text(2 * GFX_GUI_SCALE, height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 2, "Licensed under GPLv3", 8 * GFX_GUI_SCALE, true);
	gutil_text(2 * GFX_GUI_SCALE, height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 1, "Copyright (c) 2026 lberwa",
			   8 * GFX_GUI_SCALE, true);

	// just a rough estimate
	size_t preload_target = screen_lworld_preload_target();
	size_t loaded_chunks = world_loaded_chunks(&gstate.world);
	size_t pending_chunks = clin_pending_chunk_count();
	float progress = fminf((float)world_loaded_chunks(&gstate.world)
						   / (float)preload_target,
						   1.0F);

	gfx_texture(false);
	gutil_texquad_col((width - 100 * GFX_GUI_SCALE) / 2, height / 2 + 16 * GFX_GUI_SCALE, 0, 0, 0, 0, 100 * GFX_GUI_SCALE, 2 * GFX_GUI_SCALE,
					  128, 128, 128, 255);
	gutil_texquad_col((width - 100 * GFX_GUI_SCALE) / 2, height / 2 + 16 * GFX_GUI_SCALE, 0, 0, 0, 0,
					  100 * GFX_GUI_SCALE * progress, 2 * GFX_GUI_SCALE, 128, 255, 128, 255);
	gfx_texture(true);

	snprintf(dbg, sizeof(dbg), "loaded: %zu / %zu", loaded_chunks,
			 preload_target);
	gutil_text(4, 4, dbg, GFX_GUI_SCALE * 8, true);

	snprintf(dbg, sizeof(dbg), "pending chunks: %zu", pending_chunks);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 1, dbg, GFX_GUI_SCALE * 8,
			   true);

	snprintf(dbg, sizeof(dbg), "world_loaded: %d", gstate.world_loaded ? 1 : 0);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 2, dbg, GFX_GUI_SCALE * 8,
			   true);

	snprintf(dbg, sizeof(dbg), "players: %d", splitscreen_enabled() ?
			 splitscreen_player_count() : 1);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 3, dbg, GFX_GUI_SCALE * 8,
			   true);
}

struct screen screen_load_world = {
	.reset = screen_lworld_reset,
	.update = screen_lworld_update,
	.render2D = screen_lworld_render2D,
	.render3D = NULL,
	.render_world = false,
};
