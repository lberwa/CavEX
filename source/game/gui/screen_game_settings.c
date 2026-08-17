/*
	Copyright (c) 2022-2026 ByteBit/xtreme8000, lberwa

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

#include "screen.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../network/server_local.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "../../network/server_interface.h"

enum { // Menus
    MAIN,
    GAMEMODE
};

enum { // Buttons
	QUIT,
	SET_GAMEMODE_PLAYER_ONE,
    SET_GAMEMODE_PLAYER_TWO,
};

static int8_t menu;
static int8_t owner_player;

static void screen_gsettings_reset(struct screen* s, int width, int height) {
	input_pointer_enable(true);

	gutil_button_new_menu();

	owner_player = gstate_active_player();
	gstate_set_capture_input_player(owner_player, false);

    menu = MAIN;
}

static void choose(int m) {
	gutil_button_new_menu();
    menu = m;
}

static void set(int s) {
	switch (s) {
		case QUIT:
		screen_set_player(gstate_active_player(), &screen_game_menu);
		break;

		case SET_GAMEMODE_PLAYER_ONE:
		svin_rpc_try_send(&(struct server_rpc) {
			RPC_PLAYER_ID(0)
			.type = SRPC_SET_GAMEMODE,
			.payload.set_gamemode.toggle = true,
		});
		break;

		case SET_GAMEMODE_PLAYER_TWO:
		if (gstate.num_players > 1) {
			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(1)
				.type = SRPC_SET_GAMEMODE,
				.payload.set_gamemode.toggle = true,
			});
		}
		break;
	}
}

static void screen_gsettings_update(struct screen* s, float dt) { }

static void screen_gsettings_render2D(struct screen* s, int width, int height) {
	float x, y, a;
	bool avaiable = screen_pointer_local(owner_player, width, height, &x, &y, &a);

    gutil_button_reset(owner_player, avaiable, x, y);

	switch (menu) {
        case GAMEMODE:
		{
            bool creative_mode[] = {
                gstate.local_players[0]
                && gstate.local_players[0]->data.local_player.creative,

                gstate.local_players[1]
                && gstate.local_players[1]->data.local_player.creative,
            };

			gutil_button_toggle(width/2 - 150, height/2 - 5,
								creative_mode[0], &set, SET_GAMEMODE_PLAYER_ONE, 0, 0);
            gutil_button_toggle(width/2 + 50, height/2 - 5,
								creative_mode[1], &set, SET_GAMEMODE_PLAYER_TWO, 1, 0);
			gutil_button(width/2 - 150, height/2 + 60, 300, 50, "Back", &choose, MAIN, 0, 1);
		}
		break;

		default: // MAIN
        {
			gutil_button(width/2 - 350, height/2 + 50, 300, 50, "Game mode", &choose, GAMEMODE, 0, 1);
			gutil_button(width/2 + 50,  height/2 + 50, 300, 50, "Back", &set, QUIT, 1, 1);
        }
    }
	gutil_button_update();

    gutil_button_render();

	if(menu == GAMEMODE) {
		gutil_text(width/2 - 150, height/2 - 78, "Creative:", 26, true);
		gutil_text(width/2 - 150, height/2 - 32, "Player 1:", 16, true);
		gutil_text(width/2 +  50, height/2 - 32, "Player 2:", 16, true);
	}

	if(avaiable) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(x, y, glm_rad(a), 0, 0, 256, 256,
							 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}

struct screen screen_game_settings = {
	.reset = screen_gsettings_reset,
	.update = screen_gsettings_update,
	.render2D = screen_gsettings_render2D,
	.render3D = NULL,
	.render_world = true,
};