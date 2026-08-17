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

enum {
    MAIN,
#ifdef PLATFORM_PC
    FULLSCREEN,
#endif
    DEBUG,
    CONTROLLERS,
    WORLD_GENERATOR,
};

enum {
	QUIT,
	START_FULLSCREEN,
	VERSION,
	DEBUG_SET,
};

static int8_t menu;

static void screen_msettings_reset(struct screen* s, int width, int height) {
	input_pointer_enable(true);

	gutil_button_new_menu();

	gstate_set_capture_input_all(false);

    menu = MAIN;
}

static void choose(int m) {
	gutil_button_new_menu();
    menu = m;
}

static void set(int s) {
	switch (s) {
		case QUIT:
		screen_back();
		break;

		case DEBUG_SET:
		gstate.settings.debug = !gstate.settings.debug;
		break;
	}
}

static void screen_msettings_update(struct screen* s, float dt) {
	gutil_bg();

	// logische GUI-Groesse benutzen: in der update-Phase liefert gfx_width()
	// die native Fenstergroesse, die Buttons werden aber in GUI-Koordinaten
	// gezeichnet -> sonst stimmt die Zentrierung nicht.
	int width = gfx_gui_width();
	int height = gfx_gui_height();
	float _px = 0, _py = 0, _pa;
	bool _ptr = input_pointer(&_px, &_py, &_pa, 0);
	if(_ptr) gfx_pointer_to_gui(&_px, &_py);
    gutil_button_reset(0, _ptr, _px, _py);

	switch (menu) {
        case DEBUG:
		{
			//gutil_button(width/2 - 150, height/2 - 100, 300, 50, gstate.settings.debug ? "ON":"OFF",
			//				&set, DEBUG_SET, 0, 0);
			gutil_text(width/2 - 150, height/2 - 150, "Debug:", 20, true);
			gutil_button_toggle(width/2 - 150, height/2 - 100, 
								gstate.settings.debug, &set, DEBUG_SET, 0, 0);

			gutil_button(width/2 - 150, height/2 + 50, 300, 50, "Back", &choose, MAIN, 0, 1);
		}
		break;

		default: // MAIN
        {
            gutil_button(width/2 - 350, height/2 - 100, 300, 50,"Debug", &choose, DEBUG, 0, 0);
#ifdef PLATFORM_PC
			gutil_button(width/2 + 50, height/2 - 100, 300, 50, "Fullscreen", &choose, FULLSCREEN, 1, 0);	
#endif
			gutil_button(width/2 - 350, height/2 + 50, 300, 50, "World Generator", &choose, WORLD_GENERATOR, 0, 1);

			gutil_button(width/2 + 50,  height/2 + 50, 300, 50, "Back", &set, QUIT, 1, 1);
        }
    }
	gutil_button_update();
}

static void screen_msettings_render2D(struct screen* s, int width, int height) {
    gutil_button_render();

	gutil_license(width, height);

	float x, y, a;
	bool avaiable = screen_pointer_local(0, width, height, &x, &y, &a);

	if(avaiable) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(x, y, glm_rad(a), 0, 0, 256, 256, 
							 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}

struct screen screen_msettings = {
	.reset = screen_msettings_reset,
	.update = screen_msettings_update,
	.render2D = screen_msettings_render2D,
	.render3D = NULL,
	.render_world = false,
};