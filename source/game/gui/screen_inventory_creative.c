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

#include <limits.h>
#include <stdio.h>

#include "../../graphics/gfx_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/render_model.h"
#include "../../item/items.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../platform/texture.h"
#include "../../platform/time.h"
#include "../game_state.h"
#include "screen.h"
#include "screen_inventory_creative.h"

// Jede Sektion der PNG ist 390x272 Pixel.
// Angezeigte GUI-Größe in "virtuellen" Pixeln (halbe Texturauflösung).
#define GUI_WIDTH  195
#define GUI_HEIGHT 136

// Textur-Sektionen in der creative-inventory PNG (Pixelkoordinaten)
#define TEX_W 390
#define TEX_H 272

#define INVENTORY_Y_OFFSET -30
#define INVENTORY_X_OFFSET 1

// Tab-Buttons (Textur-Bereich ab x=390 in der PNG)
// Reihenfolge in der Textur: links(390), mitte(442), rechts(494) — ausgewählt
// Nicht-ausgewählt: x += 156 = (442-390)*3
// Obere Reihe (über Inventar): tex_y += TAB_BTN_H
#define TAB_BTN_W      52
#define TAB_BTN_H      64
#define TAB_DISP_W     26    // halbe Texturauflösung
#define TAB_DISP_H     32
#define TAB_OVERLAP     4    // Pixel Überlappung mit Inventar-Rand
#define TAB_UNSEL_XOFF 156
#define TAB_ROW_SIZE    6    // Buttons pro Reihe (oben / unten)
// Aktive Tabs (haben tatsächlich Inhalt): Positionen 0..TAB_ACTIVE-1 (unten)
#define TAB_ACTIVE      3
// Schrittweite zwischen Buttons (= TAB_DISP_W für lückenlos, größer für Abstand)
#define TAB_BTN_OFFSET  2
#define TAB_BTN_STEP    (TAB_DISP_W + TAB_BTN_OFFSET)

// Gesamthöhe inkl. beider Tab-Überhänge — für Screen-Scale-Berechnung
#define TAB_OVERHANG   (TAB_DISP_H - TAB_OVERLAP)   // = 28 virt. Pixel
#define GUI_TOTAL_H    (GUI_HEIGHT + 2 * TAB_OVERHANG)  // = 192

struct inv_slot {
	int x, y;
	size_t slot;
	bool is_virtual;
	uint16_t virtual_item;
};

static bool pointer_has_item[4];
static bool pointer_available[4];
static float pointer_x[4], pointer_y[4], pointer_angle[4];
static struct inv_slot slots[64]; // 5*9 virtuelle + 9 Hotbar + Rüstung
static size_t slots_index;
static size_t selected_slot[4];
static int gui_scale[4];

static creative_inv_tab current_tab;
static int  sel_tab_pos = 5;   // welcher Button-Slot (0-5) gerade ausgewählt ist
static bool sel_tab_top = false; // true = obere Reihe

// Textur-x für Button-Typ an Position pos (0=links, 5=rechts, sonst mitte)
static int tab_tex_x(int pos, bool selected) {
	int base = (pos == 0) ? 390 : (pos == TAB_ROW_SIZE - 1) ? 494 : 442;
	return selected ? base : base + TAB_UNSEL_XOFF;
}

// Screen-x eines Tab-Buttons.
// Unten: nur ganz rechts (pos 5) ist rechts-ausgerichtet.
// Oben:  die 2 rechten (pos 4, 5) sind rechts-ausgerichtet.
static int tab_screen_x(int pos, bool top, int off_x, int scale) {
	bool right_aligned = top ? (pos >= TAB_ROW_SIZE - 2)
	                         : (pos == TAB_ROW_SIZE - 1);
	if(right_aligned) {
		int from_right = TAB_ROW_SIZE - 1 - pos; // 0=ganz rechts, 1=einer links davon
		return off_x + (GUI_WIDTH - TAB_DISP_W - from_right * TAB_BTN_STEP) * scale;
	}
	return off_x + pos * TAB_BTN_STEP * scale;
}

// Category item lists — terminated by 0
// top row (sel_tab_top=true), positions 0–5
// t0: 🧱 Baumaterialien
static const uint16_t tab_list_t0[] = {
    1,2,3,4,5,7,12,13,14,15,16,17,19,24,35,
    41,42,43,44,45,48,49,56,57,82,87,88,89,0};
