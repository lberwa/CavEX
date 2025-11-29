#include "../game_state.h"

#ifdef PLATFORM_WII

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "../../graphics/gui_util.h"

#include "screen.h"
#include "../../platform/input.h"
#include "../../platform/texture.h"
#include "../../graphics/gfx_util.h"
#include "../../platform/gfx.h"
#include "../../graphics/gui_util.h"

#define MAX_WIIMOTES 4

// -------------------------
// Globale Variablen
// -------------------------
static int controller_for_slot[4];  // 0 = leer, 1 = Wiimote 2 = wiimote + nunchuk, 3 = classic TODO: 4 = gc controller
static int wiimote_assigned[4];     // -1 = keine Wiimote, sonst Nummer 0–3
static int selected_menu = 0;
static int slot_count = 0;
static int gui_selection;

static const char* menu_options[2] = {
    "OK",
    "Ändern"
};

// -------------------------
// Reset-Funktion
// -------------------------
static void screen_controllerauswahl_reset(struct screen* s, int width, int height) {
    slot_count = gstate.num_players;   // Spieleranzahl übernehmen
    if(slot_count <= 0) slot_count = 1; // fallback

    // alle Slots frei
    for(int i=0; i<slot_count; i++) {
        controller_for_slot[i] = 0;
        wiimote_assigned[i] = -1;
    }

    selected_menu = 0;

}

// -------------------------
// Update-Funktion
// -------------------------
static void screen_controllerauswahl_update(struct screen* s, float dt) {
    // Slots automatisch zuweisen
    for(int ch = 0; ch < MAX_WIIMOTES; ch++) {
        u32 wpad_pressed = WPAD_ButtonsDown(ch);

        // UP / DOWN für Menüauswahl
        if(wpad_pressed & WPAD_BUTTON_UP) {
            selected_menu = (selected_menu - 1 + 2) % 2;
        }
        if(wpad_pressed & WPAD_BUTTON_DOWN) {
            selected_menu = (selected_menu + 1) % 2;
        }

        // B = zurück
        if(wpad_pressed & WPAD_BUTTON_B) {
            screen_back();
        }

        // A = Menü bestätigen
        if(wpad_pressed & WPAD_BUTTON_A) {
            bool all_filled = true;
            for(int i=0; i<slot_count; i++)
                if(controller_for_slot[i] == 0) all_filled = false;

            if(selected_menu == 0 && all_filled) {
                menu_screen_set(&screen_select_world);
                // Slots zurücksetzen für nächsten Besuch
                for(int i=0; i<slot_count; i++) {
                    controller_for_slot[i] = 0;
                    wiimote_assigned[i] = -1;
                }
            } else if(selected_menu == 1) {
                // Ändern: alles resetten
                for(int i=0; i<slot_count; i++) {
                    controller_for_slot[i] = 0;
                    wiimote_assigned[i] = -1;
                }
                return;
            }
        }
    
        
        if(wpad_pressed != 0) {
            // Prüfen, ob Wiimote schon in einem Slot zugewiesen
            bool already_assigned = false;
            for(int i=0;i<slot_count;i++) {
                if(wiimote_assigned[i]==ch) {
                    already_assigned = true;
                    break;
                }
            }
            if(!already_assigned) {
                // ersten freien Slot suchen
                for(int i=0;i<slot_count;i++) {
                    if(controller_for_slot[i]==0) {
                        

                        u32 type;
                        int status = WPAD_Probe(ch, &type);

                        if (status == WPAD_ERR_NONE) {

                            if (type == WPAD_EXP_NUNCHUK) { 
                                controller_for_slot[i] = 2; // Wiimote + Nunchuk

                            } else if (type == WPAD_EXP_CLASSIC) {
                                controller_for_slot[i] = 3; // Classic Controller    
                            
                            } else {
                                controller_for_slot[i] = 2; // Wiimote only
                            }
                        }

                        wiimote_assigned[i] = ch;   // Wiimote-Nummer speichern
                        break;
                    }
                }
            }
        }
    }
    gui_selection = selected_menu;
}    

static int propo(int x, int y, int sx) {
    double um = sx/x;
    int sy = um * y;
    return sy;
}


