#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "../../graphics/gui_util.h"
#include "../game_state.h"// variable mit spieleranzahl
#include "screen.h"
#include "../../platform/input.h"
#include "../../platform/texture.h"

#define MAX_WIIMOTES 4

// -------------------------
// Globale Variablen
// -------------------------
static int controller_for_slot[4];  // 0 = leer, 1 = Wiimote
static int wiimote_assigned[4];     // -1 = keine Wiimote, sonst Nummer 0–3
static int selected_menu = 0;       // 0 = OK, 1 = Ändern
static int slot_count = 0;
static int highlight_y;
extern struct game_state gstate;

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
                    controller_for_slot[i] = 1; // Wiimote
                    wiimote_assigned[i] = ch;   // Wiimote-Nummer speichern
                    break;
                }
            }
        }
    }    

    

    

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
            }
        }
    }
}

// -------------------------
// Render2D-Funktion
// -------------------------
static void screen_controllerauswahl_render2D(struct screen* s, int width, int height) {
    gutil_bg();

    // Slots rendern
    int slot_w = 128, slot_h = 128;
    int spacing = 150;
    int base_x = (width - (slot_count * spacing)) / 2;
    int y = height / 3;
    int line_height = 50; // für das Highlight-Rect
    
    // Wir berechnen vier feste Positionen (ax/ay, bx/by, cx/cy, dx/dy)
    // gleichmäßig verteilt, aber feste 4-Position-Grid (nur gezeichnet wenn slot_count verlangt)
    int start_x = (width - (3 * spacing + slot_w)) / 2; // Start für 4 mögliche slots
    int ax = start_x;
    int bx = start_x + spacing;
    int cx = start_x + spacing * 2;
    int dx = start_x + spacing * 3;
    int ay = height / 3;
    int by = ay;
    int cy = ay;
    int dy = ay;




    // Hilfs-Makro zum Zeichnen des Rahmens und Textes pro Slot-Index
    // Wir schreiben die vier Blöcke explizit (unrolled) damit nichts überlappt.

    // ===== Slot 0 (immer zeichnen) =====
    {
        int x = ax; int y_slot = ay;
        // Rahmen
        gutil_texquad_col(x, y_slot, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);

        // Text in der Mitte
        if (controller_for_slot[0] == 1)
            gutil_text(x + 10, y_slot + 40, (char*)"WIIMOTE", 20, true);
        else
            gutil_text(x + 10, y_slot + 40, (char*)"EMPTY", 20, true);

        // 4 Kästchen darunter (Wiimote 0..3)
        for (int j = 0; j < 4; j++) {
            int bnx = x + j * 20;
            int bny = y_slot + slot_h + 10;
            bool filled = (controller_for_slot[0] == 1 && wiimote_assigned[0] == j);
            if (filled) gutil_texquad_col(bnx, bny, 0,0,0,0, 16,16, 200,200,200,255);
            else {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny+14, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny, 0,0,0,0, 2,16, 200,200,200,255);
                gutil_texquad_col(bnx+14, bny, 0,0,0,0, 2,16, 200,200,200,255);
            }
        }

        // Debug: "Variable: Zahl" style
        char tmp[4];
        gutil_text(x, y_slot-60, (char*)"Slot:", 20, true);
        tmp[0] = '0' + 0; tmp[1] = '\0'; gutil_text(x + 60, y_slot-60, tmp, 20, true);

        gutil_text(x, y_slot-40, (char*)"Controller:", 20, true);
        tmp[0] = '0' + controller_for_slot[0]; tmp[1] = '\0'; gutil_text(x + 110, y_slot-40, tmp, 20, true);

        gutil_text(x, y_slot-20, (char*)"Wiimote:", 20, true);
        if (wiimote_assigned[0] >= 0) { tmp[0] = '0' + wiimote_assigned[0]; tmp[1] = '\0'; }
        else { tmp[0] = '-'; tmp[1] = '\0'; }
        gutil_text(x + 80, y_slot-20, tmp, 20, true);
    }

    // ===== Slot 1 (nur wenn slot_count > 1) =====
    if (slot_count > 1) {
        int x = bx; int y_slot = by;
        gutil_texquad_col(x, y_slot, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);

        if (controller_for_slot[1] == 1)
            gutil_text(x + 10, y_slot + 40, (char*)"WIIMOTE", 20, true);
        else
            gutil_text(x + 10, y_slot + 40, (char*)"EMPTY", 20, true);

        for (int j = 0; j < 4; j++) {
            int bnx = x + j * 20;
            int bny = y_slot + slot_h + 10;
            bool filled = (controller_for_slot[1] == 1 && wiimote_assigned[1] == j);
            if (filled) gutil_texquad_col(bnx, bny, 0,0,0,0, 16,16, 200,200,200,255);
            else {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny+14, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny, 0,0,0,0, 2,16, 200,200,200,255);
                gutil_texquad_col(bnx+14, bny, 0,0,0,0, 2,16, 200,200,200,255);
            }
        }

        char tmp[4];
        gutil_text(x, y_slot-60, (char*)"Slot:", 20, true);
        tmp[0] = '0' + 1; tmp[1] = '\0'; gutil_text(x + 60, y_slot-60, tmp, 20, true);

        gutil_text(x, y_slot-40, (char*)"Controller:", 20, true);
        tmp[0] = '0' + controller_for_slot[1]; tmp[1] = '\0'; gutil_text(x + 110, y_slot-40, tmp, 20, true);

        gutil_text(x, y_slot-20, (char*)"Wiimote:", 20, true);
        if (wiimote_assigned[1] >= 0) { tmp[0] = '0' + wiimote_assigned[1]; tmp[1] = '\0'; }
        else { tmp[0] = '-'; tmp[1] = '\0'; }
        gutil_text(x + 80, y_slot-20, tmp, 20, true);
    }

    // ===== Slot 2 (nur wenn slot_count > 2) =====
    if (slot_count > 2) {
        int x = cx; int y_slot = cy;
        gutil_texquad_col(x, y_slot, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);

        if (controller_for_slot[2] == 1)
            gutil_text(x + 10, y_slot + 40, (char*)"WIIMOTE", 20, true);
        else
            gutil_text(x + 10, y_slot + 40, (char*)"EMPTY", 20, true);

        for (int j = 0; j < 4; j++) {
            int bnx = x + j * 20;
            int bny = y_slot + slot_h + 10;
            bool filled = (controller_for_slot[2] == 1 && wiimote_assigned[2] == j);
            if (filled) gutil_texquad_col(bnx, bny, 0,0,0,0, 16,16, 200,200,200,255);
            else {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny+14, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny, 0,0,0,0, 2,16, 200,200,200,255);
                gutil_texquad_col(bnx+14, bny, 0,0,0,0, 2,16, 200,200,200,255);
            }
        }

        char tmp[4];
        gutil_text(x, y_slot-60, (char*)"Slot:", 20, true);
        tmp[0] = '0' + 2; tmp[1] = '\0'; gutil_text(x + 60, y_slot-60, tmp, 20, true);

        gutil_text(x, y_slot-40, (char*)"Controller:", 20, true);
        tmp[0] = '0' + controller_for_slot[2]; tmp[1] = '\0'; gutil_text(x + 110, y_slot-40, tmp, 20, true);

        gutil_text(x, y_slot-20, (char*)"Wiimote:", 20, true);
        if (wiimote_assigned[2] >= 0) { tmp[0] = '0' + wiimote_assigned[2]; tmp[1] = '\0'; }
        else { tmp[0] = '-'; tmp[1] = '\0'; }
        gutil_text(x + 80, y_slot-20, tmp, 20, true);
    }

    // ===== Slot 3 (nur wenn slot_count > 3) =====
    if (slot_count > 3) {
        int x = dx; int y_slot = dy;
        gutil_texquad_col(x, y_slot, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y_slot, 0,0,0,0, 4, slot_h, 200,200,200,255);

        if (controller_for_slot[3] == 1)
            gutil_text(x + 10, y_slot + 40, (char*)"WIIMOTE", 20, true);
        else
            gutil_text(x + 10, y_slot + 40, (char*)"EMPTY", 20, true);

        for (int j = 0; j < 4; j++) {
            int bnx = x + j * 20;
            int bny = y_slot + slot_h + 10;
            bool filled = (controller_for_slot[3] == 1 && wiimote_assigned[3] == j);
            if (filled) gutil_texquad_col(bnx, bny, 0,0,0,0, 16,16, 200,200,200,255);
            else {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny+14, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny, 0,0,0,0, 2,16, 200,200,200,255);
                gutil_texquad_col(bnx+14, bny, 0,0,0,0, 2,16, 200,200,200,255);
            }
        }

        char tmp[4];
        gutil_text(x, y_slot-60, (char*)"Slot:", 20, true);
        tmp[0] = '0' + 3; tmp[1] = '\0'; gutil_text(x + 60, y_slot-60, tmp, 20, true);

        gutil_text(x, y_slot-40, (char*)"Controller:", 20, true);
        tmp[0] = '0' + controller_for_slot[3]; tmp[1] = '\0'; gutil_text(x + 110, y_slot-40, tmp, 20, true);

        gutil_text(x, y_slot-20, (char*)"Wiimote:", 20, true);
        if (wiimote_assigned[3] >= 0) { tmp[0] = '0' + wiimote_assigned[3]; tmp[1] = '\0'; }
        else { tmp[0] = '-'; tmp[1] = '\0'; }
        gutil_text(x + 80, y_slot-20, tmp, 20, true);
    }
    
    char buf[64];

	// Slot 0 Debug
	snprintf(buf, sizeof(buf), "Slot0: x=%d y=%d", ax, ay);
	gutil_text(20, 20, buf, 16, true);
	
	// Slot 1 Debug
	snprintf(buf, sizeof(buf), "Slot1: x=%d y=%d", bx, by);
	gutil_text(20, 40, buf, 16, true);
	
	// Slot 2 Debug
	snprintf(buf, sizeof(buf), "Slot2: x=%d y=%d", cx, cy);
	gutil_text(20, 60, buf, 16, true);
	
	// Slot 3 Debug
	snprintf(buf, sizeof(buf), "Slot3: x=%d y=%d", dx, dy);
	gutil_text(20, 80, buf, 16, true);

    
    
