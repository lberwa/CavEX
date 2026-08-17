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
#include <malloc.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "block/blocks.h"
#include "chunk.h"
#include "game/game_state.h"
#include "platform/gfx.h"
#include "stack.h"
#include "graphics/gfx_settings.h"

/* Debug: live client-chunk count (chunk_init minus chunk_destroy). If this
 * climbs while the server chunk count stays flat, client chunks are leaking
 * (blocks + displaylists live in the MEM2 heap on Wii). */
volatile long chunk_live_count = 0;

/* ---- Client-Chunk-Block-Pool ------------------------------------------------
 * Jeder Client-Chunk hat einen FESTEN 10-KB-Block-Puffer (c->blocks). Beim
 * Erkunden wird pro Chunk-Load/Unload einer ge-malloc't/ge-free't -> zusammen
 * mit den anderen Per-Chunk-Allokationen zerfranst dieser Churn den newlib-Heap
 * (der auf der Wii erst MEM1, dann MEM2 belegt und Freigaben NICHT an die Arena
 * zurueckgibt -> MEM2 sinkt monoton). Der Pool alloziert Slots EINMAL lazy und
 * gibt sie nie frei, sondern ueber eine Free-List wieder aus -> nach dem
 * Working-Set-Peak null Churn, null Fragmentierung fuer diese Puffer.
 * Slots sind alle gleich gross -> ueber Zeiger-Free-List, kein Slot-Index noetig. */
#define CLIENT_CHUNK_BLOCK_BYTES (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE * 5 / 2)

#ifndef CLIENT_CHUNK_POOL_MAX
#define CLIENT_CHUNK_POOL_MAX 1024 /* > realistischer Client-Chunk-Peak (~760) */
#endif

static uint8_t* g_cc_slot[CLIENT_CHUNK_POOL_MAX]; /* alle je allozierten Slots */
static int g_cc_count = 0;                        /* Anzahl allozierter Slots  */
static int g_cc_cap = CLIENT_CHUNK_POOL_MAX;      /* sinkt bei malloc-Fehler   */
static uint8_t* g_cc_free[CLIENT_CHUNK_POOL_MAX]; /* wiederverwendbare Slots   */
static int g_cc_free_n = 0;

int client_chunk_pool_slots(void) { return g_cc_count; }

static uint8_t* cc_pool_take(void) {
	if(g_cc_free_n > 0)
		return g_cc_free[--g_cc_free_n];
	if(g_cc_count < g_cc_cap) {
		uint8_t* p = malloc(CLIENT_CHUNK_BLOCK_BYTES);
		if(p) {
			g_cc_slot[g_cc_count++] = p;
			return p;
		}
		g_cc_cap = g_cc_count; /* Heap erschoepft -> nicht weiter wachsen */
	}
	return NULL; /* Pool voll/erschoepft -> Aufrufer faellt auf malloc zurueck */
}

static void cc_pool_return(uint8_t* p) {
	if(g_cc_free_n < CLIENT_CHUNK_POOL_MAX)
		g_cc_free[g_cc_free_n++] = p;
}

#define CHUNK_INDEX(x, y, z) ((x) + ((z) + (y) * CHUNK_SIZE) * CHUNK_SIZE)
#define CHUNK_LIGHT_INDEX(x, y, z)                                             \
	((x) + ((z) + (y) * (CHUNK_SIZE + 2)) * (CHUNK_SIZE + 2))

