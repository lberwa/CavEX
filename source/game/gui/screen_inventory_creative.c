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
#include <ctype.h>
#include <string.h>

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
#define DELETE_SLOT_X (10 + 9 * 18)
#define DELETE_SLOT_Y (84 + 3 * 18 + 4)
/* Rüstung liegt oberhalb der ersten Item-Reihe. Die beiden Spalten sitzen
 * jeweils genau in den Lücken zwischen den angegebenen Item-Spalten. */
#define ARMOR_LEFT_X  (8 + 3 * 18 - 9)   /* zwischen Spalte 3 und 4 */
#define ARMOR_RIGHT_X (8 + 6 * 18 - 9)   /* zwischen Spalte 6 und 7 */
/* Positionen der zusätzlichen Rüstungsslots im Chest-Menü.
 * Diese drei Werte kannst du direkt ändern:
 *   ARMOR_ROW_1_Y = obere Rüstungszeile (Helm/Hose)
 *   ARMOR_ROW_2_Y = untere Rüstungszeile (Brust/Schuhe)
 *   ARMOR_PLAYER_Y = Höhe der Spielerfigur
 */
#define ARMOR_ROW_1_Y (84 - 2 * 18 - 12)  /* 40 */
#define ARMOR_ROW_2_Y (84 - 1 * 18 - 3)  /* 62 */
#define ARMOR_PLAYER_Y (ARMOR_ROW_2_Y - 8)

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
#define SEARCH_BAR_Y   (5)   // ursprüngliche Position 10, 15 Pixel höher
#define SEARCH_BAR_X   (5 * 18 - 11)
#define SEARCH_BAR_WIDTH  (4 * 18 + 18)
#define SEARCH_BAR_HEIGHT 10
#define SCROLL_TRACK_H (5 * 18 + 20) // bis zur unteren Hotbar-Kante
#define SCROLL_X_OFFSET 4    // Abstand rechts vom letzten Item-Slot
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
	bool is_saved;
	bool is_delete;
	uint16_t virtual_item;
};

static bool pointer_has_item[4];
static bool pointer_available[4];
static float pointer_x[4], pointer_y[4], pointer_angle[4];
static bool pointer_keyboard_mode[4];
static bool pointer_position_valid[4];
static float pointer_last_x[4], pointer_last_y[4];
static struct inv_slot slots[64]; // 5*9 virtuelle + 9 Hotbar + Rüstung
static size_t slots_index;
static size_t selected_slot[4];
static int gui_scale[4];

static creative_inv_tab current_tab;
static int  sel_tab_pos = 5;   // welcher Button-Slot (0-5) gerade ausgewählt ist
static bool sel_tab_top = false; // true = obere Reihe
static bool tab_focus;
static char search_query[64];
#define CREATIVE_SAVED_SIZE 45
static struct item_data saved_items[CREATIVE_SAVED_SIZE];
static bool saved_sync_requested;
static uint16_t creative_item_list[ITEMS_MAX];
static size_t creative_item_count;
static size_t creative_scroll;
static size_t creative_max_scroll(void);
static void creative_normalize_scroll(void);
static bool text_contains_ignore_case(const char* text, const char* query);

