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
#include "../game/game_state.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "../platform/gfx.h"
#include "entity.h"
#include "../graphics/gfx_settings.h"

static bool entity_client_tick(struct entity* e) {
	assert(e);

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.25F, 0.25F, 0.25F);

	struct AABB tmp = bbox;
	aabb_translate(&tmp, e->pos[0], e->pos[1], e->pos[2]);

	if(entity_aabb_intersection(e, &tmp)) { // is item stuck in block?
		// find possible new position, try top/bottom last
		enum side sides[6] = {SIDE_LEFT, SIDE_RIGHT, SIDE_FRONT,
							  SIDE_BACK, SIDE_TOP,	 SIDE_BOTTOM};
		for(int k = 0; k < 6; k++) {
			int x, y, z;
			blocks_side_offset(sides[k], &x, &y, &z);

			vec3 new_pos;
			glm_vec3_add(e->pos, (vec3) {x, y, z}, new_pos);

			struct AABB tmp2 = tmp;
			aabb_translate(&tmp2, x, y, z);

			if(!entity_aabb_intersection(e, &tmp2)) {
				float threshold;
				entity_intersection_threshold(e, &bbox, new_pos, e->pos,
											  &threshold);
				glm_vec3_lerp(new_pos, e->pos, threshold, e->pos);
				e->vel[0] = x * 0.1F;
				e->vel[1] = y * 0.1F;
				e->vel[2] = z * 0.1F;

				break;
			}
		}
	}

	bool collision_xz = false;

	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	e->vel[1] -= 0.04F;
	e->vel[0] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;
	e->vel[2] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;
	e->vel[1] *= 0.98F;

	e->data.item.age++;
	return false;
}

static size_t getBoundingBox(const struct entity *e, struct AABB *out) {
    assert(e && out);
    aabb_setsize_centered(out, 0.25F, 0.25F, 0.25F);
    aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
    return 1;
}


static bool entity_server_tick(struct entity* e, struct server_local* s) {
	assert(e);

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.25F, 0.25F, 0.25F);

	struct AABB tmp = bbox;
	aabb_translate(&tmp, e->pos[0], e->pos[1], e->pos[2]);

	if(entity_aabb_intersection(e, &tmp)) { // is item stuck in block?
		// find possible new position, try top/bottom last
		enum side sides[6] = {SIDE_LEFT, SIDE_RIGHT, SIDE_FRONT,
							  SIDE_BACK, SIDE_TOP,	 SIDE_BOTTOM};
		for(int k = 0; k < 6; k++) {
			int x, y, z;
			blocks_side_offset(sides[k], &x, &y, &z);

			vec3 new_pos;
			glm_vec3_add(e->pos, (vec3) {x, y, z}, new_pos);

			struct AABB tmp2 = tmp;
			aabb_translate(&tmp2, x, y, z);

			if(!entity_aabb_intersection(e, &tmp2)) {
				float threshold;
				entity_intersection_threshold(e, &bbox, new_pos, e->pos,
											  &threshold);
				glm_vec3_lerp(new_pos, e->pos, threshold, e->pos);
				e->vel[0] = x * 0.1F;
				e->vel[1] = y * 0.1F;
				e->vel[2] = z * 0.1F;

				break;
			}
		}
	}

	bool collision_xz = false;

	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	e->vel[1] -= 0.04F;
	e->vel[0] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;
	e->vel[2] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;
	e->vel[1] *= 0.98F;

	e->data.item.age++;

	if(e->delay_destroy > 0) {
		e->delay_destroy--;
	} else if(e->data.item.age >= 2 * 4) { // allow pickup after 2s
		int best_pid = -1;
		float best_d2 = 0.0f;
		for(int i = 0; i < MAX_SERVER_PLAYERS; i++) {
			struct server_player* p = &s->players[i];
			if(!p->has_pos)
				continue;

			float d2 = glm_vec3_distance2(
				e->pos, (vec3) {p->x, p->y - 0.6F, p->z});
			if(d2 < glm_pow2(2.0F)) {
				if(best_pid < 0 || d2 < best_d2) {
					best_pid = i;
					best_d2 = d2;
				}
			}
		}

		if(best_pid >= 0) {
			struct server_player* p = &s->players[best_pid];

			// TODO: case where item cannot be picked up completely
			if(p->active_inventory && p->active_inventory->logic
			   && p->active_inventory->logic->on_collect) {
				uint8_t old_pid = s->active_player_id;
				s->active_player_id = (uint8_t)best_pid;
				p->active_inventory->logic->on_collect(p->active_inventory,
													  &e->data.item.item);
				s->active_player_id = old_pid;
			}

			clin_rpc_send(&(struct client_rpc) {
				CRPC_PLAYER_ID(best_pid)
				.type = CRPC_PICKUP_ITEM,
				.payload.pickup_item.entity_id = e->id,
				// `collector_id == 0` means: use `player_id` on client side.
				.payload.pickup_item.collector_id = 0,
			});

			e->delay_destroy = 1;
		}
	}

	return e->data.item.age >= 5 * 60 * 20; // destroy after 5 min
}

