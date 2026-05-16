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

#include "../../graphics/gfx_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/render_model.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../platform/time.h"
#include "../game_state.h"
#include "screen.h"

#define GUI_WIDTH 300
#define GUI_HEIGHT 190

struct inv_slot {
	int x, y;
	size_t slot;
};

static bool pointer_has_item[4];
static bool pointer_available[4];
static float pointer_x[4], pointer_y[4], pointer_angle[4];
static struct inv_slot slots[IRON_CHEST_SIZE];
static size_t slots_index;
static size_t selected_slot[4];
static uint8_t iron_chest_container[4];
static int gui_scale[4];

void screen_iron_chest_set_windowc(int player, uint8_t container) {
	iron_chest_container[player] = container;
}

static void screen_iron_chest_reset(struct screen* s, int width, int height) {
	int player = gstate_active_player();
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	int scale = screen_gui_scale(view_w, view_h, GUI_WIDTH, GUI_HEIGHT);
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);

	s->render3D = screen_ingame.render3D;

	pointer_available[player] = false;
	pointer_has_item[player] = false;
	gui_scale[player] = scale;

	slots_index = 0;

	for(int k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (8 + (k % INVENTORY_SIZE_HOTBAR) * 18) * scale,
			.y = (84 + (k / INVENTORY_SIZE_HOTBAR) * 18) * scale,
			.slot = k + IRON_CHEST_SLOT_MAIN,
		};
	}

	for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		if(k
		   == (int)inventory_get_hotbar(
			   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY])))
			selected_slot[player] = slots_index;

		slots[slots_index++] = (struct inv_slot) {
			.x = (8 + k * 18) * scale,
			.y = (84 + 3 * 18 + 4) * scale,
			.slot = k + IRON_CHEST_SLOT_HOTBAR,
		};
	}

	for(int k = 0; k < IRON_CHEST_SIZE_MAIN_STORAGE; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (8 + (k % INVENTORY_SIZE_HOTBAR) * 18) * scale,
			.y = (16 + (k / INVENTORY_SIZE_HOTBAR) * 18) * scale,
			.slot = k + IRON_CHEST_SLOT_MAIN_STORAGE,
		};
	}

	// TODO: a less ugly GUI which can still fit in 320x200
	// HACK: hardcoded slot counts to awkwardly fit a double chest in 320x200
	for(int k = 0; k < 12; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (170 + (k % 4) * 18) * scale,
			.y = (16 + (k / 4) * 18) * scale,
			.slot = k + IRON_CHEST_SIZE_SIDE_STORAGE,
		};
	}
	for(int k = 0; k < 12; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (178 + (k % 4) * 18) * scale,
			.y = (84 + (k / 4) * 18) * scale,
			.slot = k + 12 + IRON_CHEST_SIZE_SIDE_STORAGE,
		};
	}
	for(int k = 0; k < 3; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (195 + (k % 4) * 18) * scale,
			.y = (84 + 3 * 18 + 4) * scale,
			.slot = k + 24 + IRON_CHEST_SIZE_SIDE_STORAGE,
		};
	}
}

