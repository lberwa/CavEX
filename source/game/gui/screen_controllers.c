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

#include "../game_state.h"

//#ifdef PLATFORM_WII

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../graphics/gui_util.h"

#include "screen.h"
#include "../../platform/input.h"
#include "../../platform/texture.h"
#include "../../graphics/gfx_util.h"
#include "../../platform/gfx.h"
#include "../../graphics/gui_util.h"
#include "../../sound/sound.h"

#define MAX_WIIMOTES 4

// -------------------------
// Globale Variablen
// -------------------------
static int controller_for_slot[4];  // 0 = leer, 1 = Wiimote 2 = wiimote + nunchuk, 3 = classic TODO: 4 = gc controller
static int wiimote_num_slot[4] = { -1, -1, -1, -1 };
static int slot_count = 0;
//int gstate.player_sequence[4];  // -1 = keine Wiimote, sonst Nummer 0–3

static const char* menu_options[2] = {
    "OK",
    "Change"
};

static void controller_menu_select(int option) {
    if(option == 0) {
        bool all_filled = true;
        for(int i = 0; i < slot_count; i++)
            if(controller_for_slot[i] == 0)
                all_filled = false;
        if(all_filled) {
            sound_play(pcm_click);
            menu_screen_set(&screen_select_world);
        }
        return;
    }

    sound_play(pcm_click);
    for(int i = 0; i < slot_count; i++) {
        controller_for_slot[i] = 0;
        wiimote_num_slot[i] = -1;
    }
    for(int i = 1; i < 4; i++)
        gstate.player_sequence[i] = -1;
    gstate.player_sequence[0] = 0;
}

// -------------------------
// Reset-Funktion
// -------------------------
static void screen_controllerauswahl_reset(struct screen* s, int width, int height) {
    gutil_button_new_menu();

    slot_count = gstate.num_players;   // Spieleranzahl übernehmen
    if(slot_count <= 0) slot_count = 1; // fallback

}

// -------------------------
// Update-Funktion
// -------------------------
static void screen_controllerauswahl_update(struct screen* s, float dt) {
    (void)s;
    (void)dt;
    if(input_pressed(IB_BACK, 0)) {
        screen_back();
        sound_play(pcm_click);
    }

    for(int ch = 0; ch < MAX_WIIMOTES; ch++) {
        if(rinput_pressed(IB_ANY, ch)) {
            bool already_assigned = false;
            for(int i = 0; i < slot_count; i++) {
                if(wiimote_num_slot[i] == ch) {
                    already_assigned = true;
                    break;
                }
            }
            if(!already_assigned) {
                for(int i = 0; i < slot_count; i++) {
                    if(controller_for_slot[i] == 0) {
                        controller_for_slot[i] = which_controller(ch) == 0 ? 2 : 3;
                        gstate.player_sequence[i] = ch;
                        wiimote_num_slot[i] = ch;
                        break;
                    }
                }
            }
        }
    }
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
    gutil_bg_panorama();

    int button_width = 300;
    int button_height = 40;
    int button_x = width / 2 + (width / 2 - button_width) / 2;
    int button_y = height / 3 + 15;
    float pointer_x, pointer_y, pointer_angle;
    bool pointer_available = screen_pointer_local(0, width, height,
                                                   &pointer_x, &pointer_y,
                                                   &pointer_angle);
    gutil_button_reset(0, pointer_available, pointer_x, pointer_y);
    for(int i = 0; i < 2; i++)
        gutil_button(button_x, button_y + i * 60, button_width, button_height,
                     menu_options[i], controller_menu_select, i, 0, i);
    gutil_button_update();
    gutil_button_render();

    gfx_bind_texture(&texture_gui2);

    gutil_window(50, 50, width/2, height - 100, "Controller Assignment");

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
		sprintf(text, "Player %d", i+1);

        gutil_window(slot_x, slot_y, slotwidth, slotheight, text);

        gfx_bind_texture(&texture_gui2);

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

        if (!(wiimote_num_slot[i] == -1)) {
            gfx_bind_texture(&texture_gui2);
            gutil_texquad(slot_x + (slotwidth/2 - number_quad_width/2), //x
                          slot_y + slotheight - number_quad_height - 3, //y
                          36, 117,                                      //sx sy
                          number_quad_width/2, number_quad_height/2,     //swidth sheight
                          number_quad_width, number_quad_height);        //width height
            
            number_shift = number_start_x + (wiimote_num_slot[i] * shift_plus);
            gutil_texquad(number_shift, number_start_y, 38, 129, 46-38, 137-129,
                          (46-38)*2, (137-129)*2);
        } 
    } 

    gutil_license(width, height);

    // Steuerungs-Icons
    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_DOWN, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Back");


#if 0

    char buf[128];  // Jede Zeile separat

    int y = 20; // Start-Y
    int line_height = 20; // Abstand zwischen den Zeilen

    // Header
    snprintf(buf, sizeof(buf), "=== Slots Status ===");
    gutil_text(20, y, buf, 20, false);
    y += line_height;

    // Slots
    for(int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf),
                 "Slot %d: controller=%d, wiimote_num=%d",
                 i, controller_for_slot[i], wiimote_num_slot[i]);
        gutil_text(20, y, buf, 20, false);
        y += line_height;
    }

    // GUI & Menü
    snprintf(buf, sizeof(buf),
             "selected_menu=%d, slot_count=%d, gui_selection=%d",
             selected_menu, slot_count, gui_selection);
    gutil_text(20, y, buf, 20, false);
    y += line_height;

    // gstate.player_sequence
    snprintf(buf, sizeof(buf), "player_sequence: %d %d %d %d",
             gstate.player_sequence[0],
             gstate.player_sequence[1],
             gstate.player_sequence[2],
             gstate.player_sequence[3]);
    gutil_text(20, y, buf, 20, false);


#endif

    float x, y, a;
	bool avaiable = screen_pointer_local(0, width, height, &x, &y, &a);

	if(avaiable) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(x, y, glm_rad(a), 0, 0, 256, 256, 
							 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
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
//#endif /*PLATFORM_WII*/
