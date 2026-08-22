/*
	Copyright (c) 2026 ByteBit/xtreme8000

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

// Rideable, steerable boat entity (issue #34). Modelled on entity_minecart.c:
// the boat is a server-authoritative entity that buoys on water, falls under
// gravity on land, and applies steering input written by the riding player in
// entity_local_player.c (occupant model, like the minecart). The render reuses
// the wooden-planks block renderer (no new art) scaled to the hull box.

#include <assert.h>
#include <math.h>

#include "../block/aabb.h"
#include "../block/blocks_data.h"
#include "../game/game_state.h"
#include "../item/items.h"
#include "../network/client_interface.h"
#include "../network/server_interface.h"
#include "../network/server_local.h"
#include "entity.h"

// Pure steering math — see declaration in entity.h. Keeps no engine state so it
// can be unit-tested in isolation.
void entity_boat_steer(float* yaw, vec3 vel, int forward, int turn) {
	assert(yaw && vel);

	*yaw += (float)turn * BOAT_TURN_SPEED;

	vel[0] += (float)forward * BOAT_ACCEL * sinf(*yaw);
	vel[2] += (float)forward * BOAT_ACCEL * cosf(*yaw);

	vel[0] *= BOAT_DRAG;
	vel[2] *= BOAT_DRAG;
}

// Pure motor math — see declaration in entity.h. Adds a steady forward impulse
// along the heading while powered, then caps the horizontal speed so a cruising
// boat can never move far enough in one tick to skip past unloaded chunks.
void entity_boat_throttle(float yaw, vec3 vel, bool powered) {
	assert(vel);

	if(!powered)
		return;

	vel[0] += MOTOR_THRUST * sinf(yaw);
	vel[2] += MOTOR_THRUST * cosf(yaw);

	// Clamp horizontal speed to the cap (scale x/z together so the heading is
	// preserved). guard the divide: speed > cap implies speed > 0.
	float speed = sqrtf(vel[0] * vel[0] + vel[2] * vel[2]);
	if(speed > MOTOR_MAX_SPEED) {
		float scale = MOTOR_MAX_SPEED / speed;
		vel[0] *= scale;
		vel[2] *= scale;
	}
}

static bool entity_boat_server_tick(struct entity* e, struct server_local* s);

// ffCavEX has server->client entity position sync (CRPC_ENTITY_MOVE) disabled,
// so -- exactly like entity_minecart -- the client must run the FULL physics
// itself, consuming the steer intent the local player tick writes into
// data.boat. Otherwise the rendered hull never moves (and never floats).
static bool entity_boat_client_tick(struct entity* e) {
	return entity_boat_server_tick(e, NULL);
}

static bool entity_boat_server_tick(struct entity* e, struct server_local* s) {
	assert(e);
	(void)s; // boat physics is self-contained; no player state needed

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	// steering: yaw + thrust + horizontal drag from the last control intent
	float yaw = e->data.boat.yaw;
	entity_boat_steer(&yaw, e->vel, e->data.boat.control_forward,
					  e->data.boat.control_turn);
	e->data.boat.yaw = yaw;
	e->orient[0] = yaw;

	// motor (issue #33): self-propel forward along the heading while engaged,
	// capped so it cannot chunk-skip. Applied after steering so the rider can
	// still turn and add/subtract thrust on top of the cruise.
	entity_boat_throttle(yaw, e->vel, e->data.boat.powered);

	// consume the control so the boat coasts to a stop if the rider dismounts or
	// stops sending input.
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	// Unmanned boats damp harder so a riderless hull settles in place instead of
	// drifting/wandering off, and stays easy to find and re-board (#93).
	if(e->data.boat.passenger_id == 0) {
		e->vel[0] *= 0.6F;
		e->vel[2] *= 0.6F;
	}

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	// buoyancy: sample water at the hull bottom and top. Fully submerged -> rise
	// toward the surface; straddling the surface -> damp and settle; in air ->
	// fall under gravity.
	struct block_data blk;
	bool feet_water
		= entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1] - 0.25F),
						   floorf(e->pos[2]), &blk)
		&& (blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL);
	bool head_water
		= entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1] + 0.25F),
						   floorf(e->pos[2]), &blk)
		&& (blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL);
	e->data.boat.in_water = feet_water || head_water;

	if(head_water)
		e->vel[1] += BOAT_BUOYANCY;
	else if(feet_water)
		e->vel[1] *= 0.5F;
	else
		e->vel[1] -= BOAT_GRAVITY;
	e->vel[1] *= 0.9F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, BOAT_WIDTH, BOAT_HEIGHT, BOAT_LENGTH);

	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	return false; // boats are never auto-destroyed
}

static void entity_boat_render(struct entity* e, mat4 view, float tick_delta) {
	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

	// Reuse the planks block renderer for a wooden hull box (no new art). The
	// unit cube it draws is rotated by the heading and scaled to the hull size.
	struct item_data plank
		= {.id = BLOCK_PLANKS, .durability = 0, .count = 1};
	struct item* it = item_get(&plank);

	if(it) {
		mat4 model;
		glm_translate_make(model, pos_lerp);
		glm_rotate_y(model, e->data.boat.yaw, model);
		glm_scale(model, (vec3) {BOAT_WIDTH, BOAT_HEIGHT, BOAT_LENGTH});
		glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});

		mat4 mv;
		glm_mat4_mul(view, model, mv);
		it->renderItem(it, &plank, mv, true, R_ITEM_ENV_ENTITY);
	}

	struct AABB bbox;
	aabb_setsize_centered(&bbox, BOAT_WIDTH, 0.1F, BOAT_LENGTH);
	aabb_translate(&bbox, pos_lerp[0], pos_lerp[1] - BOAT_HEIGHT / 2.0F,
				   pos_lerp[2]);
	entity_shadow(e, &bbox, view);
}

static size_t getBoundingBox(const struct entity* e, struct AABB* out) {
	assert(e && out);
	aabb_setsize_centered(out, BOAT_WIDTH, BOAT_HEIGHT, BOAT_LENGTH);
	aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
	return 1;
}

static bool onLeftClick(struct entity* e) {
	assert(e);
	svin_rpc_send(&(struct server_rpc) {
		.type = SRPC_ENTITY_ATTACK,
		.payload.entity_attack.entity_id = e->id,
	});
	return true;
}

static bool onRightClick(struct entity* e, struct item_data* held) {
	assert(e);
	(void)held;

	struct entity* player = gstate.local_player;
	if(!player)
		return false;

	// The local player's entity id is 0, and passenger_id == 0 already means
	// "no rider" -- so we store the rider as (id + 1) to keep the two apart.
	// Otherwise an empty boat would read as "ridden by player 0" and auto-mount.
	uint32_t me = player->id + 1;

	if(e->data.boat.passenger_id == 0) {
		// Mount boat
		e->data.boat.passenger_id = me;
		e->rightClickText = "Dismount";
		return true;
	} else if(e->data.boat.passenger_id == me) {
		// Dismount
		e->data.boat.passenger_id = 0;
		e->data.boat.powered = false;
		player->pos[1] += 1.2f; // avoid being stuck
		e->rightClickText = "Ride";
		return true;
	}

	return false;
}

void entity_boat(uint32_t id, struct entity* e, bool server, void* world) {
	assert(e && world);

	// IMPORTANT: entity_default_init() resets the callback pointers (name,
	// getBoundingBox, onRightClick, ...) and zeroes pos/vel, so it MUST run
	// FIRST -- otherwise it wipes everything set below and the boat becomes
	// un-targetable (no bounding box) and un-mountable (no onRightClick).
	entity_default_init(e, server, world);

	e->name = "Boat";
	e->id = id;
	e->tick_server = entity_boat_server_tick;
	e->tick_client = entity_boat_client_tick;
	e->render = entity_boat_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_BOAT;
	e->getBoundingBox = getBoundingBox;
	e->leftClickText = NULL;
	e->onLeftClick = onLeftClick;
	e->rightClickText = "Ride";
	e->onRightClick = onRightClick;

	e->health = 4;
	e->drop_item = (struct item_data) {.id = ITEM_BOAT, .durability = 0,
									   .count = 1};

	e->data.boat.yaw = 0.0F;
	e->data.boat.passenger_id = 0;
	e->data.boat.in_water = false;
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;
	e->data.boat.powered = false;
}
