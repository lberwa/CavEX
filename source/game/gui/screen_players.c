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

#include <stdio.h>
#include <stdbool.h>

#include "../../platform/gfx.h"
#include "../game_state.h"
#include "../../graphics/gui_util.h"
#include "screen.h"
#include "../../platform/gfx.h"
#include "../../graphics/gfx_util.h"

#include "../../sound/sound.h"
#include "../../network/server_comunication.h"

#define MAX_WIIMOTES 4

enum spieleranzahl_option {
    ONE_PLAYER = 0,
    TWO_PLAYERS,
    THREE_PLAYERS,
    FOUR_PLAYERS,
    PLAYER_COUNT
};

static const char* menu_options[4] = {
    "1",
    "2",
    "3",
    "4"
};

extern struct screen screen_controllerauswahl;
static int gui_selection = 0;

static void player_count_select(int option) {
	sound_play(pcm_click);
	gstate.num_players = option + 1;
	if(option < TWO_PLAYERS + 1)
		menu_screen_set(&screen_controllerauswahl);
}
/*
static enum mp3_sound bg_playlist[16] = {
	mp3_bg1,
	mp3_bg2,
	mp3_bg3,
	mp3_bg4,
	mp3_bg5,
	mp3_bg6,
	mp3_bg7,
	mp3_bg8,
};
*/
// Reset-Funktion: Initialisiert den Screen
static void screen_splayeranzahl_reset(struct screen* s, int width, int height) {
	gutil_button_new_menu();

    input_pointer_enable(true);
	/*
	debug_send("Reset Spieleranzahl Screen");
	sound_init();

	debug_send("Spiele Hintergrundmusik im Spieleranzahl Screen");
	sound_play_bg(bg_playlist);
	*/
}

// Update-Funktion: Handhabt Navigation und Auswahl
static void screen_splayeranzahl_update(struct screen* s, float dt) {
	if(input_pressed(IB_BACK, 0))
		screen_back();
}

// Render-Funktion: Zeigt die Optionen an
static void screen_spieleranzahl_auswählen_render2D(struct screen* s, int width, int height) {
    gutil_bg();  // Hintergrund zeichnen
	gutil_bg_panorama();

	int start_y = height / 3;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	float pointer_x, pointer_y, pointer_angle;
	bool pointer_available = screen_pointer_local(0, width, height,
										 &pointer_x, &pointer_y, &pointer_angle);
	gutil_button_reset(0, pointer_available, pointer_x, pointer_y);
	for(int i = 0; i < 4; i++) {
	    int y = start_y + i * (button_height + button_spacing);
		gutil_button((width - button_width) / 2, y, button_width, button_height,
					 menu_options[i], player_count_select, i, 0, i);
	}
	gutil_button_update();
	gutil_button_render();

	gutil_license(width, height);

    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Back");

	float x, y, a;
	bool avaiable = screen_pointer_local(0, width, height, &x, &y, &a);

	if(avaiable) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(x, y, glm_rad(a), 0, 0, 256, 256, 
							 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}


// Globale Screen-Struktur
struct screen spieleranzahl_auswählen = {
    .reset = screen_splayeranzahl_reset,
    .update = screen_splayeranzahl_update,
    .render2D = screen_spieleranzahl_auswählen_render2D,
    .render3D = NULL,
    .render_world = false,
};
