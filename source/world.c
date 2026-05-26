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

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "game/game_state.h"
#include "graphics/render_entity.h"
#include "lighting.h"
#include "platform/gfx.h"
#include "platform/time.h"
#include "world.h"

// params depend on fog texture
#define FOG_DIST_NO_RENDER 1.13F
#define FOG_DIST_NO_EFFECT 0.72F

#define FOG_DIST_LESS(c, dist)                                                 \
	(glm_vec2_distance2(                                                       \
		 (vec2) {(c)->x + CHUNK_SIZE / 2, (c)->z + CHUNK_SIZE / 2},            \
		 (vec2) {gstate.camera.x, gstate.camera.z})                            \
	 <= glm_pow2((dist) * gstate.config.fog_distance))

void world_unload_section(struct world* w, w_coord_t x, w_coord_t z) {
	assert(w);

	struct world_section* s
		= dict_wsection_get(w->sections, SECTION_TO_ID(x, z));

	if(s) {
		for(size_t k = 0; k < COLUMN_HEIGHT; k++) {
			struct chunk* c = s->column[k];
			if(c) {
				if(w->world_chunk_cache == c)
					w->world_chunk_cache = NULL;
				chunk_unref(c);
			}
		}

		dict_wsection_erase(w->sections, SECTION_TO_ID(x, z));
	}
}

void world_unload_all(struct world* w) {
	assert(w);

	dict_wsection_it_t it;
	dict_wsection_it(it, w->sections);

	while(!dict_wsection_end_p(it)) {
		struct world_section* s = &dict_wsection_ref(it)->value;
		for(size_t k = 0; k < COLUMN_HEIGHT; k++) {
			if(s->column[k])
				chunk_unref(s->column[k]);
		}

		dict_wsection_next(it);
	}

	stack_clear(&w->lighting_updates);
	dict_wsection_reset(w->sections);
	w->world_chunk_cache = NULL;
}

static void world_bfs(struct world* w, ilist_chunks_t render, float x, float y,
					  float z, vec4* planes) {
	assert(w && render && planes);

	uint32_t stamp = ++w->visit_token;
	if(stamp == 0) {
		w->visit_token = 1;
		stamp = 1;
	}

	enum side sides[6]
		= {SIDE_TOP, SIDE_LEFT, SIDE_BACK, SIDE_BOTTOM, SIDE_RIGHT, SIDE_FRONT};

	struct chunk* c_camera
		= world_find_chunk(w, x, fminf(y, WORLD_HEIGHT - 1), z);

	ilist_chunks_t queue;
	ilist_chunks_init(queue);

	if(!c_camera)
		return;

	ilist_chunks_push_back(queue, c_camera);
	c_camera->tmp_data = (struct chunk_step) {
		.from = SIDE_MAX,
		.used_exit_sides = 0,
		.steps = 0,
		.visit_stamp = stamp,
	};

	while(!ilist_chunks_empty_p(queue)) {
		struct chunk* current = ilist_chunks_pop_front(queue);
		ilist_chunks_push_back(render, current);
		chunk_ref(current);

		for(int s = 0; s < 6; s++) {
			struct chunk* neigh
				= world_find_chunk_neighbour(w, current, sides[s]);

			if(neigh && neigh->tmp_data.visit_stamp != stamp
			   && !(current->tmp_data.used_exit_sides & (1 << sides[s]))
			   && (current->tmp_data.from == SIDE_MAX
				   || current->reachable[current->tmp_data.from]
					   & (1 << sides[s]))
			   && FOG_DIST_LESS(neigh, FOG_DIST_NO_RENDER)
			   && glm_aabb_frustum(
				   (vec3[2]) {{neigh->x, neigh->y, neigh->z},
							  {neigh->x + CHUNK_SIZE, neigh->y + CHUNK_SIZE,
							   neigh->z + CHUNK_SIZE}},
				   planes)) {
				ilist_chunks_push_back(queue, neigh);
				neigh->tmp_data = (struct chunk_step) {
					.from = blocks_side_opposite(sides[s]),
					.used_exit_sides = current->tmp_data.used_exit_sides
						| (1 << blocks_side_opposite(sides[s])),
					.steps = current->tmp_data.steps + (neigh->y < 64) ? 1 : 0,
					.visit_stamp = stamp,
				};
			}
		}
	}
}

void world_create(struct world* w) {
	assert(w);

	dict_wsection_init(w->sections);
	ilist_chunks_init(w->render);
	ilist_chunks2_init(w->gpu_busy_chunks);
	stack_create(&w->lighting_updates, 16,
				 sizeof(struct world_modification_entry));
	w->world_chunk_cache = NULL;
	w->anim_timer = time_get();
	w->visit_token = 1;
}

void world_destroy(struct world* w) {
	assert(w);

	world_unload_all(w);
	stack_destroy(&w->lighting_updates);
	dict_wsection_clear(w->sections);
}

size_t world_loaded_chunks(struct world* w) {
	assert(w);
	return dict_wsection_size(w->sections);
}

struct block_data world_get_block(struct world* w, w_coord_t x, w_coord_t y,
								  w_coord_t z) {
	assert(w);
	struct chunk* c = world_find_chunk(w, x, y, z);

	return c ? chunk_get_block(c, W2C_COORD(x), W2C_COORD(y), W2C_COORD(z)) :
			   (struct block_data) {
				   .type = (y < WORLD_HEIGHT) ? 1 : 0,
				   .metadata = 0,
				   .sky_light = (y < WORLD_HEIGHT) ? 0 : 15,
				   .torch_light = 0,
			   };
}

w_coord_t world_get_height(struct world* w, w_coord_t x, w_coord_t z) {
	assert(w);

	w_coord_t cx = WCOORD_CHUNK_OFFSET(x);
	w_coord_t cz = WCOORD_CHUNK_OFFSET(z);
	struct world_section* s
		= dict_wsection_get(w->sections, SECTION_TO_ID(cx, cz));

	return s ? s->heightmap[W2C_COORD(x) + W2C_COORD(z) * CHUNK_SIZE] : 0;
}

void world_copy_heightmap(struct world* w, struct chunk* c,
						  uint8_t* heightmap) {
	assert(w && c && heightmap);
	struct world_section* s = dict_wsection_get(
		w->sections, SECTION_TO_ID(c->x / CHUNK_SIZE, c->z / CHUNK_SIZE));
	assert(s);

	memcpy(heightmap, s->heightmap, sizeof(s->heightmap));
}

static bool wsection_heightmap_get_block(void* user, c_coord_t x, w_coord_t y,
										 c_coord_t z, struct block_data* blk) {
	assert(user);
	struct world_section* s = user;
	struct chunk* c = s->column[y / CHUNK_SIZE];
	if(c)
		*blk = chunk_get_block(c, x, W2C_COORD(y), z);
	return c;
}