static void creative_add_armor_slots(void) {
	/* Inventory armor order: helmet, chestplate, leggings, boots. */
	slots[slots_index++] = (struct inv_slot) {
		.x = (INVENTORY_X_OFFSET + ARMOR_LEFT_X) * gui_scale[gstate_active_player()],
		.y = (INVENTORY_Y_OFFSET + ARMOR_ROW_1_Y) * gui_scale[gstate_active_player()],
		.slot = INVENTORY_SLOT_ARMOR + 0,
		.is_virtual = false,
	};
	slots[slots_index++] = (struct inv_slot) {
		.x = (INVENTORY_X_OFFSET + ARMOR_LEFT_X) * gui_scale[gstate_active_player()],
		.y = (INVENTORY_Y_OFFSET + ARMOR_ROW_2_Y) * gui_scale[gstate_active_player()],
		.slot = INVENTORY_SLOT_ARMOR + 1,
		.is_virtual = false,
	};
	slots[slots_index++] = (struct inv_slot) {
		.x = (INVENTORY_X_OFFSET + ARMOR_RIGHT_X) * gui_scale[gstate_active_player()],
		.y = (INVENTORY_Y_OFFSET + ARMOR_ROW_1_Y) * gui_scale[gstate_active_player()],
		.slot = INVENTORY_SLOT_ARMOR + 2,
		.is_virtual = false,
	};
	slots[slots_index++] = (struct inv_slot) {
		.x = (INVENTORY_X_OFFSET + ARMOR_RIGHT_X) * gui_scale[gstate_active_player()],
		.y = (INVENTORY_Y_OFFSET + ARMOR_ROW_2_Y) * gui_scale[gstate_active_player()],
		.slot = INVENTORY_SLOT_ARMOR + 3,
		.is_virtual = false,
	};
}

static void creative_item_append(uint16_t id, const char* query) {
	if(id == 0 || id >= ITEMS_MAX || !items[id] || !items[id]->renderItem)
		return;
	struct item_data item = {.id = id, .count = 1};
	if(query && !text_contains_ignore_case(item_get_name(&item), query))
		return;
	for(size_t k = 0; k < creative_item_count; k++)
		if(creative_item_list[k] == id) return;
	if(creative_item_count < ITEMS_MAX)
		creative_item_list[creative_item_count++] = id;
}

static bool text_contains_ignore_case(const char* text, const char* query) {
	if(!query[0]) return true;
	for(const char* p = text; *p; p++) {
		const char* a = p;
		const char* b = query;
		while(*a && *b && tolower((unsigned char)*a)
			== tolower((unsigned char)*b)) { a++; b++; }
		if(!*b) return true;
	}
	return false;
}

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

// Category item lists — terminated by 0. Only items registered by this game
// can be shown; unsupported Minecraft items are intentionally omitted.
// Top row (sel_tab_top=true), positions 0–5. Positions 4 and 5 stay open
// (position 5 is reserved for search).
// t0: Building Blocks
static const uint16_t tab_list_t0[] = {
    1,2,3,4,5,7,12,13,14,15,16,17,19,20,21,22,24,35,
    41,42,44,45,47,48,49,53,56,57,67,79,80,82,86,
    87,88,89,98,108,109,112,114,121,126,128,0};
// t1: Decoration Blocks
static const uint16_t tab_list_t1[] = {
    6,18,30,31,32,37,38,39,40,50,54,58,61,65,81,83,84,85,
    101,102,106,107,113,116,120,130,355,0};
// t2: Redstone
static const uint16_t tab_list_t2[] = {
    23,25,29,33,46,55,69,70,72,75,76,77,93,96,107,123,124,131,
    137,324,330,331,0};
// t3: Transportation
static const uint16_t tab_list_t3[] = {27,28,66,328,329,333,0};
// t4/t5: open
static const uint16_t tab_list_t4[] = {0};
static const uint16_t tab_list_t5[] = {0};
// Bottom row (sel_tab_top=false), positions 0–5.
// b0: Miscellaneous
static const uint16_t tab_list_b0[] = {
    263,264,265,266,280,287,288,289,295,318,325,326,327,332,334,
    335,336,337,338,339,340,341,344,348,351,352,353,361,362,388,426,
    427,0};
// b1: Foodstuffs
static const uint16_t tab_list_b1[] = {
    260,282,297,319,320,322,349,350,354,357,360,0};
// b2: Tools
static const uint16_t tab_list_b2[] = {
    256,257,258,259,269,270,271,273,274,275,277,278,279,284,285,
    286,290,291,292,293,294,345,346,347,359,0};
