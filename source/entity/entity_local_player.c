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

#include "../block/blocks_data.h"
#include "../graphics/render_entity.h"
#include "../graphics/render_model.h"
#include "../platform/input.h"
#include "../platform/gfx.h"
#include "../game/game_state.h"
#include "../network/server_local.h"
#include "../network/server_interface.h"
#include "entity.h"

#define EYE_HEIGHT 1.62F
#define PLAYER_HITBOX_WIDTH 0.6F
#define PLAYER_HITBOX_HEIGHT 1.8F
#define PLAYER_MODEL_Y_OFFSET 1.125F
#define PLAYER_HEAD_YAW_LIMIT glm_rad(45.0F)
#define PLAYER_BODY_FOLLOW_IDLE 0.18F
#define PLAYER_BODY_FOLLOW_MOVING 0.28F

static float angle_normalize(float angle) {
	while(angle > GLM_PI)
		angle -= 2.0F * GLM_PI;
	while(angle < -GLM_PI)
		angle += 2.0F * GLM_PI;
	return angle;
}

static float angle_lerp(float from, float to, float t) {
	return from + angle_normalize(to - from) * t;
}

static size_t getBoundingBox(const struct entity* e, struct AABB* out) {
	assert(e && out);
	aabb_setsize_centered(out, PLAYER_HITBOX_WIDTH, PLAYER_HITBOX_HEIGHT,
	                      PLAYER_HITBOX_WIDTH);
	aabb_translate(out, e->pos[0],
	               e->pos[1] + PLAYER_HITBOX_HEIGHT / 2.0F - EYE_HEIGHT,
				   e->pos[2]);
	return 1;
}

static void liquid_aabb(struct AABB* out, struct block_info* blk_info) {
	int block_height = (blk_info->block->metadata & 0x8) ?
		16 :
		(8 - blk_info->block->metadata) * 2 * 7 / 8;
	aabb_setsize(out, 1.0F, (float)block_height / 16.0F, 1.0F);
	aabb_translate(out, blk_info->x, blk_info->y, blk_info->z);
}

static bool test_in_lava(struct AABB* entity, struct block_info* blk_info) {
	if(blk_info->block->type != BLOCK_LAVA_FLOW
	   && blk_info->block->type != BLOCK_LAVA_STILL)
		return false;

	struct AABB bbox;
	liquid_aabb(&bbox, blk_info);
	return aabb_intersection(entity, &bbox);
}

static bool test_in_water(struct AABB* entity, struct block_info* blk_info) {
	if(blk_info->block->type != BLOCK_WATER_FLOW
	   && blk_info->block->type != BLOCK_WATER_STILL)
		return false;

	struct AABB bbox;
	liquid_aabb(&bbox, blk_info);
	return aabb_intersection(entity, &bbox);
}

static bool test_in_liquid(struct AABB* entity, struct block_info* blk_info) {
	return test_in_water(entity, blk_info) || test_in_lava(entity, blk_info);
}