static struct world_section* world_get_or_create_section(struct world* w,
														 w_coord_t cx,
														 w_coord_t cz) {
	assert(w);

	struct world_section* s
		= dict_wsection_get(w->sections, SECTION_TO_ID(cx, cz));
	if(!s) {
		s = dict_wsection_safe_get(w->sections, SECTION_TO_ID(cx, cz));
		assert(s);
		memset(s->heightmap, 0, sizeof(s->heightmap));
		memset(s->column, 0, sizeof(s->column));
	}

	return s;
}

static struct chunk* world_get_or_create_chunk(struct world* w,
											   struct world_section* s,
											   w_coord_t cx, w_coord_t cy,
											   w_coord_t cz) {
	assert(w && s);

	struct chunk* c = s->column[cy];
	if(!c) {
		c = malloc(sizeof(struct chunk));
		assert(c);
		chunk_init(c, w, cx * CHUNK_SIZE, cy * CHUNK_SIZE, cz * CHUNK_SIZE);
		chunk_ref(c);
		s->column[cy] = c;
	}

	return c;
}

static void world_recompute_section_heightmap(struct world_section* s) {
	assert(s);

	for(c_coord_t z = 0; z < CHUNK_SIZE; z++) {
		for(c_coord_t x = 0; x < CHUNK_SIZE; x++) {
			uint8_t height = 0;

			for(int y = WORLD_HEIGHT - 1; y >= 0; y--) {
				struct chunk* c = s->column[y / CHUNK_SIZE];
				if(!c)
					continue;

				struct block_data blk = chunk_get_block(c, x, W2C_COORD(y), z);
				if(blocks[blk.type]
				   && (!blocks[blk.type]->can_see_through
					   || blocks[blk.type]->opacity > 0)) {
					height = (uint8_t)(y + 1);
					break;
				}
			}

			s->heightmap[x + z * CHUNK_SIZE] = height;
		}
	}
}

static void world_mark_column_neighbours_rebuild(struct world* w,
												 w_coord_t cx, w_coord_t cz) {
	assert(w);

	static const int offsets[4][2] = {
		{-1, 0},
		{1, 0},
		{0, -1},
		{0, 1},
	};

	for(size_t i = 0; i < 4; i++) {
		struct world_section* s = dict_wsection_get(
			w->sections, SECTION_TO_ID(cx + offsets[i][0], cz + offsets[i][1]));
		if(!s)
			continue;

		for(size_t cy = 0; cy < COLUMN_HEIGHT; cy++) {
			if(s->column[cy])
				s->column[cy]->rebuild_displist = true;
		}
	}
}

void world_set_block(struct world* w, w_coord_t x, w_coord_t y, w_coord_t z,
					 struct block_data blk, bool light_update) {
	assert(w);

	if(y < 0 || y >= WORLD_HEIGHT)
		return;

	if(light_update) {
		stack_push(&w->lighting_updates,
				   &(struct world_modification_entry) {
					   .x = x,
					   .y = y,
					   .z = z,
					   .blk = blk,
				   });
	} else {
		w_coord_t cx = WCOORD_CHUNK_OFFSET(x);
		w_coord_t cz = WCOORD_CHUNK_OFFSET(z);
		struct world_section* s
			= dict_wsection_get(w->sections, SECTION_TO_ID(cx, cz));
		struct chunk* c = world_chunk_from_section(w, s, y);

		if(!c) {
			c = malloc(sizeof(struct chunk));
			assert(c);

			w_coord_t cy = y / CHUNK_SIZE;
			chunk_init(c, w, cx * CHUNK_SIZE, cy * CHUNK_SIZE, cz * CHUNK_SIZE);
			chunk_ref(c);

			w->world_chunk_cache = c;

			if(!s) {
				s = dict_wsection_safe_get(w->sections, SECTION_TO_ID(cx, cz));
				assert(s);
				memset(s->heightmap, 0, sizeof(s->heightmap));
				memset(s->column, 0, sizeof(s->column));
			}

			assert(s->column[cy] == NULL);
			s->column[cy] = c;
		}

		chunk_set_block(c, W2C_COORD(x), W2C_COORD(y), W2C_COORD(z), blk);
		lighting_heightmap_update(s->heightmap, W2C_COORD(x), y, W2C_COORD(z),
								  blk.type, wsection_heightmap_get_block, s);
	}
}

bool world_import_chunk_column(struct world* w, w_coord_t x, w_coord_t y,
							   w_coord_t z, w_coord_t sx, w_coord_t sy,
							   w_coord_t sz, const uint8_t* ids,
							   const uint8_t* metadata,
							   const uint8_t* lighting_sky,
							   const uint8_t* lighting_torch) {
	assert(w);

	if(x % CHUNK_SIZE != 0 || z % CHUNK_SIZE != 0 || y != 0
	   || sx != CHUNK_SIZE || sy != WORLD_HEIGHT || sz != CHUNK_SIZE
	   || !ids || !metadata || !lighting_sky || !lighting_torch)
		return false;

	w_coord_t cx = WCOORD_CHUNK_OFFSET(x);
	w_coord_t cz = WCOORD_CHUNK_OFFSET(z);
	struct world_section* s = world_get_or_create_section(w, cx, cz);

	for(w_coord_t cy = 0; cy < COLUMN_HEIGHT; cy++) {
		struct chunk* c = world_get_or_create_chunk(w, s, cx, cy, cz);
		c->rebuild_displist = true;
		c->has_spawner = false;

		for(c_coord_t lx = 0; lx < CHUNK_SIZE; lx++) {
			for(c_coord_t lz = 0; lz < CHUNK_SIZE; lz++) {
				for(c_coord_t ly = 0; ly < CHUNK_SIZE; ly++) {
					size_t world_y = (size_t)cy * CHUNK_SIZE + ly;
					size_t idx = (size_t)lx * CHUNK_SIZE * WORLD_HEIGHT
						+ (size_t)lz * WORLD_HEIGHT + world_y;
					uint8_t md = (metadata[idx / 2] >> ((idx & 1u) * 4)) & 0xF;
					uint8_t sky
						= (lighting_sky[idx / 2] >> ((idx & 1u) * 4)) & 0xF;
					uint8_t torch
						= (lighting_torch[idx / 2] >> ((idx & 1u) * 4)) & 0xF;

					chunk_set_block_raw(c, lx, ly, lz,
										(struct block_data) {
											.type = ids[idx],
											.metadata = md,
											.sky_light = sky,
											.torch_light = torch,
										});
				}
			}
		}
	}

	world_recompute_section_heightmap(s);
	world_mark_column_neighbours_rebuild(w, cx, cz);
	w->world_chunk_cache = s->column[0];
	return true;
}

