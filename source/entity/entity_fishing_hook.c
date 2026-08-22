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

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "../block/aabb.h"
#include "../block/blocks_data.h"
#include "../game/game_state.h"
#include "../graphics/texture_atlas.h"
#include "../item/items.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "../particle.h"
#include "../platform/gfx.h"
#include "entity.h"

static const struct item_data fishing_loot[] = {
	{.id = ITEM_FISH,        .count = 1, .durability = 0},
	{.id = ITEM_FISH,        .count = 1, .durability = 0},
	{.id = ITEM_FISH_COOKED, .count = 1, .durability = 0},
	{.id = ITEM_STICK,       .count = 1, .durability = 0},
	{.id = ITEM_LEATHER,     .count = 1, .durability = 0},
	{.id = ITEM_STRING,      .count = 1, .durability = 0},
	{.id = ITEM_FEATHER,     .count = 1, .durability = 0},
};
#define FISHING_LOOT_COUNT ((int)(sizeof(fishing_loot) / sizeof(fishing_loot[0])))

#define HOOK_MIN_WAIT    300
#define HOOK_MAX_WAIT    600
#define HOOK_FLOAT_ACCEL 0.03F
#define HOOK_SINK_DRAG   0.85F
#define HOOK_AIR_GRAVITY 0.04F

static bool entity_fishing_hook_server_tick(struct entity* e,
											struct server_local* s);

// Compute the rod tip in world space from the current camera state.
// camera.rx = yaw (radians), camera.ry = pitch (radians, π/2 = horizontal).
// Forward = (sin(rx)*sin(ry), cos(ry), cos(rx)*sin(ry)).
// Right   = (cos(rx), 0, -sin(rx)).
// The tip is 0.5 units forward + 0.3 units right + 0.15 units up from eye.
static void rod_tip_world(float* out_x, float* out_y, float* out_z) {
	float rx  = gstate.camera.rx;
	float ry  = gstate.camera.ry;
	float fwd_x = sinf(rx) * sinf(ry);
	float fwd_z = cosf(rx) * sinf(ry);
	float rgt_x = cosf(rx);
	float rgt_z = -sinf(rx);
	*out_x = gstate.camera.x + fwd_x * 0.5f + rgt_x * 0.3f;
	*out_y = gstate.camera.y + 0.15f;
	*out_z = gstate.camera.z + fwd_z * 0.5f + rgt_z * 0.3f;
}

static bool entity_fishing_hook_client_tick(struct entity* e) {
	bool ret = entity_fishing_hook_server_tick(e, NULL);

	// Spawn a bobber particle each tick so the hook is visible.
	// Must pass the packed atlas coordinate, not the raw TEXAT enum.
	vec3 bvel = {0.0f, 0.0f, 0.0f};
	particle_add(e->pos, bvel,
	             tex_atlas_lookup_particle(TEXAT_PARTICLE_BOBBER),
	             0.12f, 2.0f, false,
	             255, 255, 255, true,
	             TEXTURE_ATLAS_PARTICLES);

	return ret;
}

static bool entity_fishing_hook_server_tick(struct entity* e,
											struct server_local* s) {
	assert(e);

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	struct block_data blk;
	bool in_water
		= entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1]),
						   floorf(e->pos[2]), &blk)
		&& (blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL);
	e->data.fishing_hook.in_water = in_water;

	if(in_water) {
		e->vel[0] *= HOOK_SINK_DRAG;
		e->vel[1] += HOOK_FLOAT_ACCEL;
		e->vel[2] *= HOOK_SINK_DRAG;
		if(e->vel[1] > 0.1F) e->vel[1] = 0.1F;

		if(s && !e->data.fishing_hook.has_bite) {
			if(e->data.fishing_hook.wait_ticks <= 0) {
				e->data.fishing_hook.wait_ticks
					= HOOK_MIN_WAIT
					+ (rand() % (HOOK_MAX_WAIT - HOOK_MIN_WAIT + 1));
			} else {
				e->data.fishing_hook.wait_ticks--;
				if(e->data.fishing_hook.wait_ticks == 0) {
					e->data.fishing_hook.has_bite   = true;
					e->data.fishing_hook.catch_item
						= fishing_loot[rand() % FISHING_LOOT_COUNT];
					clin_rpc_send(&(struct client_rpc) {
						.type = CRPC_FISHING_BITE,
						.payload.fishing_bite.entity_id = e->id,
					});
				}
			}
		}
	} else {
		e->vel[1] -= HOOK_AIR_GRAVITY;
		if(e->on_ground) {
			e->vel[0] *= 0.6F;
			e->vel[2] *= 0.6F;
		}
	}

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.003F)
			e->vel[k] = 0.0F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.2F, 0.2F, 0.2F);

	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox,
						(size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	if(collision_xz && !in_water) {
		e->vel[0] = 0.0F;
		e->vel[2] = 0.0F;
	}
	if(e->on_ground && e->vel[1] < 0.0F)
		e->vel[1] = 0.0F;

	return false;
}

static void entity_fishing_hook_render(struct entity* e, mat4 view,
									   float tick_delta) {
	assert(e);

	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

	// Draw fishing line from bobber to rod tip, but only for the local
	// player who owns this hook.
	struct entity* lp = gstate.local_player;
	if(lp && e->data.fishing_hook.owner_id == (uint32_t)(lp->id + 1)) {
		float tip_x, tip_y, tip_z;
		rod_tip_world(&tip_x, &tip_y, &tip_z);

		float dx = tip_x - pos_lerp[0];
		float dy = tip_y - pos_lerp[1];
		float dz = tip_z - pos_lerp[2];

		mat4 mv;
		glm_translate_to(view, pos_lerp, mv);
		gfx_matrix_modelview(mv);

		gfx_blending(MODE_BLEND);
		gfx_texture(false);

		uint8_t cols[8] = {180, 180, 180, 255, 180, 180, 180, 255};
		int16_t pts[6]  = {
			0, 0, 0,
			(int16_t)(dx * 256),
			(int16_t)(dy * 256),
			(int16_t)(dz * 256),
		};
		gfx_draw_lines(2, pts, cols);

		gfx_texture(true);
		gfx_blending(MODE_OFF);
	}
}

static size_t getBoundingBox(const struct entity* e, struct AABB* out) {
	assert(e && out);
	aabb_setsize_centered(out, 0.2F, 0.2F, 0.2F);
	aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
	return 1;
}

void entity_fishing_hook(uint32_t id, struct entity* e, bool server,
						 void* world) {
	assert(e && world);

	entity_default_init(e, server, world);

	e->id             = id;
	e->name           = NULL;
	e->type           = ENTITY_FISHING_HOOK;
	e->tick_server    = entity_fishing_hook_server_tick;
	e->tick_client    = entity_fishing_hook_client_tick;
	e->render         = entity_fishing_hook_render;
	e->teleport       = entity_default_teleport;
	e->getBoundingBox = getBoundingBox;
	e->onRightClick   = NULL;
	e->onLeftClick    = NULL;
	e->leftClickText  = NULL;
	e->rightClickText = NULL;

	e->health    = 1;
	e->drop_item = (struct item_data) {.id = 0};

	e->data.fishing_hook.owner_id   = 0;
	e->data.fishing_hook.wait_ticks = 0;
	e->data.fishing_hook.in_water   = false;
	e->data.fishing_hook.has_bite   = false;
	e->data.fishing_hook.catch_item = (struct item_data) {.id = 0};
}