/*
    for(int i=0; i<slot_count; i++) {
        int x = base_x + i*spacing;
        int y_slot = y;
	
	
	if (i ==0) {
        // Slot-Rahmen
        gutil_texquad_col(x, y, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y, 0,0,0,0, 4, slot_h, 0,200,0,255);
        } else {
        // Slot-Rahmen
        gutil_texquad_col(x, y, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y+slot_h-4, 0,0,0,0, slot_w, 4, 200,200,200,255);
        gutil_texquad_col(x, y, 0,0,0,0, 4, slot_h, 200,200,200,255);
        gutil_texquad_col(x+slot_w-4, y, 0,0,0,0, 4, slot_h, 200,0,0,255);
        }
        // Wiimote Text
        if(controller_for_slot[i] == 1) {
            gutil_text(x+20, y+40, "WIIMOTE", 1, true);
        } else {
            gutil_text(x+20, y+40, "EMPTY", 1, true);
        }
	
        // Kästchen für Wiimotes 1–4
        for(int j=0; j<4; j++) {
            int bnx = x + j*20;
            int bny = y + slot_h + 10;

            bool filled = (controller_for_slot[i] == 1 && wiimote_assigned[i] == j);

            if(filled) {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,16, 200,200,200,255);
            } else {
                gutil_texquad_col(bnx, bny, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny+14, 0,0,0,0, 16,2, 200,200,200,255);
                gutil_texquad_col(bnx, bny, 0,0,0,0, 2,16, 200,200,200,255);
                gutil_texquad_col(bnx+14, bny, 0,0,0,0, 2,16, 200,200,200,255);
            }
        }
        // --- DEBUG: Slot-Info direkt sichtbar ---
	// Slot-Nummer
	gutil_text(x, y-60, "Slot:", 20, true);
	char tmp[2];
	tmp[0] = '0' + i;
	tmp[1] = '\0';
	gutil_text(x + 60, y-60, tmp, 20, true);
	
	// Controller-Typ
	gutil_text(x, y-40, "Controller:", 20, true);
	tmp[0] = '0' + controller_for_slot[i];
	tmp[1] = '\0';
	gutil_text(x + 100, y-40, tmp, 20, true);
			
	// Wiimote-Nummer
	gutil_text(x, y-20, "Wiimote:", 20, true);
	if(wiimote_assigned[i] >= 0) tmp[0] = '0' + wiimote_assigned[i];
	else tmp[0] = '-';
	tmp[1] = '\0';
	gutil_text(x + 80, y-20, tmp, 20, true);
	//debug ende
    }*/
    

    // Menü rechts rendern
    int mx = base_x + slot_count*spacing + 50;
    int my = y;

    int filled_count = 0;
    for(int i=0;i<slot_count;i++) if(controller_for_slot[i] != 0) filled_count++;
    bool ok_active = (filled_count == slot_count);

    // OK
    gutil_texquad_col(mx, my, 0,0,0,0, 120,40,
                      selected_menu==0?255:150,
                      200,200,180);
    // Ändern
    gutil_texquad_col(mx, my+60, 0,0,0,0, 120,40,
                      selected_menu==1?255:150,
                      200,200,180);
    
    // Highlight-Rahmen (wie im Spieleranzahl-Screen)
/*    int highlight_y;
    if (selected_menu == 0)
        highlight_y = my;
    else
        highlight_y = my + 60;*/
        
    // Highlight wie im Spieleranzahl-Screen
    int highlight_y = (selected_menu == 0) ? my : (my + 60);
    gutil_texquad_col((width-300)/2 - 10, highlight_y - 5, 0,0,0,0, 320, line_height, 128,128,128,255);

        
    gutil_texquad_col(mx - 10, highlight_y - 5, 0,0,0,0,
                  320, 40, 128,128,128,128);
    
    gutil_text(mx+10, my+10, "OK", 20, true);
    gutil_text(mx+10, my+70, "Ändern", 20, true);
    
    //debug informationen zu variable slot_count
    gutil_text(50, 30, "Slots:", 20, true);

    char tmp[2];
    tmp[0] = '0' + slot_count;
    tmp[1] = '\0';

    gutil_text(120, 30, tmp, 20, true);
    

    
    

    // Steuerungs-Icons
    int icon_offset = 32;
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_DOWN, "Change selection");
    icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");
    icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Quit");
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