void world_redraw_chunks(struct world* w) {
	assert(w);

	dict_wsection_it_t it;
	dict_wsection_it(it, w->sections);

	while(!dict_wsection_end_p(it)) {
		for(int i = 0; i < COLUMN_HEIGHT; i++) {
			(&dict_wsection_ref(it)->value)->column[i]->rebuild_displist = true;
		}
		dict_wsection_next(it);
	}
}



static bool world_light_get_block(void* user, w_coord_t x, w_coord_t y,
								  w_coord_t z, struct block_data* blk,
								  uint8_t* height) {
	assert(user);
	struct world* w = user;

	struct world_section* s = dict_wsection_get(
		w->sections,
		SECTION_TO_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));

	if(!s)
		return false;

	struct chunk* c = world_chunk_from_section(w, s, y);

	if(!c)
		return false;

	if(blk)
		*blk = chunk_get_block(c, W2C_COORD(x), W2C_COORD(y), W2C_COORD(z));

	if(height)
		*height = s->heightmap[W2C_COORD(x) + W2C_COORD(z) * CHUNK_SIZE];

	return true;
}

static void world_light_set_light(void* user, w_coord_t x, w_coord_t y,
								  w_coord_t z, uint8_t light) {
	assert(user);
	struct chunk* c = world_find_chunk((struct world*)user, x, y, z);
	assert(c);

	chunk_set_light(c, W2C_COORD(x), W2C_COORD(y), W2C_COORD(z), light);
}

void world_update_lighting(struct world* w) {
	assert(w);

	if(stack_empty(&w->lighting_updates))
		return;

	struct world_modification_entry source;
	stack_pop(&w->lighting_updates, &source);

	world_set_block(w, source.x, source.y, source.z, source.blk, false);
	lighting_update_at_block(source, false, world_light_get_block,
							 world_light_set_light, w);
}

struct chunk* world_find_chunk_neighbour(struct world* w, struct chunk* c,
										 enum side s) {
	assert(w && c);

	int x, y, z;
	blocks_side_offset(s, &x, &y, &z);

	if(y + c->y / CHUNK_SIZE < 0
	   || y + c->y / CHUNK_SIZE >= WORLD_HEIGHT / CHUNK_SIZE)
		return NULL;

	struct world_section* res = dict_wsection_get(
		w->sections,
		SECTION_TO_ID(x + c->x / CHUNK_SIZE, z + c->z / CHUNK_SIZE));

	return res ? res->column[y + c->y / CHUNK_SIZE] : NULL;
}

struct chunk* world_chunk_from_section(struct world* w, struct world_section* s,
									   w_coord_t y) {
	assert(w);
	return (y >= 0 && y < WORLD_HEIGHT && s) ? s->column[y / CHUNK_SIZE] : NULL;
}

struct chunk* world_find_chunk(struct world* w, w_coord_t x, w_coord_t y,
							   w_coord_t z) {
	assert(w);

	if(y < 0 || y >= WORLD_HEIGHT)
		return NULL;

	int cx = WCOORD_CHUNK_OFFSET(x);
	int cy = y / CHUNK_SIZE;
	int cz = WCOORD_CHUNK_OFFSET(z);

	if(w->world_chunk_cache && cx == w->world_chunk_cache->x / CHUNK_SIZE
	   && cy == w->world_chunk_cache->y / CHUNK_SIZE
	   && cz == w->world_chunk_cache->z / CHUNK_SIZE)
		return w->world_chunk_cache;

	struct world_section* res
		= dict_wsection_get(w->sections, SECTION_TO_ID(cx, cz));

	if(res)
		w->world_chunk_cache = res->column[cy];

	return res ? res->column[cy] : NULL;
}

void world_preload(struct world* w,
				   void (*progress)(struct world* w, float percent)) {
	assert(w);

	dict_wsection_it_t it;
	dict_wsection_it(it, w->sections);

	size_t total = dict_wsection_size(w->sections);
	size_t count = 0;

	while(!dict_wsection_end_p(it)) {
		struct world_section* s = &dict_wsection_ref(it)->value;
		for(size_t k = 0; k < COLUMN_HEIGHT; k++) {
			if(s->column[k])
				chunk_check_built(s->column[k]);
		}

		if(progress)
			progress(w, (float)count / (float)total);
		count++;
		dict_wsection_next(it);
	}
}

bool world_block_intersection(struct world* w, struct ray* r, w_coord_t x,
							  w_coord_t y, w_coord_t z, enum side* s) {
	assert(w && r && s);

	struct block_data blk = world_get_block(w, x, y, z);

	if(blocks[blk.type]) {
		struct block_data neighbours[SIDE_MAX];

		for(int k = 0; k < SIDE_MAX; k++) {
			int ox, oy, oz;
			blocks_side_offset((enum side)k, &ox, &oy, &oz);

			neighbours[k] = world_get_block(w, x + ox, y + oy, z + oz);
		}

		struct block_info blk_info = (struct block_info) {
			.block = &blk,
			.neighbours = neighbours,
			.x = x,
			.y = y,
			.z = z,
		};

		size_t count = blocks[blk.type]->getBoundingBox(&blk_info, false, NULL);
		if(count > 0) {
			struct AABB bbox[count];
			blocks[blk.type]->getBoundingBox(&blk_info, false, bbox);

			for(size_t k = 0; k < count; k++) {
				aabb_translate(bbox + k, x, y, z);

				if(aabb_intersection_ray(bbox + k, r, s))
					return true;
			}
		}
	}

	return false;
}

void world_pre_render(struct world* w, struct camera* c, mat4 view) {
	assert(w && c && view);

	ilist_chunks_init(w->render);
	world_bfs(w, w->render, c->x, c->y, c->z, c->frustum_planes);

	ilist_chunks_it_t it;
	ilist_chunks_it(it, w->render);

	while(!ilist_chunks_end_p(it)) {
		struct chunk* c = ilist_chunks_ref(it);
		bool has_fog = !FOG_DIST_LESS(c, FOG_DIST_NO_EFFECT);

		chunk_pre_render(c, view, has_fog);

		if(has_fog) {
			ilist_chunks_remove(w->render, it);
			ilist_chunks_push_front(w->render, c);
		} else {
			ilist_chunks_next(it);
		}
	}
}

void world_pre_render_clear(struct world* w) {
	assert(w);
	ilist_chunks_init(w->render);
}