// b3: Combat
static const uint16_t tab_list_b3[] = {
    261,262,267,268,272,276,283,
    298,299,300,301,302,303,304,305,
    306,307,308,309,310,311,312,313,314,315,316,317,0};
// b4: Brewing
static const uint16_t tab_list_b4[] = {117,118,0};
// pos 5 bottom = CHEST — NULL

static const uint16_t* tab_item_list(void) {
	if(current_tab == CREATIVE_TAB_SEARCH || current_tab == CREATIVE_TAB_SAVED)
		return NULL;
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
	/* The Wii's per-player GUI viewport can report a reduced logical height
	 * while the creative inventory is still rendered in the full GUI pass.
	 * Falling back to scale 1 makes the item tabs visibly smaller than the
	 * chest tab. Keep the Wii creative inventory at the regular GUI scale. */
#ifdef PLATFORM_WII
	scale = GFX_GUI_SCALE;
#endif
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);

	s->render3D = screen_ingame.render3D;

	pointer_available[player] = false;
	pointer_has_item[player] = false;
	gui_scale[player] = scale;

	slots_index = 0;
	tab_focus = false;

	const uint16_t* vlist = tab_item_list();

	if(current_tab == CREATIVE_TAB_SAVED) {
		if(!saved_sync_requested) {
			svin_rpc_send(&(struct server_rpc) {
				RPC_PLAYER_ID(player)
				.type = SRPC_CREATIVE_SAVED_REQUEST,
			});
			saved_sync_requested = true;
		}
		for(int k = 0; k < CREATIVE_SAVED_SIZE; k++) {
			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8 + (k % 9) * 18) * scale,
				.y = (INVENTORY_Y_OFFSET + 48 + (k / 9) * 18) * scale,
				.slot = k, .is_saved = true,
			};
		}
		for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
			slots[slots_index++] = (struct inv_slot) {
				.x = (INVENTORY_X_OFFSET + 8 + k * 18) * scale,
				.y = (INVENTORY_Y_OFFSET + 84 + 3 * 18 + 4) * scale,
				.slot = k + INVENTORY_SLOT_HOTBAR,
			};
		}
	} else if(current_tab == CREATIVE_TAB_CHEST) {
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
		/* Item placed here is discarded. */
		slots[slots_index++] = (struct inv_slot) {
			.x = (INVENTORY_X_OFFSET + DELETE_SLOT_X) * scale,
			.y = (INVENTORY_Y_OFFSET + DELETE_SLOT_Y) * scale,
			.is_delete = true,
		};

		creative_add_armor_slots();
	} else {
		// Non-CHEST tab: 5 rows x 9 virtual slots + hotbar (real)

		/* Build the complete list. Search only uses the normal creative tabs. */
		creative_item_count = 0;
		if(current_tab == CREATIVE_TAB_SEARCH) {
			static const uint16_t* lists[] = {
				tab_list_t0, tab_list_t1, tab_list_t2, tab_list_t3,
				tab_list_b0, tab_list_b1, tab_list_b2, tab_list_b3,
				tab_list_b4
			};
			for(size_t l = 0; l < sizeof(lists) / sizeof(lists[0]); l++)
				for(size_t i = 0; lists[l][i] != 0; i++)
					creative_item_append(lists[l][i], search_query);
		} else {
			for(size_t i = 0; vlist && vlist[i] != 0; i++)
				creative_item_append(vlist[i], NULL);
		}
		size_t max_scroll = creative_max_scroll();
		if(creative_scroll > max_scroll) creative_scroll = max_scroll;
		creative_normalize_scroll();

		for(int row = 0; row < 5; row++) {
			for(int col = 0; col < 9; col++) {
				int list_idx = row * 9 + col;
				size_t item_idx = creative_scroll + (size_t)list_idx;
				uint16_t id = item_idx < creative_item_count
					? creative_item_list[item_idx] : 0;
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

static void creative_saved_action(int player, struct inventory* player_inv,
								  size_t slot, bool right) {
	struct inventory saved = {0};
	saved.items = saved_items;
	saved.capacity = CREATIVE_SAVED_SIZE;
	saved.picked_item = player_inv->picked_item;
	if(inventory_action(&saved, slot, right, NULL)) {
		inventory_set_picked_item(player_inv, saved.picked_item);
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_CREATIVE_SAVED_SLOT,
			.payload.creative_saved_slot.slot = (uint8_t)slot,
			.payload.creative_saved_slot.item = saved_items[slot],
		});
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_CREATIVE_SET_PICKED_STACK,
			.payload.creative_set_picked_stack.item = saved.picked_item,
		});
	}
}

