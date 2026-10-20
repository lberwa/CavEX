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
#include "../../network/level_archive.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../stack.h"
#include "../../util.h"
#include "../game_state.h"
#include "screen.h"
#include "../../sound/sound.h"

#include <assert.h>
#include <dirent.h>
#include "../../m-lib/m-string.h"
#include <string.h>
#include <time.h>
#include <stdbool.h>

static const char* menu_options[4] = {
    "Start",
    "Settings",
    "Paused",
    "Save & Quit"
};

static size_t gui_selection[4];
static bool server_failed = false;

static void game_menu_select(int option) {
	int player = gstate_active_player();
	sound_play(pcm_click);
	switch(option) {
		case 0: screen_set_player(player, &screen_ingame); break;
		case 1: screen_set_player(player, &screen_game_settings); break;
		case 2:
			gstate.paused = true;
			screen_set(&screen_pause);
			svin_rpc_send(&(struct server_rpc) {.type = SRPC_TOGGLE_PAUSE});
			break;
		case 3:
			gstate.paused = false;
			screen_set(&screen_select_world);
			svin_rpc_send(&(struct server_rpc) {.type = SRPC_UNLOAD_WORLD});
			break;
	}
}

static void screen_gmenu_reset(struct screen* s, int width, int height) {
	gutil_button_new_menu();

	int player = gstate_active_player();
	gstate.game_run = false;
#ifndef SPLITSCREEN
	gstate.num_players = 2;
#endif
	server_failed = false;
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);
	gui_selection[player] = 0;
}

static void screen_gmenu_update(struct screen* s, float dt) { 
	int player = gstate_active_player();
	(void)s;
	(void)dt;
	if(input_pressed(IB_BACK, player))
		screen_set_player(player, &screen_ingame);
}


static void screen_gmenu_render2D(struct screen* s, int width, int height) { 


	int start_y = (gstate.num_players < 2) ? (height / 3) : 7;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	int player = gstate_active_player();
	float pointer_x, pointer_y, pointer_angle;
	bool pointer_available = screen_pointer_local(player, width, height,
										 &pointer_x, &pointer_y, &pointer_angle);
	gutil_button_reset(player, pointer_available, pointer_x, pointer_y);
	for(int i = 0; i < 4; i++) {
	    int y = start_y + i * (button_height + button_spacing);
		gutil_button((width - button_width) / 2, y, button_width, button_height,
					 menu_options[i], game_menu_select, i, 0, i);

	}
	gutil_button_update();
	gutil_button_render();
	
	if (server_failed) {
		int window_width = 220;
		int wx = width / 2 - window_width / 2;
		int wy = height/2 - 60;

		gutil_window(wx, wy, window_width, 100, "Connection Error");

		gutil_text(wx + 20, wy + 40, "Please try again", 16, false);
		gutil_text(wx + 20, wy + 60, "later.", 16, false);
	}

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");

	if(pointer_available) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(pointer_x, pointer_y, glm_rad(pointer_angle),
		                     0, 0, 256, 256,
		                     48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}

struct screen screen_game_menu = {
	.reset = screen_gmenu_reset,
	.update = screen_gmenu_update,
	.render2D = screen_gmenu_render2D,
	.render3D = NULL,
	.render_world = true,
};