size_t world_build_chunks(struct world* w, size_t tokens) {
	ilist_chunks_it_t it;
	ilist_chunks_it(it, w->render);

	while(tokens > 0 && !ilist_chunks_end_p(it)) {
		if(chunk_check_built(ilist_chunks_ref(it)))
			tokens--;
		ilist_chunks_next(it);
	}

	dict_wsection_it_t it2;
	dict_wsection_it(it2, w->sections);

	while(tokens > 0 && !dict_wsection_end_p(it2)) {
		struct world_section* s = &dict_wsection_ref(it2)->value;
		for(size_t k = 0; k < COLUMN_HEIGHT; k++) {
			if(s->column[k] && chunk_check_built(s->column[k]))
				tokens--;
		}

		dict_wsection_next(it2);
	}

	return tokens;
}

void world_render_completed(struct world* w, bool new_render) {
	assert(w);

	ilist_chunks2_it_t it;
	ilist_chunks2_it(it, w->gpu_busy_chunks);

	while(!ilist_chunks2_end_p(it)) {
		struct chunk* c = ilist_chunks2_ref(it);
		ilist_chunks2_remove(w->gpu_busy_chunks, it);
		chunk_unref(c);
	}

	ilist_chunks2_reset(w->gpu_busy_chunks);

	if(new_render) {
		ilist_chunks_it_t it2;
		ilist_chunks_it(it2, w->render);

		while(!ilist_chunks_end_p(it2)) {
			// move imaginary reference token from "render" to "gpu_busy_chunks"
			ilist_chunks2_push_back(w->gpu_busy_chunks, ilist_chunks_ref(it2));
			ilist_chunks_next(it2);
		}
	}
}

static void world_render_spawner_mob(struct block_data* blk, mat4 view,
									 w_coord_t x, w_coord_t y, w_coord_t z) {
	assert(blk && view);

	uint8_t spawner_type = blk->metadata & 0xF;
	if(spawner_type == 0)
		return;

	int32_t ms = time_diff_ms(gstate.world_time_start, time_get());
	float spin_y = fmodf((float)ms * 0.5f, 360.0f);  // speed
	float spin_x = sinf((float)ms * 0.035f) * 22.0f;
	float spin_z = cosf((float)ms * 0.027f) * 18.0f;
	int frame = (ms / 40) % 60;

	render_entity_update_light((blk->torch_light << 4) | blk->sky_light);

	mat4 model, mv;
	glm_translate_make(model, (vec3) {x + 0.5f, y + 0.14f, z + 0.5f});
	glm_rotate_y(model, glm_rad(spin_y), model);
	glm_rotate_x(model, glm_rad(spin_x), model);
	glm_rotate_z(model, glm_rad(spin_z), model);
	glm_scale_uni(model, 0.28f);
	glm_mat4_mul(view, model, mv);

	switch(spawner_type) {
		case 1:
			render_entity_creeper(mv, spin_y, spin_y, frame);
			break;
		case 2:
			render_entity_pig(mv, spin_y, frame);
			break;
		case 3:
			render_entity_sheep(mv, spin_y, frame, false);
			break;
		default:
			break;
	}
}

static void world_render_spawners(struct world* w, struct camera* c) {
	assert(w && c);

	ilist_chunks_it_t it;
	ilist_chunks_it(it, w->render);

	while(!ilist_chunks_end_p(it)) {
		struct chunk* chunk = ilist_chunks_ref(it);
		if(!chunk->has_spawner) {
			ilist_chunks_next(it);
			continue;
		}

		for(c_coord_t cy = 0; cy < CHUNK_SIZE; cy++) {
			for(c_coord_t cz = 0; cz < CHUNK_SIZE; cz++) {
				for(c_coord_t cx = 0; cx < CHUNK_SIZE; cx++) {
					struct block_data blk = chunk_get_block(chunk, cx, cy, cz);
					if(blk.type != BLOCK_SPAWNER || blk.metadata == 0)
						continue;

					world_render_spawner_mob(&blk, c->view, chunk->x + cx,
											 chunk->y + cy, chunk->z + cz);
				}
			}
		}

		ilist_chunks_next(it);
	}
}