static void entity_render(struct entity* e, mat4 view, float td) {
	assert(e);

	if(e == gstate.local_player)
		return;

	vec3 p;
	glm_vec3_lerp(e->pos_old, e->pos, td, p);

	float model_y = p[1] - EYE_HEIGHT + PLAYER_MODEL_Y_OFFSET;
	struct block_data blk_feet = {0};
	struct block_data blk_body = {0};
	struct block_data blk_head = {0};
	entity_get_block(e, floorf(p[0]), floorf(model_y + 0.2F), floorf(p[2]),
	                 &blk_feet);
	entity_get_block(e, floorf(p[0]), floorf(model_y + 1.0F), floorf(p[2]),
	                 &blk_body);
	entity_get_block(e, floorf(p[0]), floorf(model_y + 1.7F), floorf(p[2]),
	                 &blk_head);
	uint8_t sky_light = blk_feet.sky_light;
	if(blk_body.sky_light > sky_light)
		sky_light = blk_body.sky_light;
	if(blk_head.sky_light > sky_light)
		sky_light = blk_head.sky_light;
	uint8_t torch_light = blk_feet.torch_light;
	if(blk_body.torch_light > torch_light)
		torch_light = blk_body.torch_light;
	if(blk_head.torch_light > torch_light)
		torch_light = blk_head.torch_light;
	render_entity_update_light((torch_light << 4) | sky_light);

	float body_yaw = angle_lerp(e->data.local_player.body_yaw_old,
	                            e->data.local_player.body_yaw, td);
	float head_yaw = angle_normalize(e->orient[0] - body_yaw);
	float head_pitch = glm_deg(e->orient[1]) - 90.0F;

	mat4 model, mv;
	glm_translate_make(model,
	                   (vec3) {p[0], p[1] - EYE_HEIGHT + PLAYER_MODEL_Y_OFFSET,
	                           p[2]});
	glm_rotate_y(model, glm_rad(glm_deg(body_yaw)), model);
	glm_scale_uni(model, 1.0f / 16.0f);
	glm_translate(model, (vec3) {0.0f, 10.0f, 0.0f});
	glm_mat4_mul(view, model, mv);

	float dx = e->pos[0] - e->pos_old[0];
	float dz = e->pos[2] - e->pos_old[2];
	float walk_speed = sqrtf(dx * dx + dz * dz);
	float t = time_diff_s(gstate.world_time_start, time_get());
	float foot_angle
		= walk_speed > 0.001f ? sinf(t * 2.0f * GLM_PI * 2.0f) * 30.0f : 0.0f;

	struct item_data held_item, helmet, chestplate, leggings, boots;
	struct item_data *held_ptr = NULL, *helmet_ptr = NULL, *chest_ptr = NULL,
	                 *legs_ptr = NULL, *boots_ptr = NULL;
#ifdef SPLITSCREEN
	int player_index = e->data.local_player.player_index;
#else
	int player_index = 0;
#endif
	struct window_container* wc
		= gstate.windows_by_player[player_index][WINDOWC_INVENTORY];
	if(wc) {
		struct inventory* inv = windowc_get_latest(wc);
		held_ptr = inventory_get_hotbar_item(inv, &held_item) ? &held_item : NULL;
		helmet_ptr = inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 0, &helmet) ?
			&helmet :
			NULL;
		chest_ptr
			= inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 1, &chestplate) ?
				  &chestplate :
				  NULL;
		legs_ptr = inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 2, &leggings) ?
			&leggings :
			NULL;
		boots_ptr = inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 3, &boots) ?
			&boots :
			NULL;
	}

	gfx_lighting(false);
	render_model_player(mv, head_pitch, glm_deg(head_yaw), foot_angle,
	                    foot_angle, held_ptr, helmet_ptr, chest_ptr, legs_ptr,
	                    boots_ptr);
	gfx_lighting(true);

	struct AABB shadow_bb;
	aabb_setsize_centered(&shadow_bb, 0.25f, 0.25f, 0.25f);
	aabb_translate(&shadow_bb, p[0], p[1] - EYE_HEIGHT + 0.04f, p[2]);
	entity_shadow(e, &shadow_bb, view);
}

static bool onLeftClick(struct entity* e) {
	assert(e);
#ifdef SPLITSCREEN
	svin_rpc_send(&(struct server_rpc) {
		RPC_PLAYER_ID(gstate_active_player())
		.type = SRPC_PLAYER_ATTACK,
		.payload.player_attack.target_player_id
			= (uint8_t)e->data.local_player.player_index,
	});
#endif
	return true;
}