static void creative_select_tab(bool top, int pos) {
	sel_tab_top = top;
	sel_tab_pos = pos;
	creative_scroll = 0;
	if(top && pos == 5) current_tab = CREATIVE_TAB_SEARCH;
	else if(top && pos == 4) {
		current_tab = CREATIVE_TAB_SAVED;
		saved_sync_requested = false;
	}
	else if(!top && pos == 5) current_tab = CREATIVE_TAB_CHEST;
	else current_tab = CREATIVE_TAB_ITEMS;
}

static void creative_cycle_tab(int direction) {
	/* Follow every visible tab, including bookshelf, compass and chest. */
	static const bool top[12] = {
		true,true,true,true,true,true,
		false,false,false,false,false,false
	};
	static const int pos[12] = {0,1,2,3,4,5,0,1,2,3,4,5};
	int current = 0;
	for(int i = 0; i < 12; i++)
		if(sel_tab_top == top[i] && sel_tab_pos == pos[i]) current = i;
	current = (current + direction + 12) % 12;
	creative_select_tab(top[current], pos[current]);
}

static size_t creative_max_scroll(void) {
	if(creative_item_count <= 45) return 0;
	/* Pages always advance by nine. The last page is allowed to contain
	 * fewer than nine items, leaving the remaining cells empty. */
	return ((creative_item_count - 1) / 9) * 9;
}

static size_t creative_visible_rows(void) {
	size_t remaining = creative_item_count > creative_scroll
		? creative_item_count - creative_scroll : 0;
	size_t rows = (remaining + 8) / 9;
	return rows > 5 ? 5 : rows;
}

static void creative_normalize_scroll(void) {
	size_t max_scroll = creative_max_scroll();
	creative_scroll = (creative_scroll / 9) * 9;
	if(creative_scroll > max_scroll)
		creative_scroll = max_scroll;
}

static void creative_refresh_pointer(int player, int view_w, int view_h) {
	bool available = screen_pointer_local(
		player, view_w, view_h, &pointer_x[player], &pointer_y[player],
		&pointer_angle[player]);
	if(available) {
		if(pointer_position_valid[player]
		   && (pointer_x[player] != pointer_last_x[player]
			   || pointer_y[player] != pointer_last_y[player]))
			pointer_keyboard_mode[player] = false;
		pointer_last_x[player] = pointer_x[player];
		pointer_last_y[player] = pointer_y[player];
		pointer_position_valid[player] = true;
	}
	pointer_available[player] = available && !pointer_keyboard_mode[player];
	pointer_has_item[player] = pointer_available[player];
}

static const char* creative_fit_text(const char* name, int scale,
									int max_width) {
	static char shortened[128];
	if(gutil_font_width(name, 8 * scale) <= max_width)
		return name;

	for(size_t length = strlen(name); length > 0; length--) {
		if(length + 3 >= sizeof(shortened)) continue;
		memcpy(shortened, name, length);
		memcpy(shortened + length, "...", 4);
		if(gutil_font_width(shortened, 8 * scale) <= max_width)
			return shortened;
	}
	return "...";
}