static void screen_iron_chest_update(struct screen* s, float dt) {
	int player = gstate_active_player();
	int scale = gui_scale[player];
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	if(input_pressed(IB_INVENTORY, player)) {
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_WINDOW_CLOSE,
			.payload.window_close.window = iron_chest_container[player],
		});

		screen_set_player(player, &screen_ingame);
	}

	if(input_pressed(IB_GUI_CLICK, player)) {
		uint16_t action_id;
		if(windowc_new_action(gstate_windows()[iron_chest_container[player]],
		                      &action_id, false, slots[selected_slot[player]].slot)) {
			svin_rpc_send(&(struct server_rpc) {
				RPC_PLAYER_ID(player)
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = iron_chest_container[player],
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = false,
				.payload.window_click.slot = slots[selected_slot[player]].slot,
			});
		}
	} else if(input_pressed(IB_GUI_CLICK_ALT, player)) {
		uint16_t action_id;
		if(windowc_new_action(gstate_windows()[iron_chest_container[player]],
		                      &action_id, true, slots[selected_slot[player]].slot)) {
			svin_rpc_send(&(struct server_rpc) {
				RPC_PLAYER_ID(player)
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = iron_chest_container[player],
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = true,
				.payload.window_click.slot = slots[selected_slot[player]].slot,
			});
		}
	}

	pointer_available[player]
		= screen_pointer_local(player, view_w, view_h,
		                       &pointer_x[player], &pointer_y[player],
		                       &pointer_angle[player]);

	size_t slot_nearest[4]
		= {selected_slot[player], selected_slot[player], selected_slot[player],
		   selected_slot[player]};
	int slot_dist[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
	int pointer_slot = -1;

	int off_x = (view_w - GUI_WIDTH * scale) / 2;
	int off_y = (view_h - GUI_HEIGHT * scale) / 2;

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

static void screen_iron_chest_render2D(struct screen* s, int width, int height) {
	int player = gstate_active_player();
	int scale = gui_scale[player];
	gutil_set_gui_scale(scale);
	struct inventory* inv
		= windowc_get_latest(gstate_windows()[iron_chest_container[player]]);

	// darken background
	gfx_texture(false);
	gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 180);
	gfx_texture(true);

	int off_x = (width - GUI_WIDTH * scale) / 2;
	int off_y = (height - GUI_HEIGHT * scale) / 2;

	// draw inventory
	gfx_bind_texture(&texture_gui_iron_chest);
	gutil_texquad(off_x, off_y, 0, 0, GUI_WIDTH, GUI_HEIGHT, GUI_WIDTH * scale,
				  GUI_HEIGHT * scale);
	gutil_text(off_x + 28 * scale, off_y + 6 * scale, "\2478Iron Chest", 8 * scale, false);

	struct inv_slot* selection = slots + selected_slot[player];

	// draw items
	for(size_t k = 0; k < slots_index; k++) {
		struct item_data item;
		if((selected_slot[player] != k || !inventory_get_picked_item(inv, NULL)
			|| (pointer_available[player] && pointer_has_item[player]))
		   && inventory_get_slot(inv, slots[k].slot, &item))
			gutil_draw_item(&item, off_x + slots[k].x, off_y + slots[k].y, 1);
	}

	gfx_bind_texture(&texture_gui2);

	gutil_texquad(off_x + selection->x - 4 * scale, off_y + selection->y - 4 * scale, 208, 0,
				  24, 24, 24 * scale, 24 * scale);

	int icon_offset = 16 * scale;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Move");
	if(inventory_get_picked_item(inv, NULL)) {
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK, "Swap item");
	} else if(inventory_get_slot(inv, selection->slot, NULL)) {
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK, "Pickup item");
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Split stack");
	}

	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Leave");

	struct item_data item;
	if(inventory_get_picked_item(inv, &item)) {
		if(pointer_available[player] && pointer_has_item[player]) {
			gutil_draw_item(&item, pointer_x[player] - 8 * scale,
			                pointer_y[player] - 8 * scale, 0);
		} else {
			gutil_draw_item(&item, off_x + selection->x, off_y + selection->y,
							0);
		}
	} else if(inventory_get_slot(inv, selection->slot, &item)) {
		const char* tmp = item_get_name(&item);
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gutil_texquad_col(off_x + selection->x - 2 * scale + 8 * scale
							  - gutil_font_width(tmp, 8 * scale) / 2,
						  off_y + selection->y - 2 * scale + 23 * scale, 0, 0, 0, 0,
						  gutil_font_width(tmp, 8 * scale) + 7, 12 * scale, 0, 0, 0, 180);
		gfx_texture(true);
		gfx_blending(MODE_OFF);

		gutil_text(off_x + selection->x + 8 * scale - gutil_font_width(tmp, 8 * scale) / 2,
				   off_y + selection->y + 23 * scale, tmp, 8 * scale, false);
	}

	if(pointer_available[player]) {
		gfx_bind_texture(&texture_pointer);
		gutil_texquad_rt_any(pointer_x[player], pointer_y[player],
		                     glm_rad(pointer_angle[player]), 0, 0, 256, 256,
		                     48 * scale, 48 * scale);
	}
	gutil_set_gui_scale(GFX_GUI_SCALE);
}

struct screen screen_iron_chest = {
	.reset = screen_iron_chest_reset,
	.update = screen_iron_chest_update,
	.render2D = screen_iron_chest_render2D,
	.render3D = NULL,
	.render_world = true,
};
