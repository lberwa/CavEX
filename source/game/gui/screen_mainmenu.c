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

#include <assert.h>
#include <dirent.h>
#include <m-lib/m-string.h>
#include <string.h>
#include <time.h>
#include "my_text_renderer.h"
#include <gccore.h>

static const char* menu_options[4] = {
    "Start",
    "Server",
    "Einstellungen",
    "Beenden"
};

static size_t gui_selection;
static bool server_failed = false;

static void screen_sworld_reset2(struct screen* s, int width, int height) { //TODO: rename
	gstate.game_run = false;
	gstate.num_players = 1;
	input_pointer_enable(true);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;
}

static void screen_sworld_update2(struct screen* s, float dt) { //TODO: rename
    // Navigation
    if(input_pressed(IB_GUI_UP, 1) && gui_selection > 0)
        gui_selection--;

    if(input_pressed(IB_GUI_DOWN, 1) && gui_selection < 3)
        gui_selection++;

    // Aktion beim A-Knopf
    if(input_pressed(IB_GUI_CLICK, 1)) {
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

    // Home-Button beendet das Spiel
    if(input_pressed(IB_HOME, 1))
        gstate.quit = true;
}


static void screen_sworld_render2D2(struct screen* s, int width, int height) { //TODO: rename

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

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
}

struct screen screen_select_world2 = { //TODO: rename
	.reset = screen_sworld_reset2,
	.update = screen_sworld_update2,
	.render2D = screen_sworld_render2D2,
	.render3D = NULL,
	.render_world = true,
};