static void creative_draw_selected_name(int x, int y, const char* name,
										int scale) {
	const char* text = name;
	int text_width = gutil_font_width(text, 8 * scale);
	int box_width = text_width + 7;
	int box_x = x + 8 * scale - text_width / 2 - 2 * scale;
	int box_y = y + 21 * scale;
	gfx_blending(MODE_BLEND);
	gfx_texture(false);
	gutil_texquad_col(box_x, box_y, 0, 0, 0, 0,
					  box_width, 12 * scale, 0, 0, 0, 180);
	gfx_texture(true);
	gfx_blending(MODE_OFF);
	gutil_text(box_x + (box_width - text_width) / 2,
			   box_y + 2 * scale, text, 8 * scale, false);
}

static void screen_inventory_creative_update(struct screen* s, float dt) {
	int player = gstate_active_player();
	int scale = gui_scale[player];
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	bool pressed       = input_pressed(IB_GUI_CLICK,     player);
	bool pressed_alt   = input_pressed(IB_GUI_CLICK_ALT, player);
	bool pressed_inv   = input_pressed(IB_INVENTORY,     player);
	if(input_pressed(IB_GUI_LEFT, player)
	   || input_pressed(IB_GUI_RIGHT, player)
	   || input_pressed(IB_GUI_UP, player)
	   || input_pressed(IB_GUI_DOWN, player))
		pointer_keyboard_mode[player] = true;
	/* Use the same completed bindings as the in-game hotbar. */
	if(input_pressed(IB_SCROLL_LEFT, player)) {
		creative_cycle_tab(-1);
		screen_inventory_creative_reset(s, view_w, view_h);
		return;
	}
	if(input_pressed(IB_SCROLL_RIGHT, player)) {
		creative_cycle_tab(1);
		screen_inventory_creative_reset(s, view_w, view_h);
		return;
	}

	/* Down/up on the last/first visible row scrolls the item grid instead of
	 * jumping directly to the hotbar. */
	if(current_tab != CREATIVE_TAB_CHEST
	   && (input_pressed(IB_GUI_DOWN, player)
		   || input_pressed(IB_GUI_UP, player))
	   && selected_slot[player] < 45
	   && slots[selected_slot[player]].is_virtual) {
		int row = (int)selected_slot[player] / 9;
		int col = (int)selected_slot[player] % 9;
		if(input_pressed(IB_GUI_DOWN, player) && row == 4) {
			if(creative_scroll < creative_max_scroll()) {
			creative_scroll += 9;
			if(creative_scroll > creative_max_scroll())
				creative_scroll = creative_max_scroll();
			screen_inventory_creative_reset(s, view_w, view_h);
			selected_slot[player] = 4 * 9 + col;
			creative_refresh_pointer(player, view_w, view_h);
			return;
			}
		}
		if(input_pressed(IB_GUI_UP, player) && row == 0 && creative_scroll > 0) {
			creative_scroll = creative_scroll >= 9 ? creative_scroll - 9 : 0;
			screen_inventory_creative_reset(s, view_w, view_h);
			selected_slot[player] = col;
			creative_refresh_pointer(player, view_w, view_h);
			return;
		}
	}

	if(pressed_inv) {
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_WINDOW_CLOSE,
			.payload.window_close.window = WINDOWC_INVENTORY,
		});
		screen_set_player(player, &screen_ingame);
		/* A keyboard return can make this screen the global screen too. */
		if(gstate.current_screen == &screen_inventory_creative)
			screen_set(&screen_ingame);
		/* Do not process/render this inventory again in the close frame. */
		return;
	}

	if(pressed && (!pointer_available[player] || pointer_has_item[player])) {
		struct inv_slot* sel = &slots[selected_slot[player]];
		if(sel->is_delete) {
			if(inventory_get_picked_item(windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL))
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_CREATIVE_SET_PICKED,
					.payload.creative_set_picked.item_id = 0,
				});
		} else if(sel->is_saved) {
			creative_saved_action(player, windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), sel->slot, false);
		} else if(sel->is_virtual) {
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
		if(sel->is_delete) {
			if(inventory_get_picked_item(windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL))
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(player)
					.type = SRPC_CREATIVE_SET_PICKED,
					.payload.creative_set_picked.item_id = 0,
				});
		} else if(sel->is_saved) {
			creative_saved_action(player, windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), sel->slot, true);
		} else if(sel->is_virtual) {
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

	creative_refresh_pointer(player, view_w, view_h);

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
				creative_select_tab(false, t);
				tab_changed = true;
				break;
			}
			if(in_top) {
				creative_select_tab(true, t);
				tab_changed = true;
				break;
			}
		}
	}
	if(tab_changed) {
		screen_inventory_creative_reset(s, view_w, view_h);
		return;
	}

	/* The compass only selects the search window. The keyboard is opened by
	 * the wide search field below the tabs, spanning item slots 5 through 9. */
	if(current_tab == CREATIVE_TAB_SEARCH && pointer_available[player] && pressed) {
		int search_x = off_x + (INVENTORY_X_OFFSET + SEARCH_BAR_X) * scale;
		int search_y = off_y + SEARCH_BAR_Y * scale;
		int search_w = SEARCH_BAR_WIDTH * scale;
		if(pointer_x[player] >= search_x && pointer_x[player] < search_x + search_w
		   && pointer_y[player] >= search_y
		   && pointer_y[player] < search_y + SEARCH_BAR_HEIGHT * scale) {
			screen_keyboard_open(search_query, sizeof(search_query),
									"Creative Search", &screen_inventory_creative);
			return;
		}
	}

	/* Scrollbar: seven pixels to the right of the last item slot. */
	if(current_tab != CREATIVE_TAB_CHEST && pointer_available[player]
	   && (pressed || input_held(IB_GUI_CLICK, player))) {
		int bar_x = off_x + (INVENTORY_X_OFFSET + 8 + 9 * 18
			+ SCROLL_X_OFFSET) * scale;
		int track_y = off_y + (INVENTORY_Y_OFFSET + 84 - 2 * 18) * scale;
		int track_h = SCROLL_TRACK_H * scale;
		if(pointer_x[player] >= bar_x && pointer_x[player] < bar_x + 12 * scale
		   && pointer_y[player] >= track_y
		   && pointer_y[player] < track_y + track_h
		   && creative_max_scroll() > 0) {
			size_t max_scroll = creative_max_scroll();
			size_t page_count = max_scroll / 9 + 1;
			int pos = (int)pointer_y[player] - track_y;
			size_t page_index;
			/* The bottom part must always select the final page. This also
			 * makes the incomplete final row reachable with the pointer. */
			if(pos >= track_h - 8 * scale)
				page_index = page_count - 1;
			else
				page_index = (size_t)(pos * (int)(page_count - 1)
					/ (track_h > 1 ? track_h - 1 : 1));
			creative_scroll = page_index * 9;
			if(creative_scroll > max_scroll)
				creative_scroll = max_scroll;
			screen_inventory_creative_reset(s, view_w, view_h);
			creative_refresh_pointer(player, view_w, view_h);
			return;
		}
	}

	for(size_t k = 0; k < slots_index; k++) {
		if(current_tab != CREATIVE_TAB_CHEST && slots[k].is_virtual
		   && slots[k].virtual_item == 0)
			continue;
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
		case CREATIVE_TAB_SAVED:  tex_x = 0;     tex_y = TEX_H; break;
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

	if(current_tab != CREATIVE_TAB_CHEST) {
		int bar_x = off_x + (INVENTORY_X_OFFSET + 8 + 9 * 18
			+ SCROLL_X_OFFSET) * scale;
		int track_y = off_y + (INVENTORY_Y_OFFSET + 84 - 2 * 18) * scale;
		int track_h = SCROLL_TRACK_H * scale;
		size_t max_scroll = creative_max_scroll();
		int thumb_h = 15 * scale;
		int thumb_y = track_y;
		if(max_scroll > 0)
			thumb_y += (track_h - thumb_h) * (int)creative_scroll
				/ (int)max_scroll;
		gutil_texquad(bar_x, thumb_y, max_scroll > 0 ? 390 : 414, 128,
					  24, 30, 12 * scale, thumb_h);
	}

	/* Search is an ordinary creative 5x9 item window. Its query is drawn in
	 * the field that starts at item slot 5 and ends at the last slot. */
	if(current_tab == CREATIVE_TAB_SEARCH) {
		int search_x = off_x + (INVENTORY_X_OFFSET + SEARCH_BAR_X) * scale;
		int search_y = off_y + SEARCH_BAR_Y * scale;
		int search_w = SEARCH_BAR_WIDTH * scale;
		int search_h = SEARCH_BAR_HEIGHT * scale;
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gfx_texture(true);
		gfx_blending(MODE_OFF);
		gfx_blending(MODE_BLEND);
		if(search_query[0]) {
			const char* visible_query = creative_fit_text(
				search_query, scale, search_w - 4 * scale);
			int query_width = gutil_font_width(visible_query, 8 * scale);
			gutil_text(search_x + (search_w - query_width) / 2,
					   search_y + (search_h - 8 * scale) / 2,
					   visible_query, 8 * scale, false);
		}
		gfx_blending(MODE_OFF);
	}

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
	// Oben (0-5): bricks, blume, redstone, powered rail, bookshelf, compass
	// Unten (0-5): lava-eimer, apfel, eisen-axt, gold-schwert, brewing stand, truhe
	static const uint16_t tab_top_items[TAB_ROW_SIZE]    = {45, 37, 331, 27, 47, 345};
	static const uint16_t tab_bottom_items[TAB_ROW_SIZE] = {327, 260, 258, 283, 117, 54};
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
	/* Keep the selected compass tab visible while the search field is active.
	 * The search panel is drawn over the inventory area and must not hide its
	 * top button or icon. */
	if(current_tab == CREATIVE_TAB_SEARCH) {
		//printf("[1234]: %d", tab_tex_x(5, true));
		gfx_bind_texture(&texture_gui_creative_inventory);
		gutil_texquad(tab_screen_x(5, true, off_x, scale), tab_top_y,
					  tab_tex_x(5, true), TAB_BTN_H,
					  TAB_BTN_W, TAB_BTN_H,
					  TAB_DISP_W * scale, TAB_DISP_H * scale);
		gfx_bind_texture(&texture_items);
		gutil_texquad(
			tab_screen_x(5, true, off_x, scale)
				+ (TAB_DISP_W / 2 - 8) * scale,
			tab_top_y + (TAB_DISP_H / 2 - 8) * scale,
			0, 9 * 16, 16, 16, 16 * scale, 16 * scale);
		gfx_bind_texture(&texture_gui_creative_inventory);
	}

	/* Spielerfigur zwischen den beiden Rüstungsspalten, wie im normalen
	 * Inventar. Sie wird vor den Items gerendert, damit die Slot-Items darüber
	 * liegen können. */
	if(current_tab == CREATIVE_TAB_CHEST) {
	float model_center_x = off_x
		+ (INVENTORY_X_OFFSET + (ARMOR_LEFT_X + ARMOR_RIGHT_X) / 2 + 8) * scale;
	float model_center_y = off_y
		+ (INVENTORY_Y_OFFSET + ARMOR_PLAYER_Y) * scale;
	float angle_x = atan2f((pointer_has_item[player] ? pointer_x[player] : model_center_x)
		- model_center_x, 192.0F);
	float angle_y = atan2f((pointer_has_item[player] ? pointer_y[player] : model_center_y)
		- model_center_y, 192.0F);
#ifdef GFX_3D_ELEMENTS
	mat4 model_view;
	glm_mat4_identity(model_view);
	glm_translate(model_view, (vec3) {model_center_x, model_center_y, 0.0F});
	glm_scale(model_view, (vec3) {
		2.50F * (float)scale / (float)GFX_GUI_SCALE,
		-2.50F * (float)scale / (float)GFX_GUI_SCALE, 1.0F});
	glm_rotate_x(model_view, angle_y * 0.66F * 0.5F, model_view);
	glm_rotate_y(model_view, angle_x * 0.5F, model_view);
	glm_translate(model_view, (vec3) {0.0F, 10.0F, 0.0F});

	gfx_write_buffers(true, true, true);
	struct item_data held_item, helmet, chestplate, leggings, boots;
	render_model_player(
		model_view, glm_deg(angle_y * 0.66F * 0.5F), glm_deg(angle_x * 0.5F),
		0.0F, 0.0F,
		inventory_get_hotbar_item(inv, &held_item) ? &held_item : NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 0, &helmet) ? &helmet : NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 1, &chestplate)
			? &chestplate : NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 2, &leggings)
			? &leggings : NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 3, &boots) ? &boots : NULL);
	gfx_write_buffers(true, false, false);
	gfx_matrix_modelview(GLM_MAT4_IDENTITY);