// t1: 🛋️ Dekorationsblöcke
static const uint16_t tab_list_t1[] = {
    6,18,20,30,31,32,37,38,39,40,50,51,52,53,54,
    78,79,81,83,84,86,91,96,0};
// t2: ⚙️ Redstone
static const uint16_t tab_list_t2[] = {
    23,25,27,29,33,55,69,70,72,73,74,75,76,77,94,331,356,0};
// t3: 🛒 Transport
static const uint16_t tab_list_t3[] = {27,28,66,328,333,342,343,0};
// t4: leer (holzbretter hat keine eigene Kategorie)
static const uint16_t tab_list_t4[] = {0};
// t5: leer
static const uint16_t tab_list_t5[] = {0};
// bottom row (sel_tab_top=false), positions 0–5
// b0: 🧪 Materialien / Zutaten
static const uint16_t tab_list_b0[] = {
    263,264,265,266,280,281,287,288,289,296,
    318,334,336,337,351,352,353,0};
// b1: 🍎 Lebensmittel
static const uint16_t tab_list_b1[] = {
    260,282,297,319,320,322,335,344,349,350,354,357,0};
// b2: 🛠️ Werkzeuge & Nützliches
static const uint16_t tab_list_b2[] = {
    65,256,257,258,259,261,262,267,268,269,270,271,
    272,273,274,275,276,277,278,279,283,284,285,286,
    290,291,292,293,294,323,325,326,327,339,340,
    345,346,347,355,358,359,0};
// b3: ⚔️ Kampf & Rüstung
static const uint16_t tab_list_b3[] = {
    261,262,267,268,272,276,283,
    298,299,300,301,302,303,304,305,
    306,307,308,309,310,311,312,313,314,315,316,317,0};
// b4: 🎵 Sonstiges
static const uint16_t tab_list_b4[] = {2256,2257,0};
// pos 5 bottom = CHEST — NULL

static const uint16_t* tab_item_list(void) {
	if(sel_tab_top) {
		switch(sel_tab_pos) {
			case 0: return tab_list_t0;
			case 1: return tab_list_t1;
			case 2: return tab_list_t2;
			case 3: return tab_list_t3;
			case 4: return tab_list_t4;
			case 5: return tab_list_t5;
		}
	} else {
		switch(sel_tab_pos) {
			case 0: return tab_list_b0;
			case 1: return tab_list_b1;
			case 2: return tab_list_b2;
			case 3: return tab_list_b3;
			case 4: return tab_list_b4;
			case 5: return NULL; // CHEST
		}
	}
	return NULL;
}

