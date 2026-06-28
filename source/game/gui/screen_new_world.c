/*
	Copyright (c) 2026 lberwa

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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../../cNBT/nbt.h"
#include "../../config.h"
#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../m-lib/m-string.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../sound.h"
#include "../game_state.h"
#include "screen.h"

/* edited in place; the on-screen keyboard writes the result back here */
static char world_name[64] = "New World";

/* 0 = name field selected, 1 = "Generate" button selected */
static int gui_sel;

/* name field rect (must match render) */
static void name_box_rect(int width, int height, int* x, int* y, int* w,
						  int* h) {
	int scale = GFX_GUI_SCALE;
	*w = 220 * scale;
	*h = 20 * scale;
	*x = (width - *w) / 2;
	*y = height / 2 - *h / 2;
}

/* turn a display name into a safe folder name (in place) */
static void clean_string(char* str) {
	for(int i = 0; str[i] != '\0'; i++) {
		char c = str[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			 || (c >= '0' && c <= '9') || c == '-'))
			str[i] = '_';
	}
}

/* find a folder name under `worlds` that does not exist yet */
static void unique_folder(const char* worlds, const char* base, char* out,
						  size_t out_len) {
	snprintf(out, out_len, "%s", base);
	for(int i = 1; i < 1000; i++) {
		char full[512];
		struct stat st;
		snprintf(full, sizeof(full), "%s/%s", worlds, out);
		if(stat(full, &st) != 0)
			return; /* free */
		snprintf(out, out_len, "%s_%d", base, i);
	}
}

/* Create <worlds>/<folder>/level.dat (gzip NBT) and fill the Data fields.
   Returns the full world folder path in out_path. */