static bool entity_tick(struct entity* e) {
	assert(e);
#ifdef SPLITSCREEN
	int player_index = e->data.local_player.player_index;
#else
	int player_index = 0;
#endif

#ifdef TEST_MULTIPLAYER_INPUT
	// Debug: verify per-entity input routing (WASD should only affect player 0,
	// IJKL only player 1).
	static ptime_t last_dbg_by_player[4];
	ptime_t now = time_get();
	if(player_index >= 0 && player_index < 4
	   && time_diff_ms(last_dbg_by_player[player_index], now) >= 250) {
		bool f = input_held(IB_FORWARD, player_index);
		bool b = input_held(IB_BACKWARD, player_index);
		bool l = input_held(IB_LEFT, player_index);
		bool r = input_held(IB_RIGHT, player_index);
		if(f || b || l || r) {
			printf("[entity_local_player tick] ent=%u pidx=%d held F=%d B=%d L=%d R=%d\n",
				   (unsigned)e->id, player_index, (int)f, (int)b, (int)l,
				   (int)r);
		}
		last_dbg_by_player[player_index] = now;
	}
#endif

	// MINECART CONTROL: detect cart by scanning entities for occupant_id == player id
	dict_entity_it_t it;
	dict_entity_it(it, gstate.entities);
	while(!dict_entity_end_p(it)) {
		dict_entity_itref_t *ref = dict_entity_ref(it);      // ref is a pointer to pair { key, value* }
		struct entity* cart = ref->value;                   // value is struct entity*, so deref once
		if(cart && cart->type == ENTITY_MINECART &&
		   cart->data.minecart.occupied &&
		   cart->data.minecart.occupant_id == e->id) {

			// Only allow control if we're really the rider
			if(input_held(IB_FORWARD, player_index))  cart->data.minecart.speed += 0.01f;
			if(input_held(IB_BACKWARD, player_index)) cart->data.minecart.speed -= 0.01f;

			// Clamp speed to a safe limit
			if(cart->data.minecart.speed >  0.3f) cart->data.minecart.speed =  0.3f;
			if(cart->data.minecart.speed < -0.3f) cart->data.minecart.speed = -0.3f;

			// Dismount
			if(input_pressed(IB_JUMP, player_index)) {
				cart->data.minecart.occupied   = false;
				cart->data.minecart.occupant_id = 0;
				// Lift player a little to avoid clipping into the cart
				e->pos[1] += 1.2f;
			}

			// Follow cart position (simple, cheap copy)
			e->pos[0] = cart->pos[0];
			e->pos[1] = cart->pos[1] + 1.0f;
			e->pos[2] = cart->pos[2];
			glm_vec3_copy(e->pos, e->pos_old);

			return false; // skip walking physics when riding
		}
		dict_entity_next(it);
	}

	// ---------- normal player physics ----------
	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, PLAYER_HITBOX_WIDTH, 1.0F,
	                      PLAYER_HITBOX_WIDTH);
	aabb_translate(&bbox, e->pos[0],
	               e->pos[1] + PLAYER_HITBOX_HEIGHT / 2.0F - EYE_HEIGHT,
	               e->pos[2]);

	bool in_water = entity_intersection(e, &bbox, test_in_water);
	bool in_lava  = entity_intersection(e, &bbox, test_in_lava);

	float slipperiness = (in_lava || in_water) ? 1.0F : (e->on_ground ? 0.6F : 1.0F);

	int  forward = 0;
	int  strafe  = 0;
	bool jumping = false;

	if(e->data.local_player.capture_input) {
		if(input_held(IB_FORWARD, player_index))  forward++;
		if(input_held(IB_BACKWARD, player_index)) forward--;
		if(input_held(IB_RIGHT, player_index))    strafe++;
		if(input_held(IB_LEFT, player_index))     strafe--;
		jumping = input_held(IB_JUMP, player_index);
	}

#ifdef PLATFORM_PC
	static ptime_t last_move_dbg[4];
	ptime_t move_dbg_now = time_get();
	if(player_index >= 0 && player_index < 4
	   && time_diff_ms(last_move_dbg[player_index], move_dbg_now) >= 200
	   && (forward != 0 || strafe != 0 || jumping)) {
		printf("[entity_move] pidx=%d capture=%d forward=%d strafe=%d jump=%d orient=(%.3f, %.3f)\n",
			   player_index, (int)e->data.local_player.capture_input, forward,
			   strafe, (int)jumping, e->orient[0], e->orient[1]);
		last_move_dbg[player_index] = move_dbg_now;
	}
