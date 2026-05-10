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

#include "../../graphics/gfx_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/texture_atlas.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../platform/time.h"
#include "../game_state.h"
#include "screen.h"

#define GUI_WIDTH 128
#define GUI_HEIGHT 64

static bool pointer_available[4];
static float pointer_x[4], pointer_y[4], pointer_angle[4];
static size_t slots_index;
static size_t selected_slot[4];
static uint8_t sign_container[4];
static int gui_scale[4];

void screen_sign_set_windowc(int player, uint8_t container) {
	sign_container[player] = container;
}

static void screen_sign_reset(struct screen* s, int width, int height) {
	int player = gstate_active_player();
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	int scale = screen_gui_scale(view_w, view_h, GUI_WIDTH, GUI_HEIGHT);
	input_pointer_enable(true);

	gstate_set_capture_input_player(player, false);

	s->render3D = screen_ingame.render3D;

	pointer_available[player] = false;
	gui_scale[player] = scale;

	selected_slot[player] = 0;
}

static void screen_sign_update(struct screen* s, float dt) {
	int player = gstate_active_player();
	int view_w, view_h;
	screen_viewport_size(player, &view_w, &view_h);
	if(input_pressed(IB_INVENTORY, player)) {
		svin_rpc_send(&(struct server_rpc) {
			RPC_PLAYER_ID(player)
			.type = SRPC_WINDOW_CLOSE,
			.payload.window_close.window = sign_container[player],
		});

		screen_set_player(player, &screen_ingame);
	}

	// TODO: keyboard input on PC, on-screen keyboard on Wii
	if(input_pressed(IB_GUI_LEFT, player)) {
		selected_slot[player] = (selected_slot[player] - 1) & 63;
	}

	if(input_pressed(IB_GUI_RIGHT, player)) {
		selected_slot[player] = (selected_slot[player] + 1) & 63;
	}

	if(input_pressed(IB_GUI_UP, player)) {
		// this action decrements the character
		uint16_t action_id;
		if(windowc_new_action(gstate_windows()[sign_container[player]], &action_id,
							  false, selected_slot[player])) {
			svin_rpc_send(&(struct server_rpc) {
				RPC_PLAYER_ID(player)
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = sign_container[player],
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = false,
				.payload.window_click.slot = selected_slot[player],
			});
		}
	}

	if(input_pressed(IB_GUI_DOWN, player)) {
		// this action increments the character
		uint16_t action_id;
		if(windowc_new_action(gstate_windows()[sign_container[player]], &action_id,
							  true, selected_slot[player])) {
			svin_rpc_send(&(struct server_rpc) {
				RPC_PLAYER_ID(player)
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = sign_container[player],
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = true,
				.payload.window_click.slot = selected_slot[player],
			});
		}
	}

	pointer_available[player]
		= screen_pointer_local(player, view_w, view_h,
		                       &pointer_x[player], &pointer_y[player],
		                       &pointer_angle[player]);
}

static void screen_sign_render2D(struct screen* s, int width, int height) {
	int player = gstate_active_player();
	int scale = gui_scale[player];
	gutil_set_gui_scale(scale);
	struct inventory* inv
		= windowc_get_latest(gstate_windows()[sign_container[player]]);

	// darken background
	gfx_texture(false);
	gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 180);
	gfx_texture(true);

	int off_x = (width - GUI_WIDTH * scale) / 2;
	int off_y = (height - GUI_HEIGHT * scale) / 2;

	// draw sign texture
	gfx_bind_texture(&texture_terrain);
	gutil_texquad(off_x, off_y, TEX_OFFSET(TEXTURE_X(tex_atlas_lookup(TEXAT_PLANKS))), TEX_OFFSET(TEXTURE_Y(tex_atlas_lookup(TEXAT_PLANKS))), 16, 16, GUI_WIDTH * scale, GUI_HEIGHT * scale);

	// draw text
	for(size_t k = 0; k < 64; k++) {
		char str[2];
		struct item_data chr;
		str[1] = '\0';
		if(inventory_get_slot(inv, k, &chr)) {
			str[0] = chr.count;
			gutil_text(off_x + (2 + 8 * (k % 16)) * scale, off_y + (10 + 12 * (k / 16)) * scale, str, 8 * scale, false);
		}
	}

	// draw cursor
	gutil_text(off_x + (2 + 8 * (selected_slot[player] % 16)) * scale,
	           off_y + (12 + 12 * (selected_slot[player] / 16)) * scale,
	           "_", 8 * scale, false);

	// draw controls
	int icon_offset = 16 * scale;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Dec. char");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_DOWN, "Inc. char");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_LEFT, "Prev. char");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_RIGHT, "Next char");
	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Leave");

	if(pointer_available[player]) {
		gfx_bind_texture(&texture_pointer);
		gutil_texquad_rt_any(pointer_x[player], pointer_y[player],
		                     glm_rad(pointer_angle[player]), 0, 0, 256, 256,
		                     48 * scale, 48 * scale);
	}
	gutil_set_gui_scale(GFX_GUI_SCALE);
}

struct screen screen_sign = {
	.reset = screen_sign_reset,
	.update = screen_sign_update,
	.render2D = screen_sign_render2D,
	.render3D = NULL,
	.render_world = true,
};