static bool world_create_level_dat(const char* folder, const char* name,
								   int64_t seed, char* out_path,
								   size_t out_len) {
	const char* worlds
		= config_read_string(&gstate.config_user, "paths.worlds", "saves");

	snprintf(out_path, out_len, "%s/%s", worlds, folder);
	mkdir(worlds, 0755); /* make sure saves/ exists */
	if(mkdir(out_path, 0755) != 0 && errno != EEXIST)
		return false; /* create the world folder */

	/* helper: append a node to a compound/list sentinel */
#define KB_LIST(sentinel)                                                      \
	struct nbt_list sentinel = {.data = &(nbt_node) {.type = TAG_COMPOUND}};   \
	INIT_LIST_HEAD(&sentinel.entry)

	/* --- Player.Pos / Motion (3 doubles) and Rotation (2 floats) lists --- */
	nbt_node pos_e[3] = {
		{.type = TAG_DOUBLE, .payload.tag_double = 0.0},
		{.type = TAG_DOUBLE, .payload.tag_double = 80.0},
		{.type = TAG_DOUBLE, .payload.tag_double = 0.0},
	};
	nbt_node mot_e[3] = {
		{.type = TAG_DOUBLE, .payload.tag_double = 0.0},
		{.type = TAG_DOUBLE, .payload.tag_double = 0.0},
		{.type = TAG_DOUBLE, .payload.tag_double = 0.0},
	};
	nbt_node rot_e[2] = {
		{.type = TAG_FLOAT, .payload.tag_float = 0.0F},
		{.type = TAG_FLOAT, .payload.tag_float = 0.0F},
	};

	struct nbt_list pos_sentinel = {.data = &(nbt_node) {.type = TAG_DOUBLE}};
	struct nbt_list mot_sentinel = {.data = &(nbt_node) {.type = TAG_DOUBLE}};
	struct nbt_list rot_sentinel = {.data = &(nbt_node) {.type = TAG_FLOAT}};
	INIT_LIST_HEAD(&pos_sentinel.entry);
	INIT_LIST_HEAD(&mot_sentinel.entry);
	INIT_LIST_HEAD(&rot_sentinel.entry);
	struct nbt_list pos_l[3], mot_l[3], rot_l[2];
	for(int k = 0; k < 3; k++) {
		pos_l[k].data = &pos_e[k];
		list_add_tail(&pos_l[k].entry, &pos_sentinel.entry);
		mot_l[k].data = &mot_e[k];
		list_add_tail(&mot_l[k].entry, &mot_sentinel.entry);
	}
	for(int k = 0; k < 2; k++) {
		rot_l[k].data = &rot_e[k];
		list_add_tail(&rot_l[k].entry, &rot_sentinel.entry);
	}

	nbt_node n_pos = {.type = TAG_LIST, .name = "Pos",
					  .payload.tag_list = &pos_sentinel};
	nbt_node n_mot = {.type = TAG_LIST, .name = "Motion",
					  .payload.tag_list = &mot_sentinel};
	nbt_node n_rot = {.type = TAG_LIST, .name = "Rotation",
					  .payload.tag_list = &rot_sentinel};

	/* empty Inventory list (of compounds) */
	KB_LIST(inv_sentinel);
	nbt_node n_inv = {.type = TAG_LIST, .name = "Inventory",
					  .payload.tag_list = &inv_sentinel};

	nbt_node n_health = {.type = TAG_SHORT, .name = "Health",
						 .payload.tag_short = 20};
	nbt_node n_pdim = {.type = TAG_INT, .name = "Dimension",
					   .payload.tag_int = 0};

	/* --- Player compound --- */
	nbt_node* player_children[]
		= {&n_health, &n_pdim, &n_pos, &n_rot, &n_mot, &n_inv};
	KB_LIST(player_sentinel);
	struct nbt_list player_l[sizeof(player_children) / sizeof(*player_children)];
	for(size_t k = 0; k < sizeof(player_children) / sizeof(*player_children);
		k++) {
		player_l[k].data = player_children[k];
		list_add_tail(&player_l[k].entry, &player_sentinel.entry);
	}
	nbt_node n_player = {.type = TAG_COMPOUND, .name = "Player",
						 .payload.tag_compound = &player_sentinel};

	/* --- Data fields (order kept via list_add_tail) --- */
	nbt_node data_nodes[] = {
		{.type = TAG_STRING, .name = "LevelName", .payload.tag_string = (char*)name},
		{.type = TAG_LONG, .name = "RandomSeed", .payload.tag_long = seed},
		{.type = TAG_LONG, .name = "Time", .payload.tag_long = 0},
		{.type = TAG_LONG, .name = "LastPlayed",
		 .payload.tag_long = (int64_t)time(NULL) * 1000},
		{.type = TAG_LONG, .name = "SizeOnDisk", .payload.tag_long = 0},
		{.type = TAG_INT, .name = "SpawnX", .payload.tag_int = 0},
		{.type = TAG_INT, .name = "SpawnY", .payload.tag_int = 80},
		{.type = TAG_INT, .name = "SpawnZ", .payload.tag_int = 0},
		{.type = TAG_INT, .name = "version", .payload.tag_int = 19132},
	};
	const size_t n = sizeof(data_nodes) / sizeof(*data_nodes);

	KB_LIST(data_sentinel);
	struct nbt_list data_links[sizeof(data_nodes) / sizeof(*data_nodes)];
	for(size_t k = 0; k < n; k++) {
		data_links[k].data = &data_nodes[k];
		list_add_tail(&data_links[k].entry, &data_sentinel.entry);
	}
	/* append the Player compound to Data */
	struct nbt_list data_player_link = {.data = &n_player};
	list_add_tail(&data_player_link.entry, &data_sentinel.entry);

	nbt_node data = {.type = TAG_COMPOUND, .name = "Data",
					 .payload.tag_compound = &data_sentinel};

#undef KB_LIST

	/* root("") contains "Data" */
	struct nbt_list root_sentinel = {.data = &(nbt_node) {.type = TAG_COMPOUND}};
	INIT_LIST_HEAD(&root_sentinel.entry);
	struct nbt_list root_link = {.data = &data};
	list_add_head(&root_link.entry, &root_sentinel.entry);
	nbt_node root = {.type = TAG_COMPOUND, .name = "",
					 .payload.tag_compound = &root_sentinel};

	/* --- save as gzip NBT --- */
	char file[512];
	snprintf(file, sizeof(file), "%s/level.dat", out_path);
	FILE* f = fopen(file, "wb");
	if(!f)
		return false;
	nbt_status st = nbt_dump_file(&root, f, STRAT_GZIP);
	fclose(f);
	return st == NBT_OK;
}

