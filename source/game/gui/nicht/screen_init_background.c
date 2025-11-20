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
#include "../../network/server_local.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../platform/thread.h"
#include "../game_state.h"
#include "../../network/client_interface.h"

#include <my_text_renderer.h>


#include "../../network/level_archive.h"
#include "../../network/server_interface.h"
#include "../../stack.h"
#include "../../util.h"
#include "../../game/game_state.h"
#include "../../game/gui/screen.h"

#include <assert.h>
#include <dirent.h>
#include <m-lib/m-string.h>
#include <string.h>
#include <time.h>

static struct stack* worlds = NULL;

static size_t gui_selection;
static int scroll_offset;

static int top_visible;
static int bottom_visible;
static int height_visible;
static int entry_height = 36 * GFX_GUI_SCALE;
static int side_padding = 2 * GFX_GUI_SCALE;
/*
enum screen_exit_reason {
    SCREEN_NONE,
    SCREEN_WORLD_SELECTED,
    SCREEN_HOME_PRESSED
} exit_reason;
*/
struct world_option {
	string_t name;
	string_t directory;
	string_t path;
	int64_t last_access;
	int64_t byte_size;
};

#ifdef NDEBUG
DIR* dd;
static bool world_null;
static bool opt_null;
static bool rpc_null;
#endif

static void screen_initbg_reset(struct screen* s, int width, int height) {
	//terminal_print("screen_initbg_reset starts");
	// initalization paths to background
	width  = screenMode->fbWidth;    // EFB-Breite
	height = screenMode->efbHeight;  // EFB-Höhe
	
	GX_SetScissorBoxOffset(0, 0);
	
	// Viewport auf gesamten Bildschirm
	GX_SetViewport(0, 0, width, height, 0, 1);
	GX_SetScissor(0, 0, width, height);
	
	GX_SetDispCopySrc(0, 0, width, height);
	GX_SetDispCopyDst(width, screenMode->xfbHeight);
	GX_SetDispCopyYScale(GX_GetYScaleFactor(height, screenMode->xfbHeight));


	gstate.game_run = false;
	
	input_pointer_enable(true);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;

	if(worlds) {
		while(!stack_empty(worlds)) {
			struct world_option opt;
			stack_pop(worlds, &opt);
			string_clear(opt.name);
			string_clear(opt.directory);
			string_clear(opt.path);
		}

		stack_destroy(worlds);
		free(worlds);
		worlds = NULL;
	}

	worlds = malloc(sizeof(struct stack));
	stack_create(worlds, 8, sizeof(struct world_option));

	//terminal_print("Loading worlds from saves path");

	const char* saves_path
		= config_read_string(&gstate.config_user, "paths.bg", "bg");

	DIR* d = opendir(saves_path);

	/*if (d == NULL) {
		video_init_custom();
		terminal_clear();
		terminal_print("Failed to open bg directory");
		gstate.quit = true;
	}*/

	#ifdef NDEBUG
	dd = d;
	#endif

	if(d) {
		struct dirent* dir;
		while((dir = readdir(d))) {
			if(dir->d_type & DT_DIR && *dir->d_name != '.') {
				struct world_option opt;
				string_init_printf(opt.path, "%s/%s", saves_path, dir->d_name);

				struct level_archive la;
				if(level_archive_create(&la, opt.path)) {
					char name[64];

					if(!level_archive_read(&la, LEVEL_NAME, name, sizeof(name)))
						strcpy(name, "Missing name");

					string_init_set_str(opt.name, name);
					string_init_set_str(opt.directory, dir->d_name);

					if(!level_archive_read(&la, LEVEL_DISK_SIZE, &opt.byte_size,
										   0))
						opt.byte_size = 0;

					if(!level_archive_read(&la, LEVEL_LAST_PLAYED,
										   &opt.last_access, 0))
						opt.last_access = 0;

					opt.last_access /= 1000;

					level_archive_destroy(&la);
					stack_push(worlds, &opt);
				} else {
					string_clear(opt.path);
				}
			}
		}

		closedir(d);
	}

	gui_selection = 0;
	scroll_offset = side_padding;
	top_visible = height * 0.133F;
	bottom_visible = height - 32 * GFX_GUI_SCALE;
	height_visible = bottom_visible - height * 0.133F;




	struct world_option opt;

	#ifdef NDEBUG
	if (worlds == NULL)
		world_null = true;
	else
		world_null = false;

	if (&opt == NULL)
		opt_null = true;
	else
		opt_null = false;
	
	#endif
    
	//terminal_print("load background");
    //load background
   	
	stack_at(worlds, &opt, gui_selection);

    struct server_rpc rpc;

	#ifdef NDEBUG
	if (&rpc == NULL)
		rpc_null = true;
	else
		rpc_null = false;
	#endif
	rpc.type = SRPC_LOAD_WORLD;
	string_init_set(rpc.payload.load_world.name, opt.path);
	svin_rpc_send(&rpc);

	//terminal_print("reset of screen_select_world done");
	//terminal_print("reset of screen_initbg");
	//disable 3d

	input_pointer_enable(false);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;
	//terminal_print("reset of screen_initbg done");
}