#endif

	e->data.local_player.body_yaw_old = e->data.local_player.body_yaw;

	int dist = forward * forward + strafe * strafe;
	if(dist > 0) {
		float distf = fmaxf(sqrtf(dist), 1.0F);
		float dx = (forward * sinf(e->orient[0]) - strafe * cosf(e->orient[0])) / distf;
		float dy = (strafe  * sinf(e->orient[0]) + forward * cosf(e->orient[0])) / distf;

		e->vel[0] += 0.1F * powf(0.6F / slipperiness, 3.0F) * dx;
		e->vel[2] += 0.1F * powf(0.6F / slipperiness, 3.0F) * dy;
	}

	if(e->data.local_player.jump_ticks > 0)
		e->data.local_player.jump_ticks--;

	if(jumping) {
		if(in_water || in_lava) {
			e->vel[1] += 0.04F;
		} else if(e->on_ground && e->data.local_player.jump_ticks == 0) {
			e->vel[1] = 0.42F;
			e->data.local_player.jump_ticks = 10;
		}
	} else {
		e->data.local_player.jump_ticks = 0;
	}

	aabb_setsize_centered(&bbox, PLAYER_HITBOX_WIDTH, PLAYER_HITBOX_HEIGHT,
	                      PLAYER_HITBOX_WIDTH);
	aabb_translate(&bbox, 0.0F, PLAYER_HITBOX_HEIGHT / 2.0F - EYE_HEIGHT, 0.0F);

	// Unstuck (cheap vertical nudge)
	struct AABB tmp1 = bbox, tmp2 = bbox;
	float unstuck_move = 0.01F;
	aabb_translate(&tmp1, e->pos[0], e->pos[1], e->pos[2]);
	aabb_translate(&tmp2, e->pos[0], e->pos[1] + unstuck_move, e->pos[2]);
	if(entity_aabb_intersection(e, &tmp1) && !entity_aabb_intersection(e, &tmp2)) {
		e->pos[1] += unstuck_move;
	}

	vec3 new_pos, new_vel;
	glm_vec3_copy(e->pos, new_pos);
	glm_vec3_copy(e->vel, new_vel);

	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]){1, 0, 2}[k],
		                &collision_xz, &e->on_ground);

	if(e->on_ground) {
		bool collision = false;
		bool ground    = e->on_ground;

		new_vel[1] = 0.6F;
		entity_try_move(e, new_pos, new_vel, &bbox, 1, &collision, &ground);

		new_vel[1] = 0.0F;
		entity_try_move(e, new_pos, new_vel, &bbox, 0, &collision, &ground);
		entity_try_move(e, new_pos, new_vel, &bbox, 2, &collision, &ground);

		new_vel[1] = -0.6F;
		entity_try_move(e, new_pos, new_vel, &bbox, 1, &collision, &ground);

		if(new_pos[1] > e->pos_old[1]
		   && glm_vec3_distance2(e->pos_old, e->pos) < glm_vec3_distance2(e->pos_old, new_pos)) {
			collision_xz = collision;
			e->on_ground = ground;
			glm_vec3_copy(new_pos, e->pos);
			glm_vec3_copy(new_vel, e->vel);
		}
	}

	if(in_lava) {
		e->vel[0] *= 0.5F;
		e->vel[2] *= 0.5F;
		e->vel[1]  = e->vel[1] * 0.5F - 0.02F;
	} else if(in_water) {
		e->vel[0] *= 0.8F;
		e->vel[2] *= 0.8F;
		e->vel[1]  = e->vel[1] * 0.8F - 0.02F;
	} else {
		e->vel[0] *= slipperiness * 0.91F;
		e->vel[2] *= slipperiness * 0.91F;
		e->vel[1] -= 0.08F;

		struct block_data blk;
		if(entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1] - EYE_HEIGHT), floorf(e->pos[2]), &blk)
		   && (blk.type == BLOCK_LADDER || blk.type == BLOCK_VINE)) {
			if(collision_xz) e->vel[1] = 0.12F;
			e->vel[0] = fmaxf(fminf(e->vel[0], 0.15F), -0.15F);
			e->vel[1] = fmaxf(e->vel[1], -0.15F);
			e->vel[2] = fmaxf(fminf(e->vel[2], 0.15F), -0.15F);
		}
		e->vel[1] *= 0.98F;
	}

	if(collision_xz && (in_lava || in_water)) {
		struct AABB tmp;
		aabb_setsize_centered(&tmp, PLAYER_HITBOX_WIDTH, PLAYER_HITBOX_HEIGHT,
		                      PLAYER_HITBOX_WIDTH);
		aabb_translate(&tmp, e->pos[0] + e->vel[0],
		               e->pos[1] + e->vel[1]
		                   + PLAYER_HITBOX_HEIGHT / 2.0F - EYE_HEIGHT + 0.6F,
		               e->pos[2] + e->vel[2]);
		if(!entity_intersection(e, &tmp, test_in_liquid))
			e->vel[1] = 0.3F;
	}

	// update client-side oxygen bar
