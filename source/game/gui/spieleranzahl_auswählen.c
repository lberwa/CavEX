#include <stdio.h>
#include <stdbool.h>
#include <wiiuse/wpad.h>
#include "../../platform/gfx.h"
#include "../game_state.h"
#include "screen_select_world.h" // falls du danach wechseln willst
#include "../../graphics/gui_util.h"
#include "screen.h"
#include "../../platform/gfx.h"

#define MAX_WIIMOTES 4

enum spieleranzahl_option {
    ONE_PLAYER = 0,
    TWO_PLAYERS,
    THREE_PLAYERS,
    FOUR_PLAYERS,
    PLAYER_COUNT
};

extern struct screen screen_controllerauswahl;
static int wpad_pressed = 0;

// Aktuell ausgewählte Option
static int gui_selection = 0;

// Reset-Funktion: Initialisiert den Screen
static void screen_splayeranzahl_reset(struct screen* s, int width, int height) {
/*
	width  = screenMode->fbWidth;    // EFB-Breite
	height = screenMode->efbHeight;  // EFB-Höhe

			int splitHeight = height / 2;    // Höhe des Splitscreens
			int yOffset = height*1/4;                  // für untere Hälfte: untere Kante = 0

			// Viewport & Scissor: untere Hälfte
			GX_SetViewport(0, height/2, width, height/2, 0, 1);
			
			//GX_SetScissor(0, yOffset, width, height - yOffset); // unten fehlt die Hälfte
			GX_SetScissor(0, height/2, width, height/2);


			// Display Copy Source: nur mittlere 50% der Welt (z.B. von 1/4 bis 3/4)
			GX_SetDispCopySrc(0, 0, width, height/2); // 		GX_SetDispCopySrc in libogc/libogc/gx.c:1999

			
            GX_SetDispCopyDst(width, splitHeight);

			//GX_SetScissorBoxOffset(0, -yOffset); // Kreuz oben am Rand, höhe bis hälfte, darunter Himmel
			GX_SetScissorBoxOffset(0, 0);

    
*/


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
	                // noch nicht implementiert
	                break;
	            case FOUR_PLAYERS:
	                // noch nicht implementiert
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

// Render-Funktion: Zeigt die Optionen an
static void screen_spieleranzahl_auswählen_render2D(struct screen* s, int width, int height) {
    width = gfx_width();
    height = gfx_height();

    gutil_bg();  // Hintergrund zeichnen

	//gfx_bind_texture(&texture_gui2);
	//gutil_texquad(0, 0, 0, 62, 122, 149-62, width, height);

    int start_y = height / 3;   // Start Y für erste Option
    int line_height = 50;       // Abstand zwischen den Optionen

    const char* menu_options[PLAYER_COUNT] = {
        "1 Spieler",
        "2 Spieler",
        "3 Spieler (nicht verfügbar)",
        "4 Spieler (nicht verfügbar)"
    };

    // Highlight nur für die aktuelle Auswahl
    int highlight_y = start_y + gui_selection * line_height;
    gutil_texquad_col((width-300)/2-10, highlight_y-5, 0,0,0,0, 320, line_height, 128,128,128,255);

    // Text für alle Optionen
    for(int i = 0; i < PLAYER_COUNT; i++) {
        int y = start_y + i * line_height;
        gutil_text((width-300)/2, y, (char*)menu_options[i], 20, true);
    }

	//gfx_bind_texture(&texture_gui2);
	//gutil_texquad(100, 100, 0, 43, 200, 62-43, 200, 19);

    // Steuerungs-Icons unten
    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_DOWN, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Quit");
}


// Globale Screen-Struktur
struct screen spieleranzahl_auswählen = {
    .reset = screen_splayeranzahl_reset,
    .update = screen_splayeranzahl_update,
    .render2D = screen_spieleranzahl_auswählen_render2D,
    .render3D = NULL,
    .render_world = false,
};