// -------------------------
// Render2D-Funktion
// -------------------------
static void screen_controllerauswahl_render2D(struct screen* s, int width, int height) {
    gutil_bg();

    gfx_bind_texture(&texture_bg);
	gutil_texquad(0, 0, 0, 0, 380, 216, width, height);


    // Menü rechts rendern
    int line_height = 50;
	int start_y = height / 3 + 15;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	for(int i = 0; i < 2; i++) {
	    int y = start_y + i * (button_height + button_spacing);

    	bool selected = (gui_selection == i);

    	// Texcoords für hell/dunkel
    	int tex_x = 0;
    	int tex_y = selected ? 62 : 42; // hell : dunkel
    	int tex_w = 200;
    	int tex_h = 20;

    	// Button skalieren auf 300x40
		gfx_bind_texture(&texture_gui2);
    	gutil_texquad(((width + width/2) - button_width) / 2, y, tex_x, tex_y, 
				  					 tex_w, tex_h, button_width, button_height);


		//int y = start_y + i*line_height;
    	gutil_text((((width + width/2) - button_width) / 2) 
                   + button_width/2 - (gutil_font_width(menu_options[i], 20)/2),
									         y + 10, menu_options[i], 20, true);
	}

    gfx_bind_texture(&texture_gui2);

    gutil_window(50, 50, width/2, height - 100, "Controller-Zuweisung");

    //slots
    for (int i=0; i<slot_count; i++) {

        int slotwidth = 150;
        int slotheight = 150;
        int slotspacing = 20;
        int start_x = 70;
        int start_y = 90;
        int slot_x = start_x;
        int slot_y = start_y;
        
        switch(i) {
            case 0:
                slot_x = start_x;
                slot_y = start_y;
                break;
            case 1:
                slot_x = start_x + slotwidth + slotspacing;
                slot_y = start_y;
                break;
            case 2:
                slot_x = start_x;
                slot_y = start_y + slotheight + slotspacing;
                break;
            case 3:
                slot_x = start_x + slotwidth + slotspacing;
                slot_y = start_y + slotheight + slotspacing;
                break;
        }
        
        char text[64];
        sprintf(text, "Spieler %d", i);

        gutil_window(slot_x, slot_y, slotwidth, slotheight, text);


        //icons
        if (controller_for_slot[i] == 1) //                                 wiimote
            gutil_texquad(slot_x + 10, slot_y + 10, 212 , 161, 212 - 155, 261 - 202, 
                          slotwidth - 20, propo(212 - 155, 261 - 202, slotwidth - 20));
        
        else if (controller_for_slot[i] == 2) //                            nunchuck
            gutil_texquad(slot_x + 10, slot_y + 10, 153 , 198, 195 - 153, 254 - 198, 
                          slotwidth - 20, propo(195 -153, 254 - 198, slotwidth - 20));

        else if (controller_for_slot[i] == 3) //                            classic
            gutil_texquad(slot_x + 10, slot_y + 10, 105, 215, 149 - 105, 242 - 215, 
                          slotwidth - 20, propo(149 - 150, 242 - 215, slotwidth - 20));
            
        else if (controller_for_slot[i] == 4) //                            gamecube
            gutil_texquad(slot_x + 10, slot_y + 10, 200 , 202, 249 - 200, 247 - 202, 
                          slotwidth - 20, propo(249 - 200, 247 - 202, slotwidth - 20));


        //number quads
        int number_quad_width = (82-37)*2;
        int number_quad_height = (129-117)*2;
        int number_shift;
        int number_start_x = (slot_x + (slotwidth/2 - number_quad_width/2)) + 5;
        int number_start_y = (slot_y + slotheight - number_quad_height - 3) + 4;
        int shift_plus = 30;

        if (!(wiimote_assigned[i] == -1)) {
            gfx_bind_texture(&texture_gui2);
            gutil_texquad(slot_x + (slotwidth/2 - number_quad_width/2), //x
                          slot_y + slotheight - number_quad_height - 3, //y
                          36, 117,                                      //sx sy
                          number_quad_width/2, number_quad_height/2,     //swidth sheight
                          number_quad_width, number_quad_height);        //width height
            
            number_shift = number_start_x + (wiimote_assigned[i] * shift_plus);
            gutil_texquad(number_shift, number_start_y, 38, 129, 46-38, 137-129,
                          (46-38)*2, (137-129)*2);
        } 
    }
        
    /*//debug informationen zu variable slot_count
    gutil_text(50, 30, "Slots:", 20, true);

    char tmp[2];
    tmp[0] = '0' + slot_count;
    tmp[1] = '\0';

    gutil_text(120, 30, tmp, 20, true);
    */    

    // Steuerungs-Icons
    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_DOWN, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Back");
}

// -------------------------
// Screen-Struktur
// -------------------------
struct screen screen_controllerauswahl = {
    .reset = screen_controllerauswahl_reset,
    .update = screen_controllerauswahl_update,
    .render2D = screen_controllerauswahl_render2D,
    .render3D = NULL,
    .render_world = false,
};
#endif /*PLATFORM_WII*/