void chunk_init(struct chunk* c, struct world* world, w_coord_t x, w_coord_t y,
				w_coord_t z) {
	assert(c && world);

	c->blocks = cc_pool_take();
	if(c->blocks) {
		c->blocks_pooled = true;
	} else {
		/* Pool erschoepft -> Einzel-malloc als Fallback (wird direkt ge-free't). */
		c->blocks = malloc(CLIENT_CHUNK_BLOCK_BYTES);
		c->blocks_pooled = false;
	}
	assert(c->blocks);

	memset(c->blocks, BLOCK_AIR, CLIENT_CHUNK_BLOCK_BYTES);

	c->x = x;
	c->y = y;
	c->z = z;

	for(int k = 0; k < 13; k++)
		c->has_displist[k] = false;
	c->rebuild_displist = false;
	c->has_spawner = false;
	c->has_enchanting_table = false;
	c->world = world;
	c->reference_count = 0;
	c->gpu_adopt_stamp = 0;
	c->tmp_data.visit_stamp = 0;
	c->tmp_data.from = SIDE_MAX;
	c->tmp_data.used_exit_sides = 0;
	c->tmp_data.steps = 0;

	ilist_chunks_init_field(c);
	ilist_chunks2_init_field(c);

	chunk_live_count++;
}

static void chunk_destroy(struct chunk* c) {
	assert(c);

	if(c->blocks_pooled)
		cc_pool_return(c->blocks);
	else
		free(c->blocks);

	for(int k = 0; k < 13; k++) {
		if(c->has_displist[k])
			displaylist_destroy(c->mesh + k);
	}

	free(c);

	chunk_live_count--;
}

void chunk_ref(struct chunk* c) {
	assert(c);
	c->reference_count++;
}

void chunk_unref(struct chunk* c) {
	assert(c);
	c->reference_count--;

	if(!c->reference_count)
		chunk_destroy(c);
}

struct block_data chunk_get_block(struct chunk* c, c_coord_t x, c_coord_t y,
								  c_coord_t z) {
	assert(c && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE);

	size_t idx = CHUNK_INDEX(x, y, z) / 2 * 5;
	size_t off = CHUNK_INDEX(x, y, z) % 2;

	/* storage layout:
		type 0
		type 1
		light 0
		light 1
		meta 1/0
	*/

	return (struct block_data) {
		.type = c->blocks[idx + off + 0],
		.metadata = (c->blocks[idx + 4] >> (off * 4)) & 0xF,
		.sky_light = c->blocks[idx + off + 2] & 0xF,
		.torch_light = c->blocks[idx + off + 2] >> 4,
	};
}

// for global world lookup
struct block_data chunk_lookup_block(struct chunk* c, w_coord_t x, w_coord_t y,
									 w_coord_t z) {
	assert(c);
	struct chunk* other = c;

	if(x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE || y >= CHUNK_SIZE
	   || z >= CHUNK_SIZE)
		other = world_find_chunk(c->world, c->x + x, c->y + y, c->z + z);

	return other ?
		chunk_get_block(other, W2C_COORD(x), W2C_COORD(y), W2C_COORD(z)) :
		(struct block_data) {
			.type = (y < WORLD_HEIGHT) ? 1 : 0,
			.metadata = 0,
			.sky_light = (y < WORLD_HEIGHT) ? 0 : 15,
			.torch_light = 0,
		};
}

static void chunk_trigger_neighbour_update(struct chunk* c, c_coord_t x,
										   c_coord_t y, c_coord_t z) {
	// TODO: diagonal chunks, just sharing edge or single point

	bool cond[6] = {
		x == 0, x == CHUNK_SIZE - 1, y == 0, y == CHUNK_SIZE - 1,
		z == 0, z == CHUNK_SIZE - 1,
	};

	int offset[6][3] = {
		{-1, 0, 0},			{CHUNK_SIZE, 0, 0}, {0, -1, 0},
		{0, CHUNK_SIZE, 0}, {0, 0, -1},			{0, 0, CHUNK_SIZE},
	};

	for(int k = 0; k < 6; k++) {
		if(cond[k]) {
			struct chunk* other
				= world_find_chunk(c->world, c->x + offset[k][0],
								   c->y + offset[k][1], c->z + offset[k][2]);
			if(other)
				other->rebuild_displist = true;
		}
	}
}

