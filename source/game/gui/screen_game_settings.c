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

#include <stdio.h>

#include "screen.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../network/server_local.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "../../network/server_interface.h"
#include "../../sound/sound.h"

enum { // Menus
    MAIN,
    GRAPHICS,
    SOUND,
    GAMEMODE,
};

enum { // Buttons
    QUIT,
    OPEN_GRAPHICS,
    OPEN_SOUND,
    OPEN_GAMEMODE,
    // graphics
    VIEW_DISTANCE_DEC,
    VIEW_DISTANCE_INC,
    RENDER_SCALE_DEC,
    RENDER_SCALE_INC,
    VIEW_BOB_TOGGLE,
    CAPTURE_PANORAMA,
    // sound
    MASTER_VOL_DEC,
    MASTER_VOL_INC,
    STEP_VOL_DEC,
    STEP_VOL_INC,
    DIG_VOL_DEC,
    DIG_VOL_INC,
    BG_MUSIC_TOGGLE,
    // gamemode
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

static void vol_clamp(float *v) {
	if(*v < 0.0f) *v = 0.0f;
	if(*v > 1.0f) *v = 1.0f;
}

static void set(int s) {
	switch(s) {
		case QUIT:
			screen_set_player(gstate_active_player(), &screen_game_menu);
			break;

		/* graphics */
		case VIEW_DISTANCE_DEC:
			if(gstate.settings.view_distance > MIN_VIEW_DISTANCE)
				gstate.settings.view_distance--;
			break;
		case VIEW_DISTANCE_INC:
			if(gstate.settings.view_distance < MAX_VIEW_DISTANCE)
				gstate.settings.view_distance++;
			break;
		case RENDER_SCALE_DEC:
			gstate.settings.render_scale_pct -= 10;
			if(gstate.settings.render_scale_pct < 0)
				gstate.settings.render_scale_pct = 0;
			gfx_apply_render_scale(gstate.settings.render_scale_pct);
			break;
		case RENDER_SCALE_INC:
			gstate.settings.render_scale_pct += 10;
			if(gstate.settings.render_scale_pct > 100)
				gstate.settings.render_scale_pct = 100;
			gfx_apply_render_scale(gstate.settings.render_scale_pct);
			break;
		case VIEW_BOB_TOGGLE:
			gstate.settings.view_bob = !gstate.settings.view_bob;
			break;
		case CAPTURE_PANORAMA:
			gstate.panorama_capture.active = true;
			gstate.panorama_capture.face = 0;
			break;

		/* sound */
		case MASTER_VOL_DEC:
			gstate.settings.max_volume -= 0.2f;
			if(gstate.settings.max_volume < 0.0f) gstate.settings.max_volume = 0.0f;
			break;
		case MASTER_VOL_INC:
			gstate.settings.max_volume += 0.2f;
			if(gstate.settings.max_volume > 2.0f) gstate.settings.max_volume = 2.0f;
			break;
		case STEP_VOL_DEC:
			gstate.settings.step_volume -= 0.1f;
			vol_clamp(&gstate.settings.step_volume);
			break;
		case STEP_VOL_INC:
			gstate.settings.step_volume += 0.1f;
			vol_clamp(&gstate.settings.step_volume);
			break;
		case DIG_VOL_DEC:
			gstate.settings.dig_volume -= 0.1f;
			vol_clamp(&gstate.settings.dig_volume);
			break;
		case DIG_VOL_INC:
			gstate.settings.dig_volume += 0.1f;
			vol_clamp(&gstate.settings.dig_volume);
			break;
		case BG_MUSIC_TOGGLE:
			gstate.settings.bg_music_enabled = !gstate.settings.bg_music_enabled;
			if(gstate.settings.bg_music_enabled)
				sound_resume_bg();
			else
				sound_stop_bg();
			break;

		/* gamemode */
		case SET_GAMEMODE_PLAYER_ONE:
			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(0)
				.type = SRPC_SET_GAMEMODE,
				.payload.set_gamemode.toggle = true,
			});
			break;
		case SET_GAMEMODE_PLAYER_TWO:
			if(gstate.num_players > 1) {
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

	int cx = width / 2;
	int cy = height / 2;

	switch(menu) {
		/* ---- GRAPHICS ---- */
		case GRAPHICS:
		{
			gutil_button(cx - 70, cy - 80, 50, 50, "-", &set, VIEW_DISTANCE_DEC, 0, 0);
			gutil_button(cx + 20, cy - 80, 50, 50, "+", &set, VIEW_DISTANCE_INC, 1, 0);
			gutil_button(cx - 70, cy + 15, 50, 50, "-", &set, RENDER_SCALE_DEC,  0, 1);
			gutil_button(cx + 20, cy + 15, 50, 50, "+", &set, RENDER_SCALE_INC,  1, 1);
			gutil_button_toggle(cx - 25, cy + 70, gstate.settings.view_bob,
								&set, VIEW_BOB_TOGGLE, 0, 2);
			gutil_button(cx - 125, cy + 130, 250, 50, "Capture panorama",
						 &set, CAPTURE_PANORAMA, 0, 3);
			gutil_button(cx - 75, cy + 190, 150, 50, "Back", &choose, MAIN, 0, 4);

			char vd[48];
			snprintf(vd, sizeof(vd), "View distance: %d", gstate.settings.view_distance);
			gutil_text(cx - 200, cy - 120, vd, 20, true);

			int rs = gstate.settings.render_scale_pct;
			int rw = GFX_PC_WINDOW_WIDTH  + (gfx_width()  - GFX_PC_WINDOW_WIDTH)  * rs / 100;
			int rh = GFX_PC_WINDOW_HEIGHT + (gfx_height() - GFX_PC_WINDOW_HEIGHT) * rs / 100;
			char rs_str[48];
			snprintf(rs_str, sizeof(rs_str), "Render: %d%% (%dx%d)", rs, rw, rh);
			gutil_text(cx - 200, cy - 15, rs_str, 20, true);
			gutil_text(cx - 200, cy + 40, "Camera Bob:", 20, true);
			if(gstate.panorama_capture.active)
				gutil_text(cx - 200, cy + 105, "Capturing panorama ...", 16, true);
		}
		break;

		/* ---- SOUND ---- */
		case SOUND:
		{
			int master_pct = (int)(gstate.settings.max_volume / 2.0f * 100.0f + 0.5f);
			int step_pct   = (int)(gstate.settings.step_volume * 100.0f + 0.5f);
			int dig_pct    = (int)(gstate.settings.dig_volume  * 100.0f + 0.5f);

			gutil_button(cx - 70, cy - 115, 50, 50, "-", &set, MASTER_VOL_DEC, 0, 0);
			gutil_button(cx + 20, cy - 115, 50, 50, "+", &set, MASTER_VOL_INC, 1, 0);
			gutil_button(cx - 70, cy -  20, 50, 50, "-", &set, STEP_VOL_DEC,   0, 1);
			gutil_button(cx + 20, cy -  20, 50, 50, "+", &set, STEP_VOL_INC,   1, 1);
			gutil_button(cx - 70, cy +  75, 50, 50, "-", &set, DIG_VOL_DEC,    0, 2);
			gutil_button(cx + 20, cy +  75, 50, 50, "+", &set, DIG_VOL_INC,    1, 2);
			gutil_button_toggle(cx - 25, cy + 150,
								gstate.settings.bg_music_enabled,
								&set, BG_MUSIC_TOGGLE, 0, 3);
			gutil_button(cx - 75, cy + 215, 150, 50, "Back", &choose, MAIN,    0, 4);

			char ms[32], ss[32], ds[32];
			snprintf(ms, sizeof(ms), "Master: %d%%",   master_pct);
			snprintf(ss, sizeof(ss), "Footstep: %d%%", step_pct);
			snprintf(ds, sizeof(ds), "Dig/Build: %d%%",dig_pct);
			gutil_text(cx - 200, cy - 155, ms, 20, true);
			gutil_text(cx - 200, cy -  60, ss, 20, true);
			gutil_text(cx - 200, cy +  35, ds, 20, true);
			gutil_text(cx - 200, cy + 115, "BG Music:", 20, true);
		}
		break;

		/* ---- GAMEMODE ---- */
		case GAMEMODE:
		{
			bool creative_mode[] = {
				gstate.local_players[0]
				&& gstate.local_players[0]->data.local_player.creative,
				gstate.local_players[1]
				&& gstate.local_players[1]->data.local_player.creative,
			};
			gutil_button_toggle(cx - 150, cy - 5,
								creative_mode[0], &set, SET_GAMEMODE_PLAYER_ONE, 0, 0);
			gutil_button_toggle(cx +  50, cy - 5,
								creative_mode[1], &set, SET_GAMEMODE_PLAYER_TWO, 1, 0);
			gutil_button(cx - 75, cy + 60, 150, 50, "Back", &choose, MAIN, 0, 1);

			gutil_text(cx - 150, cy - 78, "Creative:",  26, true);
			gutil_text(cx - 150, cy - 32, "Player 1:", 16, true);
			gutil_text(cx +  50, cy - 32, "Player 2:", 16, true);
		}
		break;

		/* ---- MAIN ---- */
		default:
		{
			gutil_button(cx - 75, cy - 80, 150, 50, "Graphics",  &choose, GRAPHICS, 0, 0);
			gutil_button(cx - 75, cy -  5, 150, 50, "Sound",     &choose, SOUND,    0, 1);
			gutil_button(cx - 75, cy +  70, 150, 50, "Game mode", &choose, GAMEMODE, 0, 2);
			gutil_button(cx - 75, cy + 145, 150, 50, "Back",      &set,    QUIT,     0, 3);
		}
		break;
	}

	gutil_button_update();
	gutil_button_render();

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
