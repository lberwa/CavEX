#include <stdio.h>
#include <stdbool.h>
#include <wiiuse/wpad.h>

#include "../../platform/gfx.h"
#include "../game_state.h"
#include "screen_select_world.h" // falls du danach wechseln willst
#include "../../graphics/gui_util.h"
#include "screen.h"
#include "../../platform/gfx.h"
#include "../../graphics/gfx_util.h"

//static int position = 1;
//static int stand_position = 0;

float scroll_x = 0.0f;
float speed = 1.0f;

int w[12] = {256,256,256,256,256,59,
             256,256,256,256,256,59};

int h[12] = {256,256,256,256,256,256,
             194,194,194,194,194,194};

int total_width ;


void draw_panorama() {
	total_width = 0;
	for (int i=0; i<12; i++) {
	total_width += w[i];
	}

    float x = -scroll_x;

    for(int i=0; i<12; i++) {
        gfx_bind_texture(&texture_bg[i]);

        gutil_texquad(
            (int)x, 0,      // Position auf dem Bildschirm
            0, 0,           // Texture-Start
            w[i], h[i],     // Texture-Dimension
            w[i], h[i]     // Quad-Dimension
        );

        // Wrap-Kopie:
        gutil_texquad(
            (int)(x + total_width), 0,
            0, 0,
            w[i], h[i],
            w[i], h[i]
        );

        x += w[i];
    }

    scroll_x += speed;

    if(scroll_x >= total_width)
        scroll_x -= total_width;
}



#define MAX_BG_WIDTH 1338

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
static int wpad_pressed = 0;

// Aktuell ausgewählte Option
static int gui_selection = 0;

// Reset-Funktion: Initialisiert den Screen
static void screen_splayeranzahl_reset(struct screen* s, int width, int height) {


    gui_selection = 0;
    //input_pointer_enable(true);
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
	        screen_back();
		return;
		
	    }
    }
}

static int propo(int x, int y, int sx) {
    double um = sx/x;
    int sy = um * y;
    return sy;
}



// Render-Funktion: Zeigt die Optionen an
static void screen_spieleranzahl_auswählen_render2D(struct screen* s, int width, int height) {
    width = gfx_width();
    height = gfx_height();
	//if (position > MAX_BG_WIDTH)
	//	position = 1;

    gutil_bg();  // Hintergrund zeichnen

	/*gfx_bind_texture(&texture_bg);
	gutil_texquad(0, 0, 0, 0, 380, 216, width, height);
*/
/*
	gfx_bind_texture(&texture_bg);
	
	int sbg2z;
	int bg2z = 0;
	int bg2 = false;
	int bg_width = propo(height, width, 449);

	if (propo(height, width, 449) + position > MAX_BG_WIDTH) {
		bg_width = MAX_BG_WIDTH - position;
		bg2  	 = true;
		sbg2z	 = propo(height, width, 449) - (MAX_BG_WIDTH - position);
		bg2z  	 = bg_width;
	}

	gutil_texquad(0, 0, position, 0, bg_width, 449, width, height);

	if (bg2) {
		gutil_texquad(bg_width, 0, 0, 0, sbg2z, 449, bg2z, height);
	}

	if (stand_position > 100) {
		stand_position = 0;
		position++;
	}
	stand_position++;
*/

	draw_panorama();
	int line_height = 50;

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

	/*#ifdef NDEBUG
	char tmp[500];
    tmp[0] = '0' + stand_position;
    tmp[1] = '\0';

    gutil_text(120, 30, tmp, 20, true);

	tmp[500];
    tmp[0] = '0' + position;
    tmp[1] = '\0';

    gutil_text(120, 60, tmp, 20, true);
	#endif*/


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
