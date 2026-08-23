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

#ifndef ENTITY_H
#define ENTITY_H

#include "../m-lib/m-dict.h"
#include <stdbool.h>
#include <stdint.h>

#include "../cglm/cglm.h"
#include "../item/items.h"

struct camera;

enum entity_type {
	ENTITY_LOCAL_PLAYER,
	ENTITY_ITEM,
	ENTITY_MONSTER,
	ENTITY_MINECART,
	ENTITY_BOAT,
	ENTITY_FISHING_HOOK
};

enum ai_state {
    AI_IDLE,
    AI_CHASE,
    AI_FUSE,
    AI_ATTACK,
    // todo: add more
};


struct server_local;

struct AABB;


struct entity {
	uint32_t id;
	bool on_server;
	void* world;
	int delay_destroy;
	short health;
	struct item_data drop_item;

	vec3 pos;
	vec3 pos_old;
	vec3 vel;
	vec2 orient;
	vec2 orient_old;
	bool on_ground;

	vec3 network_pos;

    float         detection_range;
    float         ai_timer;
    enum ai_state ai_state;

	bool (*tick_client)(struct entity*);
	bool (*tick_server)(struct entity*, struct server_local*);
	void (*render)(struct entity*, mat4, float);
	void (*teleport)(struct entity*, vec3);

	enum entity_type type;
    const char *name;
	size_t (*getBoundingBox)(const struct entity *e, struct AABB *out);
    const char *leftClickText;
    const char *rightClickText;
    bool (*onRightClick)(struct entity *e, struct item_data *held);
    bool (*onLeftClick)(struct entity *e);
	union entity_data {
		struct entity_local_player {
			int jump_ticks;
			bool capture_input;
			float body_yaw;
			float body_yaw_old;
			bool flying;
			bool creative;
			int jump_tap_window;
			bool jump_held_prev;
#ifdef SPLITSCREEN
			uint8_t player_index;
#endif
		} local_player;
		struct entity_item {
			struct item_data item;
			int age;
		} item;
		struct entity_monster {
			int id;
			int frame;
			int frame_time_left;
			vec2 direction;
			int direction_time;
			float body_yaw;
		    float head_yaw;
		    int fuse;
			bool shared;
			bool shared_now;
		} monster;
        struct entity_minecart {
			float speed;
			uint8_t rail_meta; // metadata of rail underneath
			bool occupied;
			uint8_t occupant_id;
		    int8_t hx, hz;         // cart-heading in XZ (unit grid: -1,0,+1)
		    uint8_t last_meta;

			struct item_data item;   // houdt id, count, durability

        } minecart;
		struct entity_fishing_hook {
			uint32_t owner_id;      // (player.id + 1), 0 = no owner
			int wait_ticks;         // countdown to bite (set on first water tick)
			bool in_water;          // currently inside a water block
			bool has_bite;          // bite triggered; catch_item is valid
			struct item_data catch_item; // item awarded on reel-in
		} fishing_hook;
		struct entity_boat {
			float yaw;			   // heading in radians (server-authoritative)
			uint32_t passenger_id; // non-zero while a player is aboard
			bool in_water;		   // set by the server tick (buoyancy state)
			int control_forward;   // last steer intent -1/0/+1 (server only)
			int control_turn;	   // last turn  intent -1/0/+1 (server only)
			bool powered; // motor engaged this tick: cruise forward on its own
						  // (issue #33). Set from the rider's held motor item,
						  // cleared on dismount. Runtime state only.
		} boat;
	} data;
};



void make_creeper_bbox(struct AABB* out);


DICT_DEF2(dict_entity, uint32_t, M_BASIC_OPLIST, struct entity*, M_POD_OPLIST)

#include "../world.h"

void entity_local_player(uint32_t id, struct entity* e, struct world* w);
bool entity_local_player_block_collide(vec3 pos, struct block_info* blk_info);

void entity_item(uint32_t id, struct entity* e, bool server, void* world,
				 struct item_data it);

void entity_monster(uint32_t id, struct entity* e, bool server, void* world,
				 int monster_id);

void entity_minecart(uint32_t id, struct entity* e, bool server, void* world);

// Rideable boat (issue #34). Same constructor shape as entity_item: wires the
// tick/render/teleport callbacks and tags the entity ENTITY_BOAT.
void entity_boat(uint32_t id, struct entity* e, bool server, void* world);

// Fishing hook / bobber projectile entity.
void entity_fishing_hook(uint32_t id, struct entity* e, bool server,
						 void* world);

