#include <stdio.h>
#include <stdbool.h>
#include <wiiuse/wpad.h>

#include "../../platform/gfx.h"
#include "../game_state.h"
#include "../../graphics/gui_util.h"
#include "screen.h"
#include "../../platform/gfx.h"
#include "../../graphics/gfx_util.h"

#include "../../sound.h"
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
	mp3_bg9,
	mp3_bg10,
};
*/
// Reset-Funktion: Initialisiert den Screen
static void screen_splayeranzahl_reset(struct screen* s, int width, int height) {
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
   
	for (int p = 0; p < MAX_WIIMOTES; p++) {
	    u32 wpad_pressed = WPAD_ButtonsDown(p);
	
	    // Navigation nach oben
	    if ((wpad_pressed & WPAD_BUTTON_UP) && gui_selection > 0) {
	        gui_selection--;
	    }
	
	    // Navigation nach unten
	    if ((wpad_pressed & WPAD_BUTTON_DOWN) && gui_selection < PLAYER_COUNT - 1) {
	        gui_selection++;
	    }
		
    
	    // Auswahl mit A
	    if((wpad_pressed & WPAD_BUTTON_A)) {
			sound_play(pcm_click);
	        switch(gui_selection) {
	            case ONE_PLAYER:
	                gstate.num_players = 1;
	                menu_screen_set(&screen_controllerauswahl); // nächster Screen z. B. Controller-Zuweisung
	                break;
	            case TWO_PLAYERS:
	                gstate.num_players = 2;
	                menu_screen_set(&screen_controllerauswahl);
	                break;
	            case THREE_PLAYERS:
	                gstate.num_players = 3;
	                menu_screen_set(&screen_controllerauswahl);
	                break;
	            case FOUR_PLAYERS:
	                gstate.num_players = 4;
	                menu_screen_set(&screen_controllerauswahl);
	                break;
	        }
	    }
	
	
	    // Home-Button zum Abbrechen
	    if (wpad_pressed & WPAD_BUTTON_B) {
			sound_play(pcm_click);
	        screen_back();
		return;
		
	    }
    }
}

// Render-Funktion: Zeigt die Optionen an
static void screen_spieleranzahl_auswählen_render2D(struct screen* s, int width, int height) {
    gutil_bg();  // Hintergrund zeichnen
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
    icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Back");
}


// Globale Screen-Struktur
struct screen spieleranzahl_auswählen = {
    .reset = screen_splayeranzahl_reset,
    .update = screen_splayeranzahl_update,
    .render2D = screen_spieleranzahl_auswählen_render2D,
    .render3D = NULL,
    .render_world = false,
};