static void world_render_enchanting_books(struct world* w, struct camera* c) {
	assert(w && c);

	// Book texture region in particles.png (assets/particles.png is 128x128).
	// Cover stays in the "book strip", but pages use the dedicated page UVs
	// requested by the user:
	// - page 1: x 2..13,  y 112..128
	// - page 2: x 13..14, y 112..128
	const float inv = 1.0f / 128.0f;
	const float cover_u0 = 0.0f * inv;
	const float cover_u1 = 68.0f * inv;
	const float cover_um = 34.0f * inv;
	const float v_cover0 = 90.0f * inv;
	const float v_cover1 = 110.0f * inv;
	const float pages_u10 = 2.0f * inv;
	const float pages_u11 = 13.0f * inv;
	const float pages_u20 = 13.0f * inv;
	const float pages_u21 = 14.0f * inv;
	const float v_pages0 = 112.0f * inv;
	const float v_pages1 = 128.0f * inv;

	gfx_matrix_modelview(c->view);
	// Use the raw particles.png. The normal particle system uses an atlased
	// texture, which does not preserve original pixel coordinates.
	gfx_bind_texture(&texture_particles_raw);
	gfx_alpha_test(true);
	gfx_lighting(false);
	gfx_cull_func(MODE_NONE);

	const int32_t ms = time_diff_ms(w->anim_timer, time_get());
	const float open_base_anim = (0.55f + 0.15f * sinf((float)ms * 0.0013f));
	//bool debug_printed_this_frame = false;

	// Flip state machine:
	// 1) target angle is calculated
	// 2) angle is increased every update by speed
	// 3) when target is reached, move to next page, total 3 pages
	static bool flip_active = false;
	static int32_t flip_last_ms = -1;
	static int32_t flip_last_burst_ms = -10000;
	static int flip_page_index = 0; // 0..2
	static float flip_angle = 0.0f;
	static float flip_start_angle = 0.0f;
	static float flip_target = 0.0f;
	const float flip_page_speed = 0.0042f; // radians per millisecond (turning page only)
	const int32_t flip_burst_pause_ms = 10000;

	if(flip_last_ms < 0)
		flip_last_ms = ms;

	int32_t dt_ms = ms - flip_last_ms;
	if(dt_ms < 0)
		dt_ms = 0;
	if(dt_ms > 200)
		dt_ms = 200;
	flip_last_ms = ms;

	if(!flip_active && (ms - flip_last_burst_ms) >= flip_burst_pause_ms) {
		flip_active = true;
		flip_page_index = 0;
		flip_start_angle = -open_base_anim;
		flip_target = +open_base_anim;
		flip_angle = flip_start_angle;
	}

	if(flip_active) {
		const float dir = (flip_target >= flip_start_angle) ? 1.0f : -1.0f;
		const float step = fabsf(flip_page_speed) * (float)dt_ms;
		flip_angle += dir * step;

		const bool reached
			= (dir > 0.0f && flip_angle >= flip_target)
			  || (dir < 0.0f && flip_angle <= flip_target);
		if(reached) {
			flip_angle = flip_target;
			flip_page_index++;
			if(flip_page_index >= 3) {
				flip_active = false;
				flip_last_burst_ms = ms;
			} else {
				// New page starts exactly at left wing pose, then rotates to right wing pose.
				flip_start_angle = -open_base_anim;
				flip_target = +open_base_anim;
				flip_angle = flip_start_angle;
			}
		}
	}

	ilist_chunks_it_t it;
	ilist_chunks_it(it, w->render);

	while(!ilist_chunks_end_p(it)) {
		struct chunk* chunk = ilist_chunks_ref(it);
		if(!chunk->has_enchanting_table) {
			ilist_chunks_next(it);
			continue;
		}

		const float open_base = (open_base_anim);
		const float lift = 0.015f;   // outer edge lift
		const float bend = 0.010f;   // inner edge lift

		for(c_coord_t cy = 0; cy < CHUNK_SIZE; cy++) {
			for(c_coord_t cz = 0; cz < CHUNK_SIZE; cz++) {
				for(c_coord_t cx = 0; cx < CHUNK_SIZE; cx++) {
					struct block_data blk = chunk_get_block(chunk, cx, cy, cz);
					if(blk.type != BLOCK_ENCHANTING_TABLE)
						continue;

					const float x = (float)(chunk->x + cx);
					const float y = (float)(chunk->y + cy);
					const float z = (float)(chunk->z + cz);

					// Render an "opened book" (two pages + one flipping page).
					const float cx0 = x + 0.5f;
					const float cz0 = z + 0.5f;
					const float y_base = y + 0.77f;
					const float y_top = y + 0.90f;
					const float page_len = 0.26f; // along X
					const float page_w = 0.18f;   // along Z

					// A slight overall tilt so it sits like in Minecraft.
					const float tilt = -0.22f; // radians (down towards player)

					// NOTE: We render with lighting disabled (like other overlays),
					// so use dimmed vertex colors to avoid a "glowing white" look.
					const uint8_t color_pages[16] = {
						0xD8, 0xD8, 0xD8, 0xFF,
						0xD8, 0xD8, 0xD8, 0xFF,
						0xD8, 0xD8, 0xD8, 0xFF,
						0xD8, 0xD8, 0xD8, 0xFF,
					};
						const uint8_t color_cover[16] = {
							0xB8, 0xB8, 0xB8, 0xFF,
						0xB8, 0xB8, 0xB8, 0xFF,
						0xB8, 0xB8, 0xB8, 0xFF,
							0xB8, 0xB8, 0xB8, 0xFF,
						};

					// Helper: transform local (lx,lz) with a Y rotation and an overall tilt
					// into world coordinates, and give us a "page-like" varying Y.
					#define BOOK_PT3D(outx, outy, outz, lx, ly, lz, yaw, fold, base_lift) do { \
						const float ca = cosf((yaw)), sa = sinf((yaw)); \
						float tx = (lx) * ca - (lz) * sa; \
						float tz = (lx) * sa + (lz) * ca; \
						/* tilt around X axis */ \
						const float ct = cosf(tilt), st = sinf(tilt); \
						float ty0 = (ly); \
						float tz2 = tz * ct - ty0 * st; \
						float ty2 = tz * st + ty0 * ct; \
						float wx = cx0 + tx; \
						float wy = (y_base + (y_top - y_base)) + ty2 + (base_lift); \
						float wz = cz0 + tz2; \
						/* fold around spine at lx=0 (rotate in X-Y plane around world point at same (ly,lz) but lx=0) */ \
						{ \
							float hx, hy, hz; \
							/* hinge point */ \
							const float txh = (0.0f) * ca - (lz) * sa; \
							const float tzh = (0.0f) * sa + (lz) * ca; \
							const float tzh2 = tzh * ct - ty0 * st; \
							const float tyh2 = tzh * st + ty0 * ct; \
							hx = cx0 + txh; \
							hy = (y_base + (y_top - y_base)) + tyh2 + (base_lift); \
							hz = cz0 + tzh2; \
							const float cu = cosf((fold)); \
							const float su = sinf((fold)); \
							const float dx = wx - hx; \
							const float dy = wy - hy; \
							wx = hx + dx * cu - dy * su; \
							wy = hy + dx * su + dy * cu; \
							wz = wz; \
						} \
						(outx) = wx; \
						(outy) = wy; \
						(outz) = wz; \
					} while(0)

					// User-provided UV rectangles (particles.png, pixel coords).
					// - top:    2,112 .. 12,128
					// - bottom: 14,122 .. 24,128
					// - left:   0,112 .. 2,128
					// - right:  12,112 .. 14,128
					// - other:  2,110 .. 12,112
					const float uv_top_u0 = 2.0f * inv,  uv_top_u1 = 12.0f * inv;
					const float uv_top_v0 = 112.0f * inv, uv_top_v1 = 128.0f * inv;
					const float uv_bot_u0 = 14.0f * inv, uv_bot_u1 = 24.0f * inv;
					const float uv_bot_v0 = 122.0f * inv, uv_bot_v1 = 128.0f * inv;
					const float uv_l_u0 = 0.0f * inv,   uv_l_u1 = 2.0f * inv;
					const float uv_l_v0 = 112.0f * inv, uv_l_v1 = 128.0f * inv;
					const float uv_r_u0 = 12.0f * inv,  uv_r_u1 = 14.0f * inv;
					const float uv_r_v0 = 112.0f * inv, uv_r_v1 = 128.0f * inv;
					const float uv_o_u0 = 2.0f * inv,   uv_o_u1 = 12.0f * inv;
					const float uv_o_v0 = 110.0f * inv, uv_o_v1 = 112.0f * inv;

					// "Leather" underlay under the book (particles.png, pixel coords).
					// Requested:
					// - top:    0,90  .. 13,110
					// - bottom: 13,90 .. 23,110
					// - left:   43,90 .. 44,110
					// - other:  55,90 .. 56,110
					const float uv_le_top_u0 = 0.0f * inv,   uv_le_top_u1 = 13.0f * inv;
					const float uv_le_top_v0 = 90.0f * inv,  uv_le_top_v1 = 110.0f * inv;
					const float uv_le_bot_u0 = 13.0f * inv,  uv_le_bot_u1 = 23.0f * inv;
					const float uv_le_bot_v0 = 90.0f * inv,  uv_le_bot_v1 = 110.0f * inv;
					const float uv_le_l_u0 = 43.0f * inv,    uv_le_l_u1 = 44.0f * inv;
					const float uv_le_l_v0 = 90.0f * inv,    uv_le_l_v1 = 110.0f * inv;
					const float uv_le_o_u0 = 55.0f * inv,    uv_le_o_u1 = 56.0f * inv;
					const float uv_le_o_v0 = 90.0f * inv,    uv_le_o_v1 = 110.0f * inv;

					const uint8_t color_leather[16] = {
						0xB0, 0xA0, 0x78, 0xFF,
						0xB0, 0xA0, 0x78, 0xFF,
						0xB0, 0xA0, 0x78, 0xFF,
						0xB0, 0xA0, 0x78, 0xFF,
					};

					#define DRAW_QUAD4(x0,y0,z0, x1,y1,z1, x2,y2,z2, x3,y3,z3, uu0,vv0, uu1,vv1, col) do { \
						const float verts[12] = { \
							(x0),(y0),(z0), \
							(x1),(y1),(z1), \
							(x2),(y2),(z2), \
							(x3),(y3),(z3), \
						}; \
						const float uvs[8] = { (uu0),(vv0), (uu0),(vv1), (uu1),(vv1), (uu1),(vv0) }; \
						gfx_draw_quads_flt(4, verts, (col), uvs); \
					} while(0)
					#define DRAW_QUAD4_ROT90(x0,y0,z0, x1,y1,z1, x2,y2,z2, x3,y3,z3, uu0,vv0, uu1,vv1, col) do { \
						const float verts[12] = { \
							(x0),(y0),(z0), \
							(x1),(y1),(z1), \
							(x2),(y2),(z2), \
							(x3),(y3),(z3), \
						}; \
						/* Rotate UVs by 90 degrees (clockwise) */ \
						const float uvs[8] = { (uu0),(vv1), (uu1),(vv1), (uu1),(vv0), (uu0),(vv0) }; \
						gfx_draw_quads_flt(4, verts, (col), uvs); \
					} while(0)

					// Draw one "wing" as a thin cuboid hinged at lx=0.
					#define DRAW_WING(lx0, lx1, fold, col) do { \
						const float lz0 = -page_w; \
						const float lz1 = page_w; \
						const float thick = 0.060f; \
						const float ly0 = -thick; \
						const float ly1 = 0.0f; \
						const float yaw = 0.0f; \
						const float lift0 = 0.010f; \
						float a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z; \
						float b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z; \
						/* top face (ly1) */ \
						BOOK_PT3D(a0x,a0y,a0z, (lx0), ly1, lz0, yaw, (fold), lift0); \
						BOOK_PT3D(a1x,a1y,a1z, (lx1), ly1, lz0, yaw, (fold), lift0); \
						BOOK_PT3D(a2x,a2y,a2z, (lx1), ly1, lz1, yaw, (fold), lift0); \
						BOOK_PT3D(a3x,a3y,a3z, (lx0), ly1, lz1, yaw, (fold), lift0); \
						DRAW_QUAD4_ROT90(a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z, uv_top_u0,uv_top_v0, uv_top_u1,uv_top_v1, (col)); \
						/* bottom face (ly0) */ \
						BOOK_PT3D(b0x,b0y,b0z, (lx0), ly0, lz1, yaw, (fold), lift0); \
						BOOK_PT3D(b1x,b1y,b1z, (lx1), ly0, lz1, yaw, (fold), lift0); \
						BOOK_PT3D(b2x,b2y,b2z, (lx1), ly0, lz0, yaw, (fold), lift0); \
						BOOK_PT3D(b3x,b3y,b3z, (lx0), ly0, lz0, yaw, (fold), lift0); \
						DRAW_QUAD4_ROT90(b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z, uv_bot_u0,uv_bot_v0, uv_bot_u1,uv_bot_v1, (col)); \
						/* left edge (lx0) */ \
						DRAW_QUAD4_ROT90(a0x,a0y,a0z, a3x,a3y,a3z, b0x,b0y,b0z, b3x,b3y,b3z, uv_l_u0,uv_l_v0, uv_l_u1,uv_l_v1, (col)); \
						/* right edge (lx1) */ \
						DRAW_QUAD4(a2x,a2y,a2z, a1x,a1y,a1z, b2x,b2y,b2z, b1x,b1y,b1z, uv_r_u0,uv_r_v0, uv_r_u1,uv_r_v1, (col)); \
						/* front/back edges (lz0/lz1) */ \
						DRAW_QUAD4(a1x,a1y,a1z, a0x,a0y,a0z, b3x,b3y,b3z, b2x,b2y,b2z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
								DRAW_QUAD4_ROT90(a3x,a3y,a3z, a2x,a2y,a2z, b1x,b1y,b1z, b0x,b0y,b0z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
					} while(0)

						// Swing-page variant: dedicated macro so edits here do not affect
						// any static/non-moving page rendering.
							#define DRAW_PAGE_RANGE_SWING(lx0, lx1, fold, extra_lift, col) do { \
						const float lz0 = -page_w + 0.010f; \
						const float lz1 = page_w - 0.010f; \
						const float thick = 0.006f; \
						const float ly0 = 0.001f; \
						const float ly1 = ly0 + thick; \
						const float yaw = 0.0f; \
						const float base_lift = 0.012f + (extra_lift); \
						float a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z; \
						float b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z; \
						/* top face */ \
						BOOK_PT3D(a0x,a0y,a0z, (lx1), ly1, lz0, yaw, (fold), base_lift); \
						BOOK_PT3D(a1x,a1y,a1z, (lx0), ly1, lz0, yaw, (fold), base_lift); \
						BOOK_PT3D(a2x,a2y,a2z, (lx0), ly1, lz1, yaw, (fold), base_lift); \
						BOOK_PT3D(a3x,a3y,a3z, (lx1), ly1, lz1, yaw, (fold), base_lift); \
						DRAW_QUAD4_ROT90(a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z, uv_top_u0,uv_top_v0, uv_top_u1,uv_top_v1, (col)); \
						/* bottom face */ \
						BOOK_PT3D(b0x,b0y,b0z, (lx1), ly0, lz1, yaw, (fold), base_lift); \
						BOOK_PT3D(b1x,b1y,b1z, (lx0), ly0, lz1, yaw, (fold), base_lift); \
						BOOK_PT3D(b2x,b2y,b2z, (lx0), ly0, lz0, yaw, (fold), base_lift); \
						BOOK_PT3D(b3x,b3y,b3z, (lx1), ly0, lz0, yaw, (fold), base_lift); \
						DRAW_QUAD4_ROT90(b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z, uv_bot_u1,uv_bot_v0, uv_bot_u0,uv_bot_v1, (col)); \
						/* thin edges */ \
						DRAW_QUAD4(a0x,a0y,a0z, a3x,a3y,a3z, b0x,b0y,b0z, b3x,b3y,b3z, uv_l_u0,uv_l_v0, uv_l_u1,uv_l_v1, (col)); \
						DRAW_QUAD4_ROT90(a2x,a2y,a2z, a1x,a1y,a1z, b2x,b2y,b2z, b1x,b1y,b1z, uv_r_u0,uv_r_v0, uv_r_u1,uv_r_v1, (col)); \
						DRAW_QUAD4_ROT90(a1x,a1y,a1z, a0x,a0y,a0z, b3x,b3y,b3z, b2x,b2y,b2z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
							DRAW_QUAD4(a3x,a3y,a3z, a2x,a2y,a2z, b1x,b1y,b1z, b0x,b0y,b0z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
							} while(0)

						// Static-page variant (kept stable): all faces rotated 90deg.
						#define DRAW_PAGE_RANGE_STATIC(lx0, lx1, fold, extra_lift, col) do { \
							const float lz0 = -page_w + 0.010f; \
							const float lz1 = page_w - 0.010f; \
							const float thick = 0.006f; \
							const float ly0 = 0.001f; \
							const float ly1 = ly0 + thick; \
							const float yaw = 0.0f; \
							const float base_lift = 0.012f + (extra_lift); \
							float a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z; \
							float b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z; \
							BOOK_PT3D(a0x,a0y,a0z, (lx1), ly1, lz0, yaw, (fold), base_lift); \
							BOOK_PT3D(a1x,a1y,a1z, (lx0), ly1, lz0, yaw, (fold), base_lift); \
							BOOK_PT3D(a2x,a2y,a2z, (lx0), ly1, lz1, yaw, (fold), base_lift); \
							BOOK_PT3D(a3x,a3y,a3z, (lx1), ly1, lz1, yaw, (fold), base_lift); \
							DRAW_QUAD4_ROT90(a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z, uv_top_u0,uv_top_v0, uv_top_u1,uv_top_v1, (col)); \
							BOOK_PT3D(b0x,b0y,b0z, (lx1), ly0, lz1, yaw, (fold), base_lift); \
							BOOK_PT3D(b1x,b1y,b1z, (lx0), ly0, lz1, yaw, (fold), base_lift); \
							BOOK_PT3D(b2x,b2y,b2z, (lx0), ly0, lz0, yaw, (fold), base_lift); \
							BOOK_PT3D(b3x,b3y,b3z, (lx1), ly0, lz0, yaw, (fold), base_lift); \
							DRAW_QUAD4_ROT90(b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z, uv_bot_u1,uv_bot_v0, uv_bot_u0,uv_bot_v1, (col)); \
							DRAW_QUAD4_ROT90(a0x,a0y,a0z, a3x,a3y,a3z, b0x,b0y,b0z, b3x,b3y,b3z, uv_l_u0,uv_l_v0, uv_l_u1,uv_l_v1, (col)); \
							DRAW_QUAD4_ROT90(a2x,a2y,a2z, a1x,a1y,a1z, b2x,b2y,b2z, b1x,b1y,b1z, uv_r_u0,uv_r_v0, uv_r_u1,uv_r_v1, (col)); \
							DRAW_QUAD4_ROT90(a1x,a1y,a1z, a0x,a0y,a0z, b3x,b3y,b3z, b2x,b2y,b2z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
							DRAW_QUAD4_ROT90(a3x,a3y,a3z, a2x,a2y,a2z, b1x,b1y,b1z, b0x,b0y,b0z, uv_o_u0,uv_o_v0, uv_o_u1,uv_o_v1, (col)); \
						} while(0)

					// Draw a leather underlay attached to the wing, slightly larger and swinging.
					#define DRAW_LEATHER(lx0, lx1, fold) do { \
						const float lz0 = -page_w - 0.015f; \
						const float lz1 = page_w + 0.015f; \
						const float thick = 0.018f; \
						const float ly0 = -0.060f - thick; \
						const float ly1 = -0.060f; \
						const float yaw = 0.0f; \
						const float base_lift = 0.006f; \
						/* Keep leather perfectly parallel to the wing (same fold angle). */ \
						const float lf = (fold); \
						float a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z; \
						float b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z; \
						/* top face */ \
						BOOK_PT3D(a0x,a0y,a0z, (lx0), ly1, lz0, yaw, (lf), base_lift); \
						BOOK_PT3D(a1x,a1y,a1z, (lx1), ly1, lz0, yaw, (lf), base_lift); \
						BOOK_PT3D(a2x,a2y,a2z, (lx1), ly1, lz1, yaw, (lf), base_lift); \
						BOOK_PT3D(a3x,a3y,a3z, (lx0), ly1, lz1, yaw, (lf), base_lift); \
						DRAW_QUAD4(a0x,a0y,a0z, a1x,a1y,a1z, a2x,a2y,a2z, a3x,a3y,a3z, uv_le_top_u0,uv_le_top_v0, uv_le_top_u1,uv_le_top_v1, color_leather); \
						/* bottom face */ \
						BOOK_PT3D(b0x,b0y,b0z, (lx0), ly0, lz1, yaw, (lf), base_lift); \
						BOOK_PT3D(b1x,b1y,b1z, (lx1), ly0, lz1, yaw, (lf), base_lift); \
						BOOK_PT3D(b2x,b2y,b2z, (lx1), ly0, lz0, yaw, (lf), base_lift); \
						BOOK_PT3D(b3x,b3y,b3z, (lx0), ly0, lz0, yaw, (lf), base_lift); \
						DRAW_QUAD4(b0x,b0y,b0z, b1x,b1y,b1z, b2x,b2y,b2z, b3x,b3y,b3z, uv_le_bot_u0,uv_le_bot_v0, uv_le_bot_u1,uv_le_bot_v1, color_leather); \
						/* left edge */ \
						DRAW_QUAD4(a0x,a0y,a0z, a3x,a3y,a3z, b0x,b0y,b0z, b3x,b3y,b3z, uv_le_l_u0,uv_le_l_v0, uv_le_l_u1,uv_le_l_v1, color_leather); \
						/* remaining sides */ \
						DRAW_QUAD4(a2x,a2y,a2z, a1x,a1y,a1z, b2x,b2y,b2z, b1x,b1y,b1z, uv_le_o_u0,uv_le_o_v0, uv_le_o_u1,uv_le_o_v1, color_leather); \
						DRAW_QUAD4(a1x,a1y,a1z, a0x,a0y,a0z, b3x,b3y,b3z, b2x,b2y,b2z, uv_le_o_u0,uv_le_o_v0, uv_le_o_u1,uv_le_o_v1, color_leather); \
						DRAW_QUAD4(a3x,a3y,a3z, a2x,a2y,a2z, b1x,b1y,b1z, b0x,b0y,b0z, uv_le_o_u0,uv_le_o_v0, uv_le_o_u1,uv_le_o_v1, color_leather); \
					} while(0)

					// ===== STATIC BOOK GEOMETRY ENDS HERE =====
					// Two 3D "wings" that open/close around the spine.
					DRAW_WING(-page_len, 0.0f, -open_base, color_pages);
					DRAW_WING(0.0f, page_len, +open_base, color_pages);

						// ===== TURNING PAGE RENDER STARTS HERE =====
							{
								// Burst swing: every 10s, perform 3 swings at 2x speed.
								const int32_t swing_period_ms = 10000;
								const int32_t swing_ms = 450; // was 900, now 2x speed
								const int32_t swing_count = 3;
								const int32_t burst_ms = swing_ms * swing_count;
								const int32_t t_cycle = ms % swing_period_ms;
								if(t_cycle < burst_ms) {
									const float p
										= (float)(t_cycle % swing_ms) / (float)swing_ms;
									const float swing_target = -2.59f;
									const float swing_fold
										= (-open_base) + p * (swing_target - (-open_base));
									DRAW_PAGE_RANGE_SWING(0.0f, -page_len, swing_fold, 0.004f, color_pages);

									/*if(!debug_printed_this_frame) {
										printf(
											"[BOOK DBG] frame_ms=%d pos=(%.3f,%.3f,%.3f) "
											"page1{angle=%.6f,lx0=%.3f,lx1=%.3f} "
											"page2{angle=%.6f,lx0=%.3f,lx1=%.3f} "
											"page3{angle=%.6f,lx0=%.3f,lx1=%.3f,p=%.3f}\n",
											ms,
											cx0, y_base, cz0,
											-open_base, -page_len, 0.0f,
											-open_base, -page_len, 0.0f,
											swing_fold, 0.0f, -page_len, p
										);
										debug_printed_this_frame = true;
									}*/
								}
							}

					// Leather underlay: slightly protrudes and swings; right side is mirrored.
					const float le_out = 0.060f;
					DRAW_LEATHER(-page_len - le_out, 0.0f, -open_base);
					DRAW_LEATHER(0.0f, page_len + le_out, +open_base);

						#undef DRAW_PAGE_RANGE_STATIC
						#undef DRAW_PAGE_RANGE_SWING
					#undef DRAW_WING
					#undef DRAW_LEATHER
					#undef DRAW_QUAD4_ROT90
					#undef DRAW_QUAD4
					#undef BOOK_PT3D
				}
			}
		}

		ilist_chunks_next(it);
	}

	gfx_cull_func(MODE_BACK);
	gfx_lighting(true);
}