static void entity_render(struct entity* e, mat4 view, float tick_delta) {
	struct item* it = item_get(&e->data.item.item);

	if(it) {
		vec3 pos_lerp;
		glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

		struct block_data in_block;
		entity_get_block(e, floorf(pos_lerp[0]), floorf(pos_lerp[1]),
						 floorf(pos_lerp[2]), &in_block);
		render_item_update_light((in_block.torch_light << 4)
								 | in_block.sky_light);

		float ticks = e->data.item.age + tick_delta;

		mat4 model;
			glm_translate_make(model, pos_lerp);
		#ifdef GFX_3D_ELEMENTS

			glm_translate_y(model, sinf(ticks / 30.0F * GLM_PIf) * 0.1F + 0.1F);
			glm_rotate_y(model, glm_rad(ticks * 3.0F), model);
			glm_scale_uni(model, 0.25F);
			glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});
		#endif

		mat4 mv;
		glm_mat4_mul(view, model, mv);

		#ifdef GFX_3D_ELEMENTS
			int amount = 1;
			if(e->data.item.item.count > 20) {
				amount = 4;
			} else if(e->data.item.item.count > 5) {
				amount = 3;
			} else if(e->data.item.item.count > 1) {
				amount = 2;
			}
			
					vec3 displacement[4] = {
				{0.0F, 0.0F, 0.0F},
				{-0.701F, -0.331F, -0.239F},
				{0.139F, -0.276F, 0.211F},
				{0.443F, 0.512F, -0.101F},
			};

			for(int k = 0; k < amount; k++) {
				mat4 final;
				glm_translate_make(final, displacement[k]);
				glm_mat4_mul(mv, final, final);

				it->renderItem(it, &e->data.item.item, final, false,
							   R_ITEM_ENV_ENTITY);
			}
		# else
			it->renderItem(it, &e->data.item.item, mv, false,
						 R_ITEM_ENV_ENTITY);
		#endif
		
		struct AABB bbox;
		aabb_setsize_centered(&bbox, 0.25F, 0.25F, 0.25F);
		aabb_translate(&bbox, pos_lerp[0], pos_lerp[1] - 0.04F, pos_lerp[2]);
		entity_shadow(e, &bbox, view);
	}
}

	void entity_item(uint32_t id, struct entity* e, bool server, void* world,
					 struct item_data it) {
		assert(e && world);

	    e->name = "Item";
		e->id = id;
//	e->drop_item = NULL;
	e->tick_server = entity_server_tick;
	e->tick_client = entity_client_tick;
	e->render = entity_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_ITEM;
	e->data.item.age = 0;
	e->data.item.item = it;
	e->getBoundingBox = getBoundingBox;
	    e->leftClickText = NULL;
	    e->onLeftClick   = NULL;
	    e->rightClickText = NULL;
	    e->onRightClick   = NULL;

		entity_default_init(e, server, world);
	}
