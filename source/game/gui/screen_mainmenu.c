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

#include "../../graphics/gui_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../network/level_archive.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../stack.h"
#include "../../util.h"
#include "../game_state.h"
#include "screen.h"
#include "../../sound.h"

#ifdef PLATFORM_WII
#include "../../boot/extension.h"
#endif

#include <assert.h>
#include <dirent.h>
#include "../../m-lib/m-string.h"
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>

static const char* menu_options[4] = {
    "Start",
    "Server",
    "Settings",
    "Quit"
};

static size_t gui_selection;
static bool server_failed = false;

// Python-Fehler (Datei:Zeile + Meldung), gesetzt von cavex_run_python_file
extern char g_py_error[160];
extern bool g_py_error_show;

static enum mp3_sound bg_playlist[16] = {
	mp3_bg1,
	mp3_bg2,
	mp3_bg3,
	mp3_bg4,
	mp3_bg5,
	mp3_bg6,
	mp3_bg7,
	mp3_bg8,
	mp3_bg9,
	mp3_bg10,
};

static void screen_mainmenu_reset(struct screen* s, int width, int height) {
	gstate.game_run = false;
#ifdef SPLITSCREEN
	gstate.num_players = 2;
#else
	gstate.num_players = 1;
#endif
	server_failed = false;
	input_pointer_enable(true);

	gstate_set_capture_input_all(false);

	//sound_init();
	sound_play_bg(bg_playlist);
}
static void screen_mainmenu_update(struct screen* s, float dt) {
#ifdef WITH_PYTHON
	/* Fehlt eine WICHTIGE Textur (default.png/gui_2.png, gesetzt in tex_init),
	   starten wir automatisch den Server-Flow (init.py mit "no_resources"), das
	   die Ressourcen nachlaedt. WICHTIG: nicht sofort, sondern erst nach ein paar
	   gerenderten Frames -- sonst ist der GL/GUI-Zustand noch nicht bereit und
	   main.py zeigt nichts an. So laeuft es im exakt gleichen Kontext wie ein
	   manueller "Server"-Klick. */
	{
		extern bool g_missing_resources;
		extern void cavex_run_python_file(const char* path, const char* arg);
		static int no_res_frames = 0;
		static bool no_res_done = false;
		if(!no_res_done && g_missing_resources) {
			if(++no_res_frames >= 3) {
				no_res_done = true;
				cavex_run_python_file(
					config_read_string(&gstate.config_user,
										"paths.python_file", "init.py"),
					"no_resources");
			}
			return; // waehrend der Wartezeit keine Menue-Eingaben verarbeiten
		}
	}
#endif

	if (server_failed) {

	if (input_pressed(IB_ANY, 0))
		sound_play(pcm_click);
		server_failed = false;

	} else if (g_py_error_show) {

		if (input_pressed(IB_ANY, 0)) {
			sound_play(pcm_click);
			g_py_error_show = false;
		}

	} else {

    // Navigation
    if(input_pressed(IB_GUI_UP, 0) && gui_selection > 0)
        gui_selection--;

    if(input_pressed(IB_GUI_DOWN, 0) && gui_selection < 3)
        gui_selection++;

    // Aktion beim A-Knopf
    if(input_pressed(IB_GUI_CLICK, 0)) {
		sound_play(pcm_click);
        switch(gui_selection) {
            case 0: // Start 
				//#ifdef PLATFORM_WII
				menu_screen_set(&spieleranzahl_auswählen);
                /*#endif
				#ifdef PLATFORM_PC
				menu_screen_set(&screen_select_world);
				#endif*/
				break;
            case 1: // Server
#ifdef WITH_PYTHON
				// CPython ist in CavEX gelinkt -> ./init.py in-process ausfuehren
				{
					extern void cavex_run_python_file(const char *path,
													  const char *arg);
					extern bool g_sdtrace;
					extern void sdlog(const char *msg);
					g_sdtrace = true; // schon VOR dem Aufruf tracen
					sdlog("=== Server gedrueckt, rufe cavex_run_python_file ===");
					cavex_run_python_file(config_read_string(&gstate.config_user,
										  "paths.python_file", "init.py"), NULL);
					sdlog("=== zurueck aus cavex_run_python_file ===");
				}
#else
				server_failed = true;
				return;
#endif
				
				break;
            case 2: // Einstellungen
                menu_screen_set(&screen_msettings);
                break;
            case 3: // Beenden
                gstate.quit = true;
                break;
        }
    }

	}

    // Home-Button beendet das Spiel
    if(input_pressed(IB_HOME, 0))
        gstate.quit = true;
}


static void screen_mainmenu_render2D(struct screen* s, int width, int height) {

	gutil_bg();
	gutil_bg_panorama();

	int start_y = height / 3;
	int button_height = 40;
	int button_width  = 300;
	int button_spacing = 20;



	for(int i = 0; i < 4; i++) {
	    int y = start_y + i * (button_height + button_spacing);

    	bool selected = (gui_selection == i);

    	// Texcoords für hell/dunkel
    	int tex_x = 0;
    	int tex_y = selected ? 62 : 42; // hell : dunkel
    	int tex_w = 200;
    	int tex_h = 20;

    	// Button skalieren auf 300x40
		gfx_bind_texture(&texture_gui2);
    	gutil_texquad((width - button_width) / 2, y, tex_x, tex_y, 
					  					 tex_w, tex_h, button_width, button_height);


		//int y = start_y + i*line_height;
    	gutil_text((width/2) - (gutil_font_width(menu_options[i], 20)/2),
									 y + 10, menu_options[i], 20, true);

	}
	
	if (server_failed) {
		int window_width = 220;
		int wx = width / 2 - window_width / 2;
		int wy = height/2 - 60;

		gutil_window(wx, wy, window_width, 100, "Verbindungsfehler");

		gutil_text(wx + 20, wy + 40, "Versuchen sie es", 16, false);
		gutil_text(wx + 20, wy + 60, "später erneut.", 16, false);
	}
	if (g_py_error_show) {
		int window_width = 420;
		int wx = width / 2 - window_width / 2;
		int wy = height / 2 - 80;

		gutil_window(wx, wy, window_width, 160, "Python-Fehler");

		// lange Meldung in Zeilen zu ~40 Zeichen umbrechen (max 4 Zeilen)
		const int per_line = 40;
		int len = (int)strlen(g_py_error);
		char line[per_line + 1];
		for (int li = 0; li < 4; li++) {
			int off = li * per_line;
			if (off >= len)
				break;
			int n = len - off;
			if (n > per_line)
				n = per_line;
			memcpy(line, g_py_error + off, n);
			line[n] = '\0';
			gutil_text(wx + 15, wy + 35 + li * 22, line, 14, false);
		}

		gutil_text(wx + 15, wy + 135, "Taste = weiter", 14, false);
	}

	gutil_license(width, height);

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Change selection");
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Select option");

	float x, y, a;
	bool avaiable = screen_pointer_local(0, width, height, &x, &y, &a);

	if(avaiable) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(x, y, glm_rad(a), 0, 0, 256, 256, 
							 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}

struct screen screen_mainmenu = {
	.reset = screen_mainmenu_reset,
	.update = screen_mainmenu_update,
	.render2D = screen_mainmenu_render2D,
	.render3D = NULL,
	.render_world = true,
};