static void screen_initbg_update(struct screen* s, float dt) {
	screen_set(&screen_select_world2);
	if(gstate.world_loaded) {
	//	terminal_print("world loaded, switching to menu");

    	//init 3d
		input_pointer_enable(false);

		if(gstate.local_player)
			gstate.local_player->data.local_player.capture_input = true;

        //start menu
        if (gstate.bg_init_first) {
	        screen_set(&screen_select_world2);
            gstate.bg_init_first = false;
        } else {
            screen_set(&screen_select_world);
        }

    }

	if(input_pressed(IB_HOME))
        gstate.quit = true;

}

static void screen_initbg_render2D(struct screen* s, int width, int height) {
	gutil_bg();
    //gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 0);//black background

	

	gutil_text((width - gutil_font_width("Building terrain", 8 * GFX_GUI_SCALE)) / 2,
			   height / 2 + 4 * GFX_GUI_SCALE, "WIINECRAFT", 8 * GFX_GUI_SCALE, true);

	gutil_text(2 * GFX_GUI_SCALE, height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 2, "Licensed under GPLv3", 8 * GFX_GUI_SCALE, true);
	gutil_text(2 * GFX_GUI_SCALE, height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 1, "Copyright (c) 2023 ByteBit/xtreme8000",
			   8 * GFX_GUI_SCALE, true);

	// just a rough estimate
	float progress
		= fminf((float)world_loaded_chunks(&gstate.world) / MAX_CHUNKS, 1.0F);

	gfx_texture(false);
	gutil_texquad_col((width - 100 * GFX_GUI_SCALE) / 2, height / 2 + 16 * GFX_GUI_SCALE, 0, 0, 0, 0, 100 * GFX_GUI_SCALE, 2 * GFX_GUI_SCALE,
					  128, 128, 128, 255);
	gutil_texquad_col((width - 100 * GFX_GUI_SCALE) / 2, height / 2 + 16 * GFX_GUI_SCALE, 0, 0, 0, 0,
					  100 * GFX_GUI_SCALE * progress, 2 * GFX_GUI_SCALE, 128, 255, 128, 255);
	gfx_texture(true);


	#ifndef NDEBUG
	#define NDEBUG
	#endif

	#ifdef NDEBUG

	char buf[32];
	float f = progress;

	
	snprintf(buf, sizeof(buf), "%f", f);  // Float in String umwandeln

	gutil_text(100, 100, buf, 8 * GFX_GUI_SCALE, false);


	buf[32];
	snprintf(buf, sizeof(buf), "worlds count: %zu", stack_size(worlds));
	gutil_text(20, 80, buf, 8 * GFX_GUI_SCALE, false);

	//ob es drinn ist
	if (!dd) {
    gutil_text(20, 40, "bg folder not found!", 8 * GFX_GUI_SCALE, false);
	}

	/*//was ist drinn
	for (size_t i = 0; i < worlds->count; i++) {
    struct world_option opt;
    stack_at(worlds, &opt, i);

    // C-String aus m_string bekommen
    gutil_text(40, 120 + i * 20, string_cstr(&opt.name), 8 * GFX_GUI_SCALE, false);
    gutil_text(200, 120 + i * 20, string_cstr(&opt.directory), 8 * GFX_GUI_SCALE, false);
	}*/

	if (gstate.world_loaded) gutil_text(20, 140, "state.world_loaded = true", 8 * GFX_GUI_SCALE, false);
	else gutil_text(20, 140, "state.world_loaded = false", 8 * GFX_GUI_SCALE, false);

	if (time_diff_ms(global_last_pos_update, time_get()) >= 50) gutil_text(20, 160, "time_diff_ms = true", 8 * GFX_GUI_SCALE, false);
	else gutil_text(20, 160, "time_diff_ms = false", 8 * GFX_GUI_SCALE, false);

	//gutil_text(20, 160, global_call_type, 8 * GFX_GUI_SCALE, false);

	extern struct client_rpc* global_call_type;


	if (global_call_type) {
		buf[32];
		snprintf(buf, sizeof(buf), "RPC type: %d", global_call_type->type);
		gutil_text(20, 180, buf, 8 * GFX_GUI_SCALE, false);
	} else {
		gutil_text(20, 200, "RPC type: NULL", 8 * GFX_GUI_SCALE, false);
	}

	/*
	if (tchannel_receive(&clin_inbox, &global_call_type, false))
		gutil_text(20, 160, "tchannel_receive = true", 8 * GFX_GUI_SCALE, false);
	else
		gutil_text(20, 160, "tchannel_receive = false", 8 * GFX_GUI_SCALE, false);
*/	

	if (world_null)
		gutil_text(20, 240, "worlds is NULL", 8 * GFX_GUI_SCALE, false);
	else
		gutil_text(20, 240, "worlds is NOT NULL", 8 * GFX_GUI_SCALE, false);

	if (opt_null)
		gutil_text(20, 220, "opt is NULL", 8 * GFX_GUI_SCALE, false);
	else
		gutil_text(20, 220, "opt is NOT NULL", 8 * GFX_GUI_SCALE, false);

	if (rpc_null)
		gutil_text(20, 260, "rpc is NULL", 8 * GFX_GUI_SCALE, false);
	else
		gutil_text(20, 260, "rpc is NOT NULL", 8 * GFX_GUI_SCALE, false);

	#endif
}

struct screen screen_init_bg = {
	.reset = screen_initbg_reset,
	.update = screen_initbg_update,
	.render2D = screen_initbg_render2D,
	.render3D = NULL,
	.render_world = false,
};