static void screen_inventory_creative_reset(struct screen* s, int width,
											int height) {
	int player = gstate_active_player();
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	int scale = screen_gui_scale(view_w, view_h, GUI_WIDTH, GUI_TOTAL_H);
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);

	s->render3D = screen_ingame.render3D;

	pointer_available[player] = false;
	pointer_has_item[player] = false;
	gui_scale[player] = scale;

	slots_index = 0;

	const uint16_t* vlist = tab_item_list();

	if(vlist == NULL) {
		// CHEST tab: 3 rows of main + hotbar + armor (all real slots)
		for(int k = 0; k < INVENTORY_SIZE_MAIN; k++) {
			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8 + (k % INVENTORY_SIZE_HOTBAR) * 18) * scale,
				.y = (INVENTORY_Y_OFFSET + 84 + (k / INVENTORY_SIZE_HOTBAR) * 18) * scale,
				.slot = k + INVENTORY_SLOT_MAIN,
				.is_virtual = false,
				.virtual_item = 0,
			};
		}

		for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
			if(k == (int)inventory_get_hotbar(
				   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY])))
				selected_slot[player] = slots_index;

			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8 + k * 18) * scale,
				.y = (INVENTORY_Y_OFFSET + 84 + 3 * 18 + 4) * scale,
				.slot = k + INVENTORY_SLOT_HOTBAR,
				.is_virtual = false,
				.virtual_item = 0,
			};
		}

		for(int k = 0; k < INVENTORY_SIZE_ARMOR; k++) {
			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8) * scale,
				.y = (INVENTORY_Y_OFFSET + 8 + k * 18) * scale,
				.slot = k + INVENTORY_SLOT_ARMOR,
				.is_virtual = false,
				.virtual_item = 0,
			};
		}
	} else {
		// Non-CHEST tab: 5 rows x 9 virtual slots + hotbar (real)

		// Build filtered list of valid item IDs from vlist
		// Count valid entries first to determine total slots
		size_t list_len = 0;
		for(size_t i = 0; vlist[i] != 0; i++) {
			uint16_t id = vlist[i];
			if(id > 0 && id < ITEMS_MAX && items[id] && items[id]->renderItem)
				list_len++;
		}

		// Build filtered array inline while filling slots
		size_t filtered_idx = 0;
		// Pre-collect valid ids (max 5*9 = 45)
		uint16_t filtered[45];
		size_t filtered_count = 0;
		for(size_t i = 0; vlist[i] != 0 && filtered_count < 45; i++) {
			uint16_t id = vlist[i];
			if(id > 0 && id < ITEMS_MAX && items[id] && items[id]->renderItem)
				filtered[filtered_count++] = id;
		}

		for(int row = 0; row < 5; row++) {
			for(int col = 0; col < 9; col++) {
				int list_idx = row * 9 + col;
				uint16_t id = (list_idx < (int)filtered_count) ? filtered[list_idx] : 0;
				slots[slots_index++] = (struct inv_slot) {
					.x = (INVENTORY_X_OFFSET + 8 + col * 18) * scale,
					.y = (INVENTORY_Y_OFFSET + 84 - 2 * 18 + row * 18) * scale,
					.slot = 0,
					.is_virtual = true,
					.virtual_item = id,
				};
			}
		}

		// Hotbar slots (real)
		size_t first_hotbar = slots_index;
		for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
			if(k == (int)inventory_get_hotbar(
				   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY])))
				selected_slot[player] = slots_index;

			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8 + k * 18) * scale,
				.y = (INVENTORY_Y_OFFSET + 84 + 3 * 18 + 4) * scale,
				.slot = k + INVENTORY_SLOT_HOTBAR,
				.is_virtual = false,
				.virtual_item = 0,
			};
		}
		// Ensure selected_slot points to hotbar if not set above
		if(selected_slot[player] >= slots_index)
			selected_slot[player] = first_hotbar;
	}
}