// Boat hull dimensions (blocks) and physics tuning. Shared between the server
// tick and the render so the collision box and drawn box agree.
#define BOAT_WIDTH 1.0F
#define BOAT_HEIGHT 0.5F
#define BOAT_LENGTH 2.0F
#define BOAT_TURN_SPEED 0.06F // yaw change (rad) per tick of turn input
#define BOAT_ACCEL 0.04F	  // forward thrust per tick along the heading
#define BOAT_DRAG 0.9F		  // fraction of horizontal speed kept per tick
#define BOAT_BUOYANCY 0.04F	  // upward push per tick while submerged
#define BOAT_GRAVITY 0.04F	  // downward pull per tick while airborne

// Motor (issue #33): when the motor is engaged the boat self-propels forward.
// MOTOR_THRUST is added along the heading each tick (independent of rider input,
// so it cruises hands-off); MOTOR_MAX_SPEED caps the resulting horizontal speed
// so a powered boat can never outrun chunk loading and skip terrain.
#define MOTOR_THRUST 0.06F	  // forward accel per tick while powered
#define MOTOR_MAX_SPEED 0.35F // hard cap on horizontal speed (blocks/tick)

// Pure steering math: advance the heading by the turn input, add forward thrust
// along the new heading into the x/z velocity, then apply horizontal drag.
// forward/turn are each -1/0/+1. No engine state, so it is unit-testable.
void entity_boat_steer(float* yaw, vec3 vel, int forward, int turn);

// Pure motor math: while `powered`, add MOTOR_THRUST along the heading into the
// x/z velocity, then clamp the horizontal speed to MOTOR_MAX_SPEED. No engine
// state, so it is unit-testable.
void entity_boat_throttle(float yaw, vec3 vel, bool powered);

uint32_t entity_gen_id(dict_entity_t dict);
void entities_client_tick(dict_entity_t dict);
/* world_filter: NULL = alle Entities; sonst nur Entities in dieser Welt */
void entities_client_render(dict_entity_t dict, struct camera* c,
							struct world* world_filter, float tick_delta);

void entity_default_init(struct entity* e, bool server, void* world);
void entity_default_teleport(struct entity* e, vec3 pos);
bool entity_default_client_tick(struct entity* e);

void entity_shadow(struct entity* e, struct AABB* a, mat4 view);

bool entity_get_block(struct entity* e, w_coord_t x, w_coord_t y, w_coord_t z,
					  struct block_data* blk);
bool entity_intersection_threshold(struct entity* e, struct AABB* aabb,
								   vec3 old_pos, vec3 new_pos,
								   float* threshold);
bool entity_intersection(struct entity* e, struct AABB* a,
						 bool (*test)(struct AABB* entity,
									  struct block_info* blk_info));
bool entity_block_aabb_test(struct AABB* entity, struct block_info* blk_info);
bool entity_aabb_intersection(struct entity* e, struct AABB* a);
void entity_try_move(struct entity* e, vec3 pos, vec3 vel, struct AABB* bbox,
					 size_t coord, bool* collision_xz, bool* on_ground);
bool entity_aabb_intersect_ray(const vec3 origin,
                               const vec3 dir,
                               const struct entity *e,
                               float *out_t);

struct entity *raycast_entity(dict_entity_t *entities,
                              const vec3 origin,
                              const vec3 dir,
                              const struct entity *ignore,
                              float maxDist,
                              float *out_tNear);
void entity_damp_velocity(struct entity* e, float threshold);
void entity_move_in_direction(struct entity* e, float accel, vec2 dir);
void entity_clamp_speed(struct entity* e, float max_speed);
void entity_apply_gravity(struct entity* e, float gravity);
void entity_apply_friction(struct entity* e, float slip);
bool entity_try_auto_jump(struct entity* e, float jump_force, float threshold);
void entity_try_unstuck(struct entity* e, void (*make_bbox)(struct AABB*));
void entity_try_move_axis(struct entity* e, int axis, struct AABB (*make_bbox)(void), bool* collision_flag, bool* on_ground_flag);
void entity_get_delta(struct entity* e, vec3 out_delta);
void entity_blend_body_to_head(float* body_yaw, float head_yaw, float factor);
void entity_tick_animation(struct entity* e, float walk_speed, int max_frame);
void entity_choose_random_direction(struct entity* e, vec2 out_dir);

#define JUMP_TAP_WINDOW 10
bool detect_double_tap(bool pressed, int* window);

#endif
