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
#include "../../graphics/background/background.h"

//#include "../../menu/menu.h"
#include "screen_select_world.h"

#include <assert.h>
#include <dirent.h>
#include <m-lib/m-string.h>
#include <string.h>
#include <time.h>
#include "my_text_renderer.h"
#include <gccore.h>
//#include <wiiuse/wpad.h>
/*enum screen_exit_reason {
    SCREEN_NONE,
    SCREEN_WORLD_SELECTED,
    SCREEN_HOME_PRESSED
};*/

enum screen_exit_reason {
    SCREEN_NONE,
    SCREEN_WORLD_SELECTED,
    SCREEN_HOME_PRESSED
};

//#define exit_reason screen_exit_reason

static const char* menu_options[4] = {
    "Start",
    "Server",
    "Einstellungen",
    "Beenden"
};


static struct stack* worlds = NULL;

static size_t gui_selection;
static int scroll_offset;
static int wpad_pressed;

static int top_visible;
static int bottom_visible;
static int height_visible;
//static int entry_height = 72;
static int side_padding = 4;
//static int exit_reason;

struct world_option {
	string_t name;
	string_t directory;
	string_t path;
	int64_t last_access;
	int64_t byte_size;
};

static void screen_sworld_reset2(struct screen* s, int width, int height) { //TODO: rename


	gstate.game_run = false;
	gstate.num_players = 1;
	input_pointer_enable(true);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;
/*
	if(worlds) {
		while(!stack_empty(worlds)) {
			struct world_option opt;
			stack_pop(worlds, &opt);
			string_clear(opt.name);
			string_clear(opt.directory);
			string_clear(opt.path);
		}

		stack_destroy(worlds);
		free(worlds);
		worlds = NULL;
	}

	worlds = malloc(sizeof(struct stack));
	stack_create(worlds, 8, sizeof(struct world_option));

	const char* saves_path
		= config_read_string(&gstate.config_user, "paths.worlds", "saves");

	DIR* d = opendir(saves_path);

	if(d) {
		struct dirent* dir;
		while((dir = readdir(d))) {
			if(dir->d_type & DT_DIR && *dir->d_name != '.') {
				struct world_option opt;
				string_init_printf(opt.path, "%s/%s", saves_path, dir->d_name);

				struct level_archive la;
				if(level_archive_create(&la, opt.path)) {
					char name[64];

					if(!level_archive_read(&la, LEVEL_NAME, name, sizeof(name)))
						strcpy(name, "Missing name");

					string_init_set_str(opt.name, name);
					string_init_set_str(opt.directory, dir->d_name);

					if(!level_archive_read(&la, LEVEL_DISK_SIZE, &opt.byte_size,
										   0))
						opt.byte_size = 0;

					if(!level_archive_read(&la, LEVEL_LAST_PLAYED,
										   &opt.last_access, 0))
						opt.last_access = 0;

					opt.last_access /= 1000;

					level_archive_destroy(&la);
					stack_push(worlds, &opt);
				} else {
					string_clear(opt.path);
				}
			}
		}

		closedir(d);
	}
*/

	gui_selection = 0;
	scroll_offset = side_padding;
	top_visible = height * 0.133F;
	bottom_visible = height - 64;
	height_visible = bottom_visible - height * 0.133F;
	
}

static void screen_sworld_update2(struct screen* s, float dt) { //TODO: rename


/*    for (int ch = 0; ch < WPAD_MAX_WIIMOTES; ch++) {
        u32 wpad_pressed = WPAD_ButtonsDown(ch);
    }*/

    // Navigation
    if(input_pressed(IB_GUI_UP) && gui_selection > 0)
        gui_selection--;

    if(input_pressed(IB_GUI_DOWN) && gui_selection < 3)
        gui_selection++;

    // Aktion beim A-Knopf
    if(input_pressed(IB_GUI_CLICK)) {
        switch(gui_selection) {
            case 0: // Start 
			menu_screen_set(&spieleranzahl_auswählen);
            /*	menu_screen_set(&spieleranzahl_auswählen);
	        if (exit_reason == SCREEN_WORLD_SELECTED) {
		    exit_reason = SCREEN_NONE;
		    // zurück zu main.c
		} else if (exit_reason == SCREEN_HOME_PRESSED) {
		    exit_reason = SCREEN_NONE;
		    // Menü bleibt aktiv
		}
                break;*/
            case 1: // Server
                //server_menu();
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
    if(input_pressed(IB_HOME))
        gstate.quit = true;
}


static void screen_sworld_render2D2(struct screen* s, int width, int height) { //TODO: rename

gutil_bg();
//_3d_bg();


int start_y = height / 3; // Startposition Y
int line_height = 50;

// Highlight je nach Auswahl
if(gui_selection == 0)
    gutil_texquad_col((width-300)/2-10, start_y-5, 0,0,0,0, 320, line_height, 128,128,128,255);
else if(gui_selection == 1)
    gutil_texquad_col((width-300)/2-10, start_y + line_height-5, 0,0,0,0, 320, line_height, 128,128,128,255);
else if(gui_selection == 2)
    gutil_texquad_col((width-300)/2-10, start_y + 2*line_height-5, 0,0,0,0, 320, line_height, 128,128,128,255);
else if(gui_selection == 3)
    gutil_texquad_col((width-300)/2-10, start_y + 3*line_height-5, 0,0,0,0, 320, line_height, 128,128,128,255);

// Text für alle Optionen
for(int i=0; i<4; i++) {
    int y = start_y + i*line_height;
    gutil_text((width-300)/2, y, menu_options[i], 20, true);
}


// Steuerungs-Icons
int icon_offset = 32;
icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Quit");


/*    gutil_bg();

    int start_y = height / 3; // Startposition Y
    int line_height = 50;

    for(size_t i = 0; i < 4; i++) {
        int x = (width - 300) / 2; // Text zentrieren
        int y = start_y + i * line_height;

        // Highlight für ausgewählte Option
        if(gui_selection == i) {
            gutil_texquad_col(x-10, y-5, 0,0,0,0, 320, line_height, 128,128,128,255);
        }

        gutil_text(x, y, menu_options[i], 20, true);
    }

    // Steuerungs-Icons wie im Original
    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Quit");

*/

}

struct screen screen_select_world2 = { //TODO: rename
	.reset = screen_sworld_reset2,
	.update = screen_sworld_update2,
	.render2D = screen_sworld_render2D2,
	.render3D = NULL,
	.render_world = true,
};