static void screen_inventory_creative_update(struct screen* s, float dt) {
	int player = gstate_active_player();
	int scale = gui_scale[player];
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);

	bool pressed       = input_pressed(IB_GUI_CLICK,     player);
	bool pressed_alt   = input_pressed(IB_GUI_CLICK_ALT, player);
	bool pressed_inv   = input_pressed(IB_INVENTORY,     player);

	if(pressed_inv) {
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_WINDOW_CLOSE,
			.payload.window_close.window = WINDOWC_INVENTORY,
		});
		screen_set_player(player, &screen_ingame);
	}

	if(pressed && (!pointer_available[player] || pointer_has_item[player])) {
		struct inv_slot* sel = &slots[selected_slot[player]];
		if(sel->is_virtual) {
			if(sel->virtual_item > 0) {
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_CREATIVE_SET_PICKED,
					.payload.creative_set_picked.item_id = sel->virtual_item,
				});
			}
		} else {
			uint16_t action_id;
			if(windowc_new_action(gstate_windows()[WINDOWC_INVENTORY], &action_id,
								  false, sel->slot)) {
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_WINDOW_CLICK,
					.payload.window_click.window = WINDOWC_INVENTORY,
					.payload.window_click.action_id = action_id,
					.payload.window_click.right_click = false,
					.payload.window_click.slot = sel->slot,
				});
			}
		}
	} else if(pressed_alt && (!pointer_available[player] || pointer_has_item[player])) {
		struct inv_slot* sel = &slots[selected_slot[player]];
		if(sel->is_virtual) {
			if(sel->virtual_item > 0) {
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_CREATIVE_SET_PICKED,
					.payload.creative_set_picked.item_id = sel->virtual_item,
				});
			}
		} else {
			uint16_t action_id;
			if(windowc_new_action(gstate_windows()[WINDOWC_INVENTORY], &action_id,
								  true, sel->slot)) {
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_WINDOW_CLICK,
					.payload.window_click.window = WINDOWC_INVENTORY,
					.payload.window_click.action_id = action_id,
					.payload.window_click.right_click = true,
					.payload.window_click.slot = sel->slot,
				});
			}
		}
	}

	pointer_available[player]
		= screen_pointer_local(player, view_w, view_h, &pointer_x[player],
							   &pointer_y[player], &pointer_angle[player]);

	//printf("ptr avail=%d x=%.1f y=%.1f clicked=%d\n",
	//	   pointer_available[player], pointer_x[player], pointer_y[player],
	//	   pressed);

	size_t slot_nearest[4]
		= {selected_slot[player], selected_slot[player],
		   selected_slot[player], selected_slot[player]};
	int slot_dist[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
	int pointer_slot = -1;

	int off_x = (view_w - GUI_WIDTH    * scale) / 2;
	int off_y = (view_h - GUI_TOTAL_H  * scale) / 2 + TAB_OVERHANG * scale;

	// Tab-Klick per Mauszeiger
	// Ganz rechts oben  (pos 5, obere Reihe) → SEARCH
	// Ganz rechts unten (pos 5, untere Reihe) → CHEST
	// Alle anderen                            → ITEMS
	bool tab_changed = false;
	if(pointer_available[player] && pressed) {
		int tab_bottom_y_scr = off_y + (GUI_HEIGHT - TAB_OVERLAP) * scale;
		int tab_top_y_scr    = off_y - TAB_OVERHANG * scale;
		for(int t = 0; t < TAB_ROW_SIZE; t++) {
			int tab_x_scr = tab_screen_x(t, false, off_x, scale);
			int tab_x_top_scr = tab_screen_x(t, true, off_x, scale);
			bool in_x     = pointer_x[player] >= tab_x_scr
					     && pointer_x[player] <  tab_x_scr + TAB_DISP_W * scale;
			bool in_x_top = pointer_x[player] >= tab_x_top_scr
					     && pointer_x[player] <  tab_x_top_scr + TAB_DISP_W * scale;
			bool in_bottom = in_x
						  && pointer_y[player] >= tab_bottom_y_scr
						  && pointer_y[player] <  tab_bottom_y_scr + TAB_DISP_H * scale;
			bool in_top    = in_x_top
						  && pointer_y[player] >= tab_top_y_scr
						  && pointer_y[player] <  tab_top_y_scr    + TAB_DISP_H * scale;
			if(!in_bottom && !in_top) continue;
			if(in_bottom) {
				current_tab = (t == TAB_ROW_SIZE - 1)
					? CREATIVE_TAB_CHEST : CREATIVE_TAB_ITEMS;
				sel_tab_pos = t;
				sel_tab_top = false;
				tab_changed = true;
				break;
			}
			if(in_top) {
				current_tab = (t == TAB_ROW_SIZE - 1)
					? CREATIVE_TAB_SEARCH : CREATIVE_TAB_ITEMS;
				sel_tab_pos = t;
				sel_tab_top = true;
				tab_changed = true;
				break;
			}
		}
	}
	if(tab_changed) {
		screen_inventory_creative_reset(s, view_w, view_h);
		return;
	}

	for(size_t k = 0; k < slots_index; k++) {
		int dx = slots[k].x - slots[selected_slot[player]].x;
		int dy = slots[k].y - slots[selected_slot[player]].y;

		if(pointer_x[player] >= off_x + slots[k].x
		   && pointer_x[player] < off_x + slots[k].x + 16 * scale
		   && pointer_y[player] >= off_y + slots[k].y
		   && pointer_y[player] < off_y + slots[k].y + 16 * scale)
			pointer_slot = k;

		int distx = dx * dx + dy * dy * 8;
		int disty = dx * dx * 8 + dy * dy;

		if(dx < 0 && distx < slot_dist[0]) {
			slot_nearest[0] = k;
			slot_dist[0] = distx;
		}
		if(dx > 0 && distx < slot_dist[1]) {
			slot_nearest[1] = k;
			slot_dist[1] = distx;
		}
		if(dy < 0 && disty < slot_dist[2]) {
			slot_nearest[2] = k;
			slot_dist[2] = disty;
		}
		if(dy > 0 && disty < slot_dist[3]) {
			slot_nearest[3] = k;
			slot_dist[3] = disty;
		}
	}

	if(pointer_available[player] && pointer_slot >= 0) {
		selected_slot[player] = pointer_slot;
		pointer_has_item[player] = true;
	} else {
		pointer_has_item[player] = false;
		if(input_pressed(IB_GUI_LEFT, player)) {
			selected_slot[player] = slot_nearest[0];
			pointer_has_item[player] = false;
		}
		if(input_pressed(IB_GUI_RIGHT, player)) {
			selected_slot[player] = slot_nearest[1];
			pointer_has_item[player] = false;
		}
		if(input_pressed(IB_GUI_UP, player)) {
			selected_slot[player] = slot_nearest[2];
			pointer_has_item[player] = false;
		}
		if(input_pressed(IB_GUI_DOWN, player)) {
			selected_slot[player] = slot_nearest[3];
			pointer_has_item[player] = false;
		}
	}
}