/* create the world on disk and start loading it */
static void new_world_generate(void) {
	char folder[64];
	snprintf(folder, sizeof(folder), "%s", world_name);
	clean_string(folder);
	if(folder[0] == '\0')
		snprintf(folder, sizeof(folder), "world");

	const char* worlds
		= config_read_string(&gstate.config_user, "paths.worlds", "saves");

	char chosen[80];
	unique_folder(worlds, folder, chosen, sizeof(chosen));

	char path[512];
	/* seed 0 -> the server derives it from the world name on load */
	if(!world_create_level_dat(chosen, world_name, 0, path, sizeof(path)))
		return;

	struct server_rpc rpc = {0};
	rpc.type = SRPC_LOAD_WORLD;
#ifdef SPLITSCREEN
	rpc.player_id = 0;
#endif
	string_init_set_str(rpc.payload.load_world.name, path);
	rpc.payload.load_world.find_spawn = true; /* new world -> snap spawn to ground */
	svin_rpc_send(&rpc);

	screen_set(&screen_generate_world);
}

void screen_new_world_reset(struct screen* s, int width, int height) {
	(void)s;
	(void)width;
	(void)height;
	input_pointer_enable(true);
	gstate_set_capture_input_all(false);
	gui_sel = 0;
}

void screen_new_world_update(struct screen* s, float dt) {
	(void)s;
	(void)dt;

	if(input_pressed(IB_BACK, 0)) {
		sound_play(pcm_click);
		screen_set(&screen_select_world);
		return;
	}

	if(input_pressed(IB_GUI_UP, 0) && gui_sel > 0) {
		gui_sel--;
		sound_play(pcm_click);
	}
	if(input_pressed(IB_GUI_DOWN, 0) && gui_sel < 1) {
		gui_sel++;
		sound_play(pcm_click);
	}

	if(input_pressed(IB_GUI_CLICK, 0)) {
		sound_play(pcm_click);
		if(gui_sel == 0) {
			/* edit the name with the on-screen keyboard; it returns here and
			 * overwrites world_name */
			screen_keyboard_open(world_name, sizeof(world_name),
								 "Enter world name", &screen_new_world);
		} else {
			new_world_generate();
		}
	}
}

void screen_new_world_render2d(struct screen* s, int width, int height) {
	(void)s;
	int scale = GFX_GUI_SCALE;
	int font = 8 * scale;

	gutil_bg();

	gutil_text((width - gutil_font_width("Create a New World", font)) / 2,
			   height / 4, "Create a New World", font, true);

	int box_x, box_y, box_w, box_h;
	name_box_rect(width, height, &box_x, &box_y, &box_w, &box_h);

	/* name field (bright when selected) */
	gfx_bind_texture(&texture_gui2);
	gutil_texquad(box_x, box_y, 0, gui_sel == 0 ? 62 : 42, 200, 20, box_w,
				  box_h);
	gutil_text(box_x + 4 * scale, box_y + (box_h - font) / 2, world_name, font,
			   true);

	/* "Generate" button below */
	int gen_y = box_y + box_h + 10 * scale;
	gfx_bind_texture(&texture_gui2);
	gutil_texquad(box_x, gen_y, 0, gui_sel == 1 ? 62 : 42, 200, 20, box_w,
				  box_h);
	gutil_text((width - gutil_font_width("Generate", font)) / 2,
			   gen_y + (box_h - font) / 2, "Generate", font, true);
}

struct screen screen_new_world = {
	.reset = screen_new_world_reset,
	.update = screen_new_world_update,
	.render2D = screen_new_world_render2d,
	.render3D = NULL,
	.render_world = false,
};