#endif
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
		} else if(slots[k].is_saved) {
			if(saved_items[slots[k].slot].id > 0)
				gutil_draw_item(&saved_items[slots[k].slot], off_x + slots[k].x,
								 off_y + slots[k].y, 1);
		} else if(!slots[k].is_delete) {
			struct item_data item;
			if((selected_slot[player] != k
				|| !inventory_get_picked_item(inv, NULL)
				|| pointer_available[player])
			   && inventory_get_slot(inv, slots[k].slot, &item))
				gutil_draw_item(&item, off_x + slots[k].x, off_y + slots[k].y, 1);
		}
	}

	/* On the last page, do not present completely empty nine-slot rows as
	 * usable inventory fields. The panel texture contains the slot artwork,
	 * so cover only the rows which have no items at all. */
	if(current_tab != CREATIVE_TAB_CHEST) {
		size_t visible_rows = creative_visible_rows();
		for(size_t row = visible_rows; row < 5; row++) {
			int row_y = off_y
				+ (INVENTORY_Y_OFFSET + 84 - 2 * 18 + (int)row * 18) * scale;
			gutil_texquad_col(
				off_x + (INVENTORY_X_OFFSET + 8) * scale, row_y,
				0, 0, 0, 0, 9 * 18 * scale, 18 * scale,
				198, 198, 198, 255);
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
	if(selection->is_delete) {
		icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Löschen");
	} else if(selection->is_saved) {
		if(saved_items[selection->slot].id > 0 || inventory_get_picked_item(inv, NULL))
			icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Ablegen");
	} else if(selection->is_virtual) {
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
		creative_draw_selected_name(off_x + selection->x,
							   off_y + selection->y, item_get_name(&vsel), scale);
	} else if(selection->is_saved && saved_items[selection->slot].id > 0) {
		creative_draw_selected_name(off_x + selection->x,
							   off_y + selection->y,
							   item_get_name(&saved_items[selection->slot]), scale);
	} else if(!selection->is_virtual && !selection->is_saved
			&& !selection->is_delete && inventory_get_slot(inv, selection->slot, &item)) {
		creative_draw_selected_name(off_x + selection->x,
							   off_y + selection->y, item_get_name(&item), scale);
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

void creative_inventory_set_saved_item(size_t slot, struct item_data item) {
	if(slot < CREATIVE_SAVED_SIZE)
		saved_items[slot] = item;
}

struct screen screen_inventory_creative = {
	.reset    = screen_inventory_creative_reset,
	.update   = screen_inventory_creative_update,
	.render2D = screen_inventory_creative_render2D,
	.render3D = NULL,
	.render_world = true,
};