void chunk_set_light(struct chunk* c, c_coord_t x, c_coord_t y, c_coord_t z,
					 uint8_t light) {
	assert(c && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE);

	size_t idx = CHUNK_INDEX(x, y, z) / 2 * 5;
	size_t off = CHUNK_INDEX(x, y, z) % 2;

	c->blocks[idx + off + 2] = light;
	c->rebuild_displist = true;

	chunk_trigger_neighbour_update(c, x, y, z);
}

void chunk_set_block(struct chunk* c, c_coord_t x, c_coord_t y, c_coord_t z,
					 struct block_data blk) {
	assert(c && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE);

	chunk_set_block_raw(c, x, y, z, blk);
	c->rebuild_displist = true;

	chunk_trigger_neighbour_update(c, x, y, z);
}

void chunk_set_block_raw(struct chunk* c, c_coord_t x, c_coord_t y,
						 c_coord_t z, struct block_data blk) {
	assert(c && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE);

	size_t idx = CHUNK_INDEX(x, y, z) / 2 * 5;
	size_t off = CHUNK_INDEX(x, y, z) % 2;

	c->blocks[idx + off + 0] = blk.type;
	c->blocks[idx + off + 2] = (blk.torch_light << 4) | blk.sky_light;
	c->blocks[idx + 4] = (c->blocks[idx + 4] & ~(0x0F << (off * 4)))
		| (blk.metadata << (off * 4));
	if(blk.type == BLOCK_SPAWNER)
		c->has_spawner = true;
	if(blk.type == BLOCK_ENCHANTING_TABLE)
		c->has_enchanting_table = true;
}

bool chunk_check_built(struct chunk* c) {
	assert(c);

	if(c->rebuild_displist && chunk_mesher_send(c)) {
		c->rebuild_displist = false;
		return true;
	}

	return false;
}

void chunk_pre_render(struct chunk* c, mat4 view, bool has_fog) {
	assert(c && view);

	glm_translate_to(view, (vec3) {c->x, c->y, c->z}, c->model_view);
	c->has_fog = has_fog;
}

static void check_matrix_set(struct chunk* c, bool* needs_matrix) {
	assert(c && needs_matrix);

	if(*needs_matrix) {
		gfx_matrix_modelview(c->model_view);
		gfx_fog(c->has_fog);
		gfx_fog_pos(c->x - gstate.camera.x, c->z - gstate.camera.z,
					gstate.config.fog_distance);
		*needs_matrix = false;
	}
}

void chunk_render(struct chunk* c, bool pass, float x, float y, float z) {
	assert(c);

	bool needs_matrix = true;
	int offset = pass ? 6 : 0;

	if(y < c->y + CHUNK_SIZE && c->has_displist[SIDE_BOTTOM + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_BOTTOM + offset);
	}

	if(y > c->y && c->has_displist[SIDE_TOP + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_TOP + offset);
	}

	if(x < c->x + CHUNK_SIZE && c->has_displist[SIDE_LEFT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_LEFT + offset);
	}

	if(x > c->x && c->has_displist[SIDE_RIGHT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_RIGHT + offset);
	}

	if(z < c->z + CHUNK_SIZE && c->has_displist[SIDE_FRONT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_FRONT + offset);
	}

	if(z > c->z && c->has_displist[SIDE_BACK + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_BACK + offset);
	}

	#ifdef GFX_DOUBLESIDED
	if(!pass && c->has_displist[12]) {
		check_matrix_set(c, &needs_matrix);
		gfx_blending(MODE_BLEND);
		gfx_fog(false);
		gfx_write_buffers(true, false, true);
		gfx_cull_func(MODE_NONE);
		displaylist_render(c->mesh + 12);
		gfx_cull_func(MODE_BACK);
		gfx_write_buffers(true, true, true);
		gfx_fog(c->has_fog);
		gfx_blending(MODE_OFF);
	}
	#endif
}