#ifdef SPLITSCREEN
	if(in_water)
		gstate.oxygen_arr[player_index]--;
	else
		gstate.oxygen_arr[player_index] = MAX_OXYGEN;
#else
	if(gstate.in_water) gstate.oxygen--;
	else gstate.oxygen = MAX_OXYGEN;
#endif

	vec3 movement_delta;
	entity_get_delta(e, movement_delta);
	float walk_speed = sqrtf(movement_delta[0] * movement_delta[0]
	                         + movement_delta[2] * movement_delta[2]);
	float head_world_yaw = e->orient[0];
	float yaw_diff
		= angle_normalize(head_world_yaw - e->data.local_player.body_yaw);
	float follow_factor = walk_speed > 0.001F ? PLAYER_BODY_FOLLOW_MOVING :
	                                          PLAYER_BODY_FOLLOW_IDLE;
	entity_blend_body_to_head(&e->data.local_player.body_yaw, head_world_yaw,
	                          follow_factor);
	yaw_diff = angle_normalize(head_world_yaw - e->data.local_player.body_yaw);
	if(yaw_diff > PLAYER_HEAD_YAW_LIMIT)
		e->data.local_player.body_yaw
			= angle_normalize(head_world_yaw - PLAYER_HEAD_YAW_LIMIT);
	else if(yaw_diff < -PLAYER_HEAD_YAW_LIMIT)
		e->data.local_player.body_yaw
			= angle_normalize(head_world_yaw + PLAYER_HEAD_YAW_LIMIT);

	return false;
}
bool entity_local_player_block_collide(vec3 pos, struct block_info* blk_info) {
	assert(pos && blk_info);

	struct AABB bbox;
	aabb_setsize_centered(&bbox, PLAYER_HITBOX_WIDTH, PLAYER_HITBOX_HEIGHT,
	                      PLAYER_HITBOX_WIDTH);
	aabb_translate(&bbox, pos[0],
	               PLAYER_HITBOX_HEIGHT / 2.0F - EYE_HEIGHT + pos[1], pos[2]);

	return entity_block_aabb_test(&bbox, blk_info);
}

	void entity_local_player(uint32_t id, struct entity* e, struct world* w) {
		assert(e && w);

	entity_default_init(e, false, w);
	e->id = id;
	e->tick_server = NULL;
	e->tick_client = entity_tick;
	e->render = entity_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_LOCAL_PLAYER;
	e->name = "Player";
	e->getBoundingBox = getBoundingBox;
	e->data.local_player.capture_input = true;
	// `player_index` is assigned by the caller (e.g. splitscreen setup).
	// Do not overwrite it here, otherwise all local players end up with index 0.
	    e->leftClickText = "Attack";
	    e->onLeftClick   = onLeftClick;
	    e->rightClickText = NULL;
	    e->onRightClick   = NULL;
	e->health = MAX_PLAYER_HEALTH;
		e->data.local_player.jump_ticks = 0;
		e->data.local_player.body_yaw = e->orient[0];
		e->data.local_player.body_yaw_old = e->orient[0];
	}
