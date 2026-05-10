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
#include "../../sound.h"

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

static enum mp3_sound bg_playlist[16] = {
	mp3_bg1,
	mp3_bg2,
	mp3_bg3,
	mp3_bg4,
	mp3_bg5,
	mp3_bg6,
	mp3_bg7,
	mp3_bg8,
	mp3_bg9,
	mp3_bg10,
};

static void screen_gmenu_reset(struct screen* s, int width, int height) { 
	int player = gstate_active_player();
	gstate.game_run = false;
#ifndef SPLITSCREEN
	gstate.num_players = 2;
#endif
	server_failed = false;
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);
	gui_selection[player] = 0;

	sound_init();
	sound_play_bg(bg_playlist);
}

static void screen_gmenu_update(struct screen* s, float dt) { 
	int player = gstate_active_player();
    // Navigation
    if(input_pressed(IB_GUI_UP, player) && gui_selection[player] > 0)
        gui_selection[player]--;

    if(input_pressed(IB_GUI_DOWN, player) && gui_selection[player] < 3)
        gui_selection[player]++;

    // Aktion beim A-Knopf
	    if(input_pressed(IB_GUI_CLICK, player)) {
			sound_play(pcm_click);
	        switch(gui_selection[player]) {
            case 0:  
				screen_set_player(player, &screen_ingame);
				break;
            case 1: 
				break;
            case 2: 
                gstate.paused = true;
                screen_set(&screen_pause);
        		svin_rpc_send(&(struct server_rpc) {
        			.type = SRPC_TOGGLE_PAUSE,
		        });
                break;
	            case 3:
					// Save & quit: unload the world but do NOT toggle pause here.
					// `screen_game_menu` itself doesn't pause the server, so toggling
					// would leave the server stuck in paused state for the next world.
					gstate.paused = false;
					screen_set(&screen_select_world);
					svin_rpc_send(&(struct server_rpc) {
						.type = SRPC_UNLOAD_WORLD,
					});
	                break;
	        }
	    }
	}


static void screen_gmenu_render2D(struct screen* s, int width, int height) { 


	int start_y = (gstate.num_players < 2) ? (height / 3) : 7;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	for(int i = 0; i < 4; i++) {
	    int y = start_y + i * (button_height + button_spacing);

    	bool selected = (gui_selection[gstate_active_player()] == (size_t)i);

    	// Texcoords für hell/dunkel
    	int tex_x = 0;
    	int tex_y = selected ? 62 : 42; // hell : dunkel
    	int tex_w = 200;
    	int tex_h = 20;

    	// Button skalieren auf 300x40
		gfx_bind_texture(&texture_gui2);
    	gutil_texquad((width - button_width) / 2, y, tex_x, tex_y, 
					  					 tex_w, tex_h, button_width, button_height);


		//int y = start_y + i*line_height;
    	gutil_text((width/2) - (gutil_font_width(menu_options[i], 20)/2),
									 y + 10, menu_options[i], 20, true);

	}
	
	if (server_failed) {
		int window_width = 220;
		int wx = width / 2 - window_width / 2;
		int wy = height/2 - 60;

		gutil_window(wx, wy, window_width, 100, "Verbindungsfehler");

		gutil_text(wx + 20, wy + 40, "Versuchen sie es", 16, false);
		gutil_text(wx + 20, wy + 60, "später erneut.", 16, false);
	}

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
}

struct screen screen_game_menu = {
	.reset = screen_gmenu_reset,
	.update = screen_gmenu_update,
	.render2D = screen_gmenu_render2D,
	.render3D = NULL,
	.render_world = true,
};
