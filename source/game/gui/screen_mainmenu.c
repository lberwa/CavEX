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
#include <m-lib/m-string.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

static const char* menu_options[4] = {
    "Start",
    "Server",
    "Einstellungen",
    "Beenden"
};

static size_t gui_selection;
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

static void screen_mainmenu_reset(struct screen* s, int width, int height) {
	gstate.game_run = false;
	gstate.num_players = 1;
	server_failed = false;
	input_pointer_enable(true);

	gstate_set_capture_input_all(false);

	sound_init();
	sound_play_bg(bg_playlist);
}
static void screen_mainmenu_update(struct screen* s, float dt) {
	if (server_failed) {

	if (input_pressed(IB_ANY, 0)) 
		sound_play(pcm_click);
		server_failed = false;

	} else {

    // Navigation
    if(input_pressed(IB_GUI_UP, 0) && gui_selection > 0)
        gui_selection--;

    if(input_pressed(IB_GUI_DOWN, 0) && gui_selection < 3)
        gui_selection++;

    // Aktion beim A-Knopf
    if(input_pressed(IB_GUI_CLICK, 0)) {
		sound_play(pcm_click);
        switch(gui_selection) {
            case 0: // Start 
				#ifdef PLATFORM_WII
				menu_screen_set(&spieleranzahl_auswählen);
                #endif
				#ifdef PLATFORM_PC
				menu_screen_set(&screen_select_world);
				#endif
				break;
            case 1: // Server
				if (gstate.network) {
                	menu_screen_set(&screen_server);
				} else {
					server_failed = true;
					return;
				}
				
				break;
            case 2: // Einstellungen
                //settings_menu();
                break;
            case 3: // Beenden
                gstate.quit = true;
                break;
        }
    }

	}

    // Home-Button beendet das Spiel
    if(input_pressed(IB_HOME, 0))
        gstate.quit = true;
}


static void screen_mainmenu_render2D(struct screen* s, int width, int height) {

	gutil_bg();
	gutil_bg_panorama();

	int start_y = height / 3;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	for(int i = 0; i < 4; i++) {
	    int y = start_y + i * (button_height + button_spacing);

    	bool selected = (gui_selection == i);

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

struct screen screen_mainmenu = {
	.reset = screen_mainmenu_reset,
	.update = screen_mainmenu_update,
	.render2D = screen_mainmenu_render2D,
	.render3D = NULL,
	.render_world = true,
};