static void screen_inventory_creative_render2D(struct screen* s, int width,
											   int height) {
	struct inventory* inv
		= windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]);
	int player = gstate_active_player();
	int scale = gui_scale[player];
	gutil_set_gui_scale(scale);

	// Hintergrund abdunkeln
	gfx_texture(false);
	gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 180);
	gfx_texture(true);

	int off_x = (width  - GUI_WIDTH    * scale) / 2;
	// off_y zeigt auf die Oberkante des Inventar-Panels (oberhalb der oberen Tabs)
	int off_y = (height - GUI_TOTAL_H * scale) / 2 + TAB_OVERHANG * scale;

	int tab_top_y    = off_y - TAB_OVERHANG * scale;  // obere Tab-Reihe
	int tab_bottom_y = off_y + (GUI_HEIGHT - TAB_OVERLAP) * scale;

	// Textur-Sektion je nach aktivem Tab
	int tex_x, tex_y;
	switch(current_tab) {
		default:
		case CREATIVE_TAB_CHEST:  tex_x = 0;     tex_y = 0;     break;
		case CREATIVE_TAB_ITEMS:  tex_x = 0;     tex_y = TEX_H; break;
		case CREATIVE_TAB_SEARCH: tex_x = TEX_W; tex_y = TEX_H; break;
	}

	gfx_bind_texture(&texture_gui_creative_inventory);

	// Alle unausgewählten Tabs zuerst (liegen hinter dem Inventar-Rand)
	for(int t = 0; t < TAB_ROW_SIZE; t++) {
		if(!(t == sel_tab_pos && !sel_tab_top))
			gutil_texquad(tab_screen_x(t, false, off_x, scale), tab_bottom_y,
						  tab_tex_x(t, false), 0,
						  TAB_BTN_W, TAB_BTN_H,
						  TAB_DISP_W * scale, TAB_DISP_H * scale);
		if(!(t == sel_tab_pos && sel_tab_top))
			gutil_texquad(tab_screen_x(t, true, off_x, scale), tab_top_y,
						  tab_tex_x(t, false), TAB_BTN_H,
						  TAB_BTN_W, TAB_BTN_H,
						  TAB_DISP_W * scale, TAB_DISP_H * scale);
	}

	// Inventar-Hintergrund
	gutil_texquad(off_x, off_y, tex_x, tex_y, TEX_W, TEX_H,
				  GUI_WIDTH * scale, GUI_HEIGHT * scale);

	// Ausgewählten Button vorne zeichnen
	{
		int row_y  = sel_tab_top ? tab_top_y : tab_bottom_y;
		int tex_ty = sel_tab_top ? TAB_BTN_H : 0;
		gutil_texquad(tab_screen_x(sel_tab_pos, sel_tab_top, off_x, scale), row_y,
					  tab_tex_x(sel_tab_pos, true), tex_ty,
					  TAB_BTN_W, TAB_BTN_H,
					  TAB_DISP_W * scale, TAB_DISP_H * scale);
	}

	// Items auf Tab-Buttons
	// Oben (0-5): stonebrick, blume, redstone, schiene, holzbretter, grass
	// Unten (0-5): lava-eimer, apfel, eisen-axt, gold-schwert, eisen-helm, truhe
	static const uint16_t tab_top_items[TAB_ROW_SIZE]    = {98, 37, 331, 66,  5,   2};
	static const uint16_t tab_bottom_items[TAB_ROW_SIZE] = {327, 260, 258, 283, 306, 54};
	{
		struct item_data ti = {.durability = 0, .count = 1};
		int item_off_x = (TAB_DISP_W / 2 - 8) * scale;
		int item_off_y = (TAB_DISP_H / 2 - 8) * scale;
		for(int t = 0; t < TAB_ROW_SIZE; t++) {
			ti.id = tab_top_items[t];
			gutil_draw_item(&ti,
							tab_screen_x(t, true,  off_x, scale) + item_off_x,
							tab_top_y    + item_off_y, 1);
			ti.id = tab_bottom_items[t];
			gutil_draw_item(&ti,
							tab_screen_x(t, false, off_x, scale) + item_off_x,
							tab_bottom_y + item_off_y, 1);
		}
	}

	// Items in Slots zeichnen
	for(size_t k = 0; k < slots_index; k++) {
		if(slots[k].is_virtual) {
			if(slots[k].virtual_item > 0) {
				struct item_data vitem = {
					.id = slots[k].virtual_item,
					.count = 1,
					.durability = 0,
				};
				gutil_draw_item(&vitem, off_x + slots[k].x, off_y + slots[k].y, 1);
			}
		} else {
			struct item_data item;
			if((selected_slot[player] != k
				|| !inventory_get_picked_item(inv, NULL)
				|| pointer_available[player])
			   && inventory_get_slot(inv, slots[k].slot, &item))
				gutil_draw_item(&item, off_x + slots[k].x, off_y + slots[k].y, 1);
		}
	}

	// Slot-Auswahl-Cursor
	struct inv_slot* selection = slots + selected_slot[player];
	if(!pointer_available[player] || pointer_has_item[player]) {
		gfx_bind_texture(&texture_gui2);
		gutil_texquad(off_x + selection->x - 4 * scale,
					  off_y + selection->y - 4 * scale,
					  208, 0, 24, 24, 24 * scale, 24 * scale);
	}

	int icon_offset = 16 * scale;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Bewegen");
	if(selection->is_virtual) {
		if(selection->virtual_item > 0)
			icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Aufnehmen");
	} else if(inventory_get_picked_item(inv, NULL)) {
		icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Tauschen");
		icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Einzeln");
	} else if(inventory_get_slot(inv, selection->slot, NULL)) {
		icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Aufnehmen");
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Teilen");
	}
	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Schließen");

	struct item_data item;
	if(inventory_get_picked_item(inv, &item)) {
		if(pointer_available[player]) {
			gutil_draw_item(&item, pointer_x[player] - 8 * scale,
							pointer_y[player] - 8 * scale, 0);
		} else {
			gutil_draw_item(&item, off_x + selection->x,
							off_y + selection->y, 0);
		}
	} else if(selection->is_virtual && selection->virtual_item > 0) {
		struct item_data vsel = {
			.id = selection->virtual_item,
			.count = 1,
			.durability = 0,
		};
		const char* tmp = item_get_name(&vsel);
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gutil_texquad_col(
			off_x + selection->x - 2 * scale + 8 * scale
				- gutil_font_width(tmp, 8 * scale) / 2,
			off_y + selection->y - 2 * scale + 23 * scale, 0, 0, 0, 0,
			gutil_font_width(tmp, 8 * scale) + 7, 12 * scale, 0, 0, 0, 180);
		gfx_texture(true);
		gfx_blending(MODE_OFF);
		gutil_text(
			off_x + selection->x + 8 * scale
				- gutil_font_width(tmp, 8 * scale) / 2,
			off_y + selection->y + 23 * scale, tmp, 8 * scale, false);
	} else if(!selection->is_virtual && inventory_get_slot(inv, selection->slot, &item)) {
		const char* tmp = item_get_name(&item);
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gutil_texquad_col(
			off_x + selection->x - 2 * scale + 8 * scale
				- gutil_font_width(tmp, 8 * scale) / 2,
			off_y + selection->y - 2 * scale + 23 * scale, 0, 0, 0, 0,
			gutil_font_width(tmp, 8 * scale) + 7, 12 * scale, 0, 0, 0, 180);
		gfx_texture(true);
		gfx_blending(MODE_OFF);
		gutil_text(
			off_x + selection->x + 8 * scale
				- gutil_font_width(tmp, 8 * scale) / 2,
			off_y + selection->y + 23 * scale, tmp, 8 * scale, false);
	}

	if(pointer_available[player]) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(pointer_x[player], pointer_y[player],
							 glm_rad(pointer_angle[player]), 0, 0, 256, 256,
							 48 * scale, 48 * scale);
	}

	gutil_set_gui_scale(GFX_GUI_SCALE);
}

creative_inv_tab creative_inventory_get_tab(void) {
	return current_tab;
}

void creative_inventory_set_tab(creative_inv_tab tab) {
	current_tab = tab;
}

struct screen screen_inventory_creative = {
	.reset    = screen_inventory_creative_reset,
	.update   = screen_inventory_creative_update,
	.render2D = screen_inventory_creative_render2D,
	.render3D = NULL,
	.render_world = true,
};