size_t world_render(struct world* w, struct camera* c, bool pass) {
	assert(w && c);

	size_t in_view = 0;
	ilist_chunks_it_t it;

	gfx_fog(true);
	gfx_lighting(true);

	if(!pass) {
		gfx_bind_texture(&texture_terrain);
		gfx_blending(MODE_OFF);
		gfx_alpha_test(true);

		ilist_chunks_it(it, w->render);

		while(!ilist_chunks_end_p(it)) {
			chunk_render(ilist_chunks_ref(it), false, c->x, c->y, c->z);
			in_view++;
			ilist_chunks_next(it);
		}

		world_render_spawners(w, c);
		world_render_enchanting_books(w, c);
	} else {
		gfx_alpha_test(false);
		gfx_blending(MODE_BLEND);
		gfx_write_buffers(false, true, true);

		mat4 matrix_anim;
		int anim = (time_diff_ms(w->anim_timer, time_get()) * 7 / 800) % 28;
		glm_translate_make(
			matrix_anim,
			(vec3) {(anim / 14) * 0.4921875F, (anim % 14) * 0.0703125F, 0.0F});

		gfx_matrix_texture(true, matrix_anim);
		gfx_bind_texture(&texture_anim);

		for(int t_pass = 0; t_pass < 2; t_pass++) {
			if(t_pass == 1)
				gfx_write_buffers(true, false, true);

			ilist_chunks_it(it, w->render);
			while(!ilist_chunks_end_p(it)) {
				chunk_render(ilist_chunks_ref(it), true, c->x, c->y, c->z);
				ilist_chunks_next(it);
			}
		}

		gfx_matrix_texture(false, NULL);
		gfx_write_buffers(true, true, true);
	}

	return in_view;
}

