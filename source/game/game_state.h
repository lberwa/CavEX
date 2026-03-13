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

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

#include "../config.h"
#include "../entity/entity.h"
#include "../item/window_container.h"
#include "../platform/time.h"
#include "../world.h"
#include "camera.h"
#include "gui/screen.h"

#define GAME_NAME "ffCavEX"
#define VERSION_MAJOR 0
#define VERSION_MINOR 3
#define VERSION_PATCH 0
#define VERSION_FORK  2

struct game_state {
	bool network;
	int gerenderte_splitscreen_anzahl; 
	int height2_icon;
	bool game_run;
	int num_players;
	int player_sequence[4];
	sig_atomic_t quit;
	struct random_gen rand_src;
	struct config config_user;
	struct {
		float dt, fps;
		float dt_gpu, dt_vsync;
		size_t chunks_rendered;
	} stats;
	struct {
		float fov;
		float render_distance;
		float fog_distance;
	} config;
	struct screen* current_screen;
	struct camera camera;
	struct camera_ray_result camera_hit;
	struct world world;
	struct entity* local_player;
	dict_entity_t entities;
	uint64_t world_time;
	ptime_t world_time_start;
	struct window_container* windows[256];
	struct digging {
		bool active;
		ptime_t start;
		ptime_t cooldown;
		w_coord_t x, y, z;
	} digging;
	struct held_anim {
		struct {
			ptime_t start;
			bool place;
		} punch;
		struct {
			ptime_t start;
			size_t old_slot;
		} switch_item;
	} held_item_animation;
	bool world_loaded;
	bool in_water;
	bool paused;
	int oxygen;
#ifdef SPLITSCREEN
	int active_player;
	struct camera cameras[2];
	struct camera_ray_result camera_hits[2];
	struct entity* local_players[2];
	struct digging diggings[2];
	struct held_anim held_item_animations[2];
	bool in_water_arr[2];
	int oxygen_arr[2];
#else
#endif
};

extern struct game_state gstate;

#ifdef SPLITSCREEN
static inline struct window_container** gstate_windows(void) {
	return gstate.windows;
}
static inline int gstate_active_player(void) {
	return gstate.active_player;
}
static inline int splitscreen_player_count(void) {
	return (gstate.num_players > 2) ? 2 : gstate.num_players;
}
static inline bool splitscreen_enabled(void) {
#ifdef PLATFORM_PC
	return false;
#else
	return gstate.num_players > 1;
#endif
}
static inline void splitscreen_load_player(int idx) {
	gstate.active_player = idx;
	gstate.camera = gstate.cameras[idx];
	gstate.camera_hit = gstate.camera_hits[idx];
	gstate.local_player = gstate.local_players[idx];
	gstate.digging = gstate.diggings[idx];
	gstate.held_item_animation = gstate.held_item_animations[idx];
	gstate.in_water = gstate.in_water_arr[idx];
	gstate.oxygen = gstate.oxygen_arr[idx];
}
static inline void splitscreen_store_player(int idx) {
	gstate.cameras[idx] = gstate.camera;
	gstate.camera_hits[idx] = gstate.camera_hit;
	gstate.local_players[idx] = gstate.local_player;
	gstate.diggings[idx] = gstate.digging;
	gstate.held_item_animations[idx] = gstate.held_item_animation;
	gstate.in_water_arr[idx] = gstate.in_water;
	gstate.oxygen_arr[idx] = gstate.oxygen;
}
static inline void gstate_set_capture_input_all(bool enable) {
	int count = splitscreen_player_count();
	for(int i = 0; i < count; i++) {
		if(gstate.local_players[i])
			gstate.local_players[i]->data.local_player.capture_input = enable;
	}
}
#else
static inline struct window_container** gstate_windows(void) {
	return gstate.windows;
}
static inline int gstate_active_player(void) {
	return 0;
}
static inline void gstate_set_capture_input_all(bool enable) {
	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = enable;
}
#endif

#ifndef NDEBUG
#define NDEBUG
#endif

#ifdef NDEBUG // for bg_init debugging
//extern struct game_state gstate;
//extern ptime_t global_last_pos_update;
//extern struct client_rpc* global_call_type;
//extern struct thread_channel clin_inbox;



#endif

#endif
