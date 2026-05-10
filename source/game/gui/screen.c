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

#include <assert.h>

#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../graphics/gfx_settings.h"
#include "../game_state.h"
#include "screen.h"

struct screen* screen_stack[MAX_SCREEN_STACK];
int screen_stack_top = -1;

static void screen_viewport_rect(int player, int* out_x, int* out_y,
								 int* out_w, int* out_h) {
	int w = gfx_width();
	int h = gfx_height();
	int player_count = splitscreen_player_count();

	if(player_count == 2) {
		int vp_h = h / 2;
		*out_x = 0;
		*out_w = w;
		*out_h = vp_h;
		#ifdef PLATFORM_WII
		*out_y = (player == 0) ? 0 : vp_h;
		#else
		*out_y = (player == 0) ? vp_h : 0;
		#endif
		return;
	}

	if(player_count == 4) {
		int vp_w = w / 2;
		int vp_h = h / 2;
		int row = player / 2;
		int col = player % 2;
		*out_w = vp_w;
		*out_h = vp_h;
		*out_x = col * vp_w;
		#ifdef PLATFORM_WII
		*out_y = row * vp_h;
		#else
		*out_y = (1 - row) * vp_h;
		#endif
		return;
	}

	*out_x = 0;
	*out_y = 0;
	*out_w = w;
	*out_h = h;
}

#define ZOOM_FACTOR 0.69F
#define ZOOM_FACTOR_GAME_MENU 1.0F

#ifdef PLATFORM_WII
static struct screen* screen_viewport_target(int player) {
	if(gstate.current_screen == &screen_ingame)
		return screen_get_player(player);
	return gstate.current_screen;
}
#endif

void screen_viewport_size(int player, int* width, int* height) {
#ifdef SPLITSCREEN
	if(splitscreen_enabled()) {
		int x, y, w, h;
		screen_viewport_rect(player, &x, &y, &w, &h);

#ifdef PLATFORM_WII
		if(screen_viewport_target(player) == &screen_game_menu) {
#else
		if(screen_get_player(player) == &screen_game_menu) {
#endif
			if(width)
				*width = (int)w * ZOOM_FACTOR_GAME_MENU;
			if(height)
				*height = (int)h * ZOOM_FACTOR_GAME_MENU;
		} else {
			if(width)
				*width = (int)w * ZOOM_FACTOR;
			if(height)
				*height = (int)h * ZOOM_FACTOR;
		}

		return;
	}
#else
	(void)player;
#endif
	if(width)
		*width = gfx_width();
	if(height)
		*height = gfx_height();
}

static void screen_reset_player_context(int player, struct screen* s) {
	assert(s);
#ifdef SPLITSCREEN
	int restore_player = gstate_active_player();
	splitscreen_store_player(restore_player);
	splitscreen_load_player(player);
#endif
	if(s->reset)
		s->reset(s, gfx_width(), gfx_height());
#ifdef SPLITSCREEN
	splitscreen_store_player(player);
	splitscreen_load_player(restore_player);
#endif
}

void screen_set(struct screen* s) {
	assert(s);
	gstate.current_screen = s;
	if(s == &screen_ingame) {
		int count = splitscreen_player_count();
		for(int i = 0; i < count; i++)
			gstate.player_screens[i] = &screen_ingame;
	}
	if(s->reset)
		s->reset(s, gfx_width(), gfx_height());
}

void screen_set_player(int player, struct screen* s) {
	assert(s);
	gstate.player_screens[player] = s;
	gstate_set_capture_input_player(player, s == &screen_ingame);
	screen_reset_player_context(player, s);
}

struct screen* screen_get_player(int player) {
	struct screen* s = gstate.player_screens[player];
	return s ? s : &screen_ingame;
}

int screen_gui_scale(int width, int height, int gui_width, int gui_height) {
	int scale = GFX_GUI_SCALE;
	while(scale > 1 && (gui_width * scale > width
	                    || gui_height * scale > height))
		scale--;
	return scale < 1 ? 1 : scale;
}

bool screen_pointer_local(int player, int view_width, int view_height,
						  float* x, float* y, float* angle) {
	float px, py, pa;
	if(!input_pointer(&px, &py, &pa, player))
		return false;

#ifdef SPLITSCREEN
	if(splitscreen_enabled()) {
		int vp_x, vp_y, vp_w, vp_h;
		screen_viewport_rect(player, &vp_x, &vp_y, &vp_w, &vp_h);

		int gui_w, gui_h;

#ifdef PLATFORM_WII
		if(screen_viewport_target(player) == &screen_game_menu) {
#else
		if(screen_get_player(player) == &screen_game_menu) {
#endif
			gui_w = (int)vp_w * ZOOM_FACTOR_GAME_MENU;
			gui_h = (int)vp_h * ZOOM_FACTOR_GAME_MENU;
		} else {
			gui_w = (int)vp_w * ZOOM_FACTOR;
			gui_h = (int)vp_h * ZOOM_FACTOR;
		}

		px -= (float)vp_x;
		#ifdef PLATFORM_WII
		py -= (float)vp_y;
		#else
		int top_y = gfx_height() - (vp_y + vp_h);
		py -= (float)top_y;
		#endif

		if(px < 0.0F || py < 0.0F || px >= (float)vp_w
		   || py >= (float)vp_h)
			return false;

		px *= (float)gui_w / (float)vp_w;
		py *= (float)gui_h / (float)vp_h;
	}
#endif

	*x = px;
	*y = py;
	*angle = pa;
	return true;
}

void screen_back() {
    if(screen_stack_top > 0) {
        screen_stack_top--;
        struct screen* s = screen_stack[screen_stack_top];
        gstate.current_screen = s;

        if(s->reset)
            s->reset(s, gfx_width(), gfx_height());
    }
}



void menu_screen_set(struct screen* s) {
    assert(s);
    
    for(int i=0; i<=screen_stack_top; i++) {
        if(screen_stack[i] == s) return; // Schon im Stack
    }

    if(screen_stack_top < MAX_SCREEN_STACK - 1) {
        screen_stack[++screen_stack_top] = s;
    }

    gstate.current_screen = s;

    if(s->reset)
        s->reset(s, gfx_width(), gfx_height());
}