bool world_aabb_intersection(struct world* w, struct AABB* a) {
	assert(w && a);

	w_coord_t min_x = floorf(a->x1);
	// need to look one further, otherwise fence block breaks
	w_coord_t min_y = floorf(a->y1) - 1;
	w_coord_t min_z = floorf(a->z1);

	w_coord_t max_x = ceilf(a->x2) + 1;
	w_coord_t max_y = ceilf(a->y2) + 1;
	w_coord_t max_z = ceilf(a->z2) + 1;

	for(w_coord_t x = min_x; x < max_x; x++) {
		for(w_coord_t z = min_z; z < max_z; z++) {
			for(w_coord_t y = min_y; y < max_y; y++) {
				struct block_data blk = world_get_block(w, x, y, z);

				if(blocks[blk.type]) {
					struct block_info blk_info = (struct block_info) {
						.block = &blk,
						.neighbours = NULL,
						.x = x,
						.y = y,
						.z = z,
					};

					size_t count = blocks[blk.type]->getBoundingBox(&blk_info,
																	true, NULL);
					if(count > 0) {
						struct AABB bbox[count];
						blocks[blk.type]->getBoundingBox(&blk_info, true, bbox);

						for(size_t k = 0; k < count; k++) {
							aabb_translate(bbox + k, x, y, z);

							if(aabb_intersection(a, bbox + k))
								return true;
						}
					}
				}
			}
		}
	}

	return false;
}

static const float light_lookup_overworld[16] = {
	0.05F,	0.067F, 0.085F, 0.106F, 0.129F, 0.156F, 0.186F, 0.221F,
	0.261F, 0.309F, 0.367F, 0.437F, 0.525F, 0.638F, 0.789F, 1.0F,
};

static const float light_lookup_nether[16] = {
	0.1F, 0.116F, 0.133F, 0.153F, 0.175F, 0.2F,	  0.229F, 0.262F,
	0.3F, 0.345F, 0.4F,	  0.467F, 0.55F,  0.657F, 0.8F,	  1.0F,
};

const float* world_dimension_light(struct world* w) {
	assert(w);

	return (w->dimension == WORLD_DIM_OVERWORLD) ? light_lookup_overworld :
												   light_lookup_nether;
}
