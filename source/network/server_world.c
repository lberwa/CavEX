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

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../block/blocks.h"
#include "../lighting.h"
#include "../util.h"
#include "client_interface.h"
#include "server_local.h"
#include "server_world.h"
#include "../daytime.h"
#include "../platform/time.h"
#include "../cubiomes/generator.h"
#include "../cubiomes/biomes.h"
#include "gen_world.h"

#define EXPLOSION_MAX_RAYS 300
#define EXPLOSION_STEP     0.5f
#define HARDNESS_SCALE     0.0005f

#ifdef CAVEX_DEBUG_BLOCK_PLACEMENT
#define BLOCK_PLACE_SERVER_PRINTF(...) printf(__VA_ARGS__)
#else
#define BLOCK_PLACE_SERVER_PRINTF(...) ((void)0)
#endif

// #define ALL_FALSE_FINISHER

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static bool server_world_is_redstone_visual_block(uint8_t type) {
	return type == BLOCK_REDSTONE_WIRE || type == BLOCK_REDSTONE_TORCH
		   || type == BLOCK_REDSTONE_TORCH_LIT || type == BLOCK_REPEATER_OFF
		   || type == BLOCK_REPEATER_ON;
}

#define CHUNK_DIST2(x1, x2, z1, z2)                                            \
	(((x1) - (x2)) * ((x1) - (x2)) + ((z1) - (z2)) * ((z1) - (z2)))

#define S_CHUNK_IDX(x, y, z)                                                   \
	((y) + (W2C_COORD(z) + W2C_COORD(x) * CHUNK_SIZE) * WORLD_HEIGHT)

#define GEN_SEA_LEVEL 62
#define GEN_BIOME_CACHE_MARGIN 8
#define GEN_BIOME_CACHE_WIDTH  (CHUNK_SIZE + GEN_BIOME_CACHE_MARGIN * 2)

struct gen_cuberite_runtime_args {
	float land_frequency_x;
	float land_frequency_z;
	int land_octaves;
	float detail_frequency_x;
	float detail_frequency_z;
	int detail_octaves;
	float mountain_frequency_x;
	float mountain_frequency_z;
	float mountain_gain;
	float cave_frequency_xy;
	float cave_frequency_y;
	float cave_threshold_bias;
	float cave_secondary_frequency_xy;
	float cave_secondary_frequency_y;
	float cave_mix_primary;
	int cave_max_y;
	int cave_surface_gap;
	int cave_min_y;
	float edge_blend_weight_self;
	float edge_blend_weight_neighbor;
	float island_mask_frequency_x;
	float island_mask_frequency_z;
	float island_mask_threshold;
	float island_mask_scale;
	float island_max_lift;
	float island_flatten_threshold;
	float rough_ravines_max_center_width;
	float rough_ravines_min_center_width;
	float rough_ravines_max_roughness;
	float rough_ravines_min_roughness;
	float rough_ravines_max_floor_height_center;
	float rough_ravines_min_floor_height_center;
	float rough_ravines_max_ceiling_height_center;
	float tree_threshold_forest;
	float tree_threshold_dense;
	float grass_threshold;
	float flower_threshold;
	float dead_bush_threshold;
	float cactus_threshold;
	float cactus_tall_threshold;
	float reed_threshold;
	float reed_tall_threshold;
	float pumpkin_threshold;
	float melon_threshold;
	float lily_threshold;
	float vine_threshold;
	float clay_threshold;
	float gravel_patch_threshold;
	int sea_level;
};

enum gen_profile_phase {
	GEN_PROFILE_TERRAIN,
	GEN_PROFILE_FEATURES,
	GEN_PROFILE_DECO,
	GEN_PROFILE_FINALIZE,
	GEN_PROFILE_PHASE_COUNT,
};

struct gen_profile_phase_stats {
	uint64_t calls;
	uint64_t ms;
};

struct gen_profile_stats {
	uint64_t chunks;
	struct gen_profile_phase_stats phase[GEN_PROFILE_PHASE_COUNT];
};

static struct gen_profile_stats g_gen_profile;

static void gen_profile_phase_done(enum gen_profile_phase phase,
								  ptime_t start) {
	int32_t elapsed = time_diff_ms(start, time_get());
	if(elapsed < 0)
		elapsed = 0;
	g_gen_profile.phase[phase].calls++;
	g_gen_profile.phase[phase].ms += (uint64_t)elapsed;
}

void server_world_generation_profile_save(void) {
#if 0
	FILE* f = fopen("./cavex.log", "a");
	if(!f)
		return;

	uint64_t total_ms = 0;
	for(int i = 0; i < GEN_PROFILE_PHASE_COUNT; i++)
		total_ms += g_gen_profile.phase[i].ms;

	fprintf(f, "[GEN_PROFILE] chunks=%llu total_ms=%llu avg_chunk_ms=%.2f\n",
			(unsigned long long)g_gen_profile.chunks,
			(unsigned long long)total_ms,
			g_gen_profile.chunks ?
				(float)total_ms / (float)g_gen_profile.chunks : 0.0f);
	static const char* names[GEN_PROFILE_PHASE_COUNT] = {
		"terrain", "features", "deco", "finalize"
	};
	for(int i = 0; i < GEN_PROFILE_PHASE_COUNT; i++) {
		struct gen_profile_phase_stats* p = &g_gen_profile.phase[i];
		fprintf(f, "[GEN_PROFILE] %s calls=%llu total_ms=%llu avg_ms=%.2f\n",
				names[i], (unsigned long long)p->calls,
				(unsigned long long)p->ms,
				p->calls ? (float)p->ms / (float)p->calls : 0.0f);
	}
	fclose(f);
#endif
	memset(&g_gen_profile, 0, sizeof(g_gen_profile));
}

static float gen_inv_nonzero(float value, float fallback) {
	return (fabsf(value) > 0.0001f) ? (1.0f / value) : fallback;
}

static struct gen_cuberite_runtime_args gen_runtime_args(const struct server_world* w) {
	const struct server_world_cuberite_config* cfg = &w->generator;
	struct gen_cuberite_runtime_args args = {
		.land_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_base_frequency_x, 1.0f / 40.0f) * 0.42f,
		.land_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_base_frequency_z, 1.0f / 40.0f) * 0.42f,
		.land_octaves = (cfg->biomal_noise3d_num_base_octaves > 0) ?
			cfg->biomal_noise3d_num_base_octaves : 6,
		.detail_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_frequency_x, 1.0f / 40.0f) * 0.62f,
		.detail_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_frequency_z, 1.0f / 40.0f) * 0.62f,
		.detail_octaves = (cfg->biomal_noise3d_num_density_octaves > 0) ?
			cfg->biomal_noise3d_num_density_octaves : 6,
		.mountain_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_x, 1.0f / 40.0f) * 0.34f,
		.mountain_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_z, 1.0f / 40.0f) * 0.34f,
		.mountain_gain = 0.85f * fmaxf(0.25f, cfg->biomal_noise3d_base_amplitude),
		.cave_frequency_xy = gen_inv_nonzero((float)cfg->worm_nest_caves_grid, 1.0f / 96.0f) * 5.8f,
		.cave_frequency_y = gen_inv_nonzero((float)cfg->worm_nest_caves_size, 1.0f / 64.0f) * 4.8f,
		.cave_threshold_bias = 0.06f + cfg->biomal_noise3d_air_threshold * 0.03f,
		.cave_secondary_frequency_xy = gen_inv_nonzero((float)cfg->worm_nest_caves_grid, 1.0f / 96.0f) * 10.0f,
		.cave_secondary_frequency_y = gen_inv_nonzero((float)cfg->worm_nest_caves_size, 1.0f / 64.0f) * 2.9f,
		.cave_mix_primary = 0.72f,
		.cave_max_y = 31,
		.cave_surface_gap = 20,
		.cave_min_y = 9,
		.edge_blend_weight_self = 0.70f,
		.edge_blend_weight_neighbor = 0.30f,
		.island_mask_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_x, 1.0f / 40.0f) * 0.30f,
		.island_mask_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_z, 1.0f / 40.0f) * 0.30f,
		.island_mask_threshold = 0.08f,
		.island_mask_scale = 0.32f,
		.island_max_lift = 10.0f,
		.island_flatten_threshold = 0.35f,
		.rough_ravines_max_center_width = cfg->rough_ravines_max_center_width,
		.rough_ravines_min_center_width = cfg->rough_ravines_min_center_width,
		.rough_ravines_max_roughness = cfg->rough_ravines_max_roughness,
		.rough_ravines_min_roughness = cfg->rough_ravines_min_roughness,
		.rough_ravines_max_floor_height_center =
			cfg->rough_ravines_max_floor_height_center,
		.rough_ravines_min_floor_height_center =
			cfg->rough_ravines_min_floor_height_center,
		.rough_ravines_max_ceiling_height_center =
			cfg->rough_ravines_max_ceiling_height_center,
		.tree_threshold_forest = 0.55f,
		.tree_threshold_dense = 0.62f,
		.grass_threshold = 0.94f,
		.flower_threshold = 0.90f,
		.dead_bush_threshold = 0.955f,
		.cactus_threshold = 0.985f,
		.cactus_tall_threshold = 0.995f,
		.reed_threshold = 0.975f,
		.reed_tall_threshold = 0.992f,
		.pumpkin_threshold = 0.9975f,
		.melon_threshold = 0.997f,
		.lily_threshold = 0.985f,
		.vine_threshold = 0.94f,
		.clay_threshold = 0.92f,
		.gravel_patch_threshold = 0.78f,
		.sea_level = (cfg->sea_level > 0) ? cfg->sea_level : GEN_SEA_LEVEL,
	};
	return args;
}

static uint32_t gen_hash_u32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static uint32_t gen_hash3i(int32_t x, int32_t y, int32_t z, uint32_t seed) {
	uint32_t h = seed;
	h ^= gen_hash_u32((uint32_t)x * 0x9E3779B9U);
	h ^= gen_hash_u32((uint32_t)y * 0x85EBCA6BU);
	h ^= gen_hash_u32((uint32_t)z * 0xC2B2AE35U);
	return gen_hash_u32(h);
}

static float gen_rand01_from_hash(uint32_t h) {
	return (float)(h & 0x00FFFFFFU) / 16777215.0f;
}

static float gen_smooth(float t) {
	return t * t * (3.0f - 2.0f * t);
}

static float gen_lerp(float a, float b, float t) {
	return a + (b - a) * t;
}

static int gen_chunk_surface_height(const struct server_chunk* sc, int lx, int lz);
static int gen_chunk_actual_surface_height(const struct server_chunk* sc, int lx, int lz);
static bool gen_is_water(const struct server_chunk* sc, int x, int y, int z);
static bool gen_is_ore_block(uint8_t block);

#ifdef ALL_FALSE_FINISHER
static const bool GEN_ENABLE_RIVER_LOWERING = 					false;
static const bool GEN_ENABLE_NEIGHBOR_EDGE_BLEND = 				false;
static const bool GEN_ENABLE_OCEAN_ISLAND_MASK = 				false;
static const bool GEN_ENABLE_LOWLAND_NEIGHBOR_SMOOTHING = 		false;
static const bool GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL = 			false;
static const bool GEN_ENABLE_SNOW_SURFACE_LAYER = 				false;
#else
static const bool GEN_ENABLE_RIVER_LOWERING = 					true;
static const bool GEN_ENABLE_NEIGHBOR_EDGE_BLEND = 				false;
static const bool GEN_ENABLE_OCEAN_ISLAND_MASK = 				true;
static const bool GEN_ENABLE_LOWLAND_NEIGHBOR_SMOOTHING = 		false;
static const bool GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL = 			true;
static const bool GEN_ENABLE_SNOW_SURFACE_LAYER = 				true;
#endif

static float gen_value_noise2d(float x, float z, uint32_t seed) {
	int32_t xi = (int32_t)floorf(x);
	int32_t zi = (int32_t)floorf(z);
	float xf = x - (float)xi;
	float zf = z - (float)zi;

	float v00 = gen_rand01_from_hash(gen_hash3i(xi, 0, zi, seed)) * 2.0f - 1.0f;
	float v10
		= gen_rand01_from_hash(gen_hash3i(xi + 1, 0, zi, seed)) * 2.0f - 1.0f;
	float v01
		= gen_rand01_from_hash(gen_hash3i(xi, 0, zi + 1, seed)) * 2.0f - 1.0f;
	float v11 = gen_rand01_from_hash(gen_hash3i(xi + 1, 0, zi + 1, seed))
		* 2.0f
		- 1.0f;

	float u = gen_smooth(xf);
	float v = gen_smooth(zf);
	return gen_lerp(gen_lerp(v00, v10, u), gen_lerp(v01, v11, u), v);
}

static float gen_value_noise3d(float x, float y, float z, uint32_t seed) {
	int32_t xi = (int32_t)floorf(x);
	int32_t yi = (int32_t)floorf(y);
	int32_t zi = (int32_t)floorf(z);
	float xf = x - (float)xi;
	float yf = y - (float)yi;
	float zf = z - (float)zi;

	float c000
		= gen_rand01_from_hash(gen_hash3i(xi, yi, zi, seed)) * 2.0f - 1.0f;
	float c100
		= gen_rand01_from_hash(gen_hash3i(xi + 1, yi, zi, seed)) * 2.0f - 1.0f;
	float c010
		= gen_rand01_from_hash(gen_hash3i(xi, yi + 1, zi, seed)) * 2.0f - 1.0f;
	float c110 = gen_rand01_from_hash(gen_hash3i(xi + 1, yi + 1, zi, seed))
		* 2.0f
		- 1.0f;
	float c001
		= gen_rand01_from_hash(gen_hash3i(xi, yi, zi + 1, seed)) * 2.0f - 1.0f;
	float c101 = gen_rand01_from_hash(gen_hash3i(xi + 1, yi, zi + 1, seed))
		* 2.0f
		- 1.0f;
	float c011 = gen_rand01_from_hash(gen_hash3i(xi, yi + 1, zi + 1, seed))
		* 2.0f
		- 1.0f;
	float c111 = gen_rand01_from_hash(gen_hash3i(xi + 1, yi + 1, zi + 1, seed))
		* 2.0f
		- 1.0f;

	float u = gen_smooth(xf);
	float v = gen_smooth(yf);
	float w = gen_smooth(zf);

	float x00 = gen_lerp(c000, c100, u);
	float x10 = gen_lerp(c010, c110, u);
	float x01 = gen_lerp(c001, c101, u);
	float x11 = gen_lerp(c011, c111, u);
	float y0 = gen_lerp(x00, x10, v);
	float y1 = gen_lerp(x01, x11, v);
	return gen_lerp(y0, y1, w);
}

static float gen_fbm2d(float x, float z, uint32_t seed, int octaves,
					   float lacunarity, float gain) {
	float amp = 1.0f;
	float freq = 1.0f;
	float sum = 0.0f;
	float norm = 0.0f;
	for(int i = 0; i < octaves; i++) {
		sum += gen_value_noise2d(x * freq, z * freq, seed + (uint32_t)i * 977U)
			* amp;
		norm += amp;
		amp *= gain;
		freq *= lacunarity;
	}
	return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

static float gen_fbm3d(float x, float y, float z, uint32_t seed, int octaves,
					   float lacunarity, float gain) {
	float amp = 1.0f;
	float freq = 1.0f;
	float sum = 0.0f;
	float norm = 0.0f;
	for(int i = 0; i < octaves; i++) {
		sum += gen_value_noise3d(x * freq, y * freq, z * freq,
								 seed + (uint32_t)i * 1301U) * amp;
		norm += amp;
		amp *= gain;
		freq *= lacunarity;
	}
	return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

static bool gen_is_opaque(uint8_t block_id) {
	switch(block_id) {
		case BLOCK_AIR:
		case BLOCK_WATER_FLOW:
		case BLOCK_WATER_STILL:
		case BLOCK_LEAVES:
		case BLOCK_TALL_GRASS:
		case BLOCK_SNOW: return false;
		default: return true;
	}
}

static float gen_clampf(float value, float minv, float maxv) {
	if(value < minv)
		return minv;
	if(value > maxv)
		return maxv;
	return value;
}

struct gen_biome_profile {
	float base_height;
	float amplitude;
	uint8_t top_block;
	uint8_t filler_block;
	bool oceanic;
	bool riverine;
};

static struct gen_biome_profile gen_profile_for_biome(int biome_id) {
	struct gen_biome_profile p = {
		.base_height = 63.0f,
		.amplitude = 9.0f,
		.top_block = BLOCK_GRASS,
		.filler_block = BLOCK_DIRT,
		.oceanic = false,
		.riverine = false,
	};

	if(biome_id == river || biome_id == frozen_river) {
		p.base_height = 60.0f;
		p.amplitude = 2.0f;
		p.top_block = BLOCK_GRAVEL;
		p.filler_block = BLOCK_DIRT;
		p.oceanic = false;
		p.riverine = true;
		return p;
	}

	if(isOceanic(biome_id)) {
		p.base_height = 49.0f;
		p.amplitude = 4.0f;
		p.top_block = BLOCK_GRAVEL;
		p.filler_block = BLOCK_DIRT;
		p.oceanic = true;
		return p;
	}

	switch(biome_id) {
		case mushroom_fields:
		case mushroom_field_shore:
			p.base_height = 64.0f;
			p.amplitude = 7.0f;
			p.top_block = BLOCK_MYCELIUM;
			p.filler_block = BLOCK_DIRT;
			break;

		case desert:
		case desert_hills:
		case desert_lakes:
		case beach:
		case snowy_beach:
		case badlands:
		case badlands_plateau:
		case wooded_badlands_plateau:
		case eroded_badlands:
		case modified_badlands_plateau:
		case modified_wooded_badlands_plateau:
			p.base_height = 64.0f;
			p.amplitude = 6.0f;
			p.top_block = BLOCK_SAND;
			p.filler_block = BLOCK_SANDSTONE;
			break;

		case mountains:
		case wooded_mountains:
		case mountain_edge:
		case gravelly_mountains:
		case modified_gravelly_mountains:
			p.base_height = 74.0f;
			p.amplitude = 30.0f;
			p.top_block = BLOCK_GRASS;
			p.filler_block = BLOCK_DIRT;
			break;

		case stone_shore:
			p.base_height = 62.0f;
			p.amplitude = 4.0f;
			p.top_block = BLOCK_STONE;
			p.filler_block = BLOCK_STONE;
			break;

		case forest:
		case birch_forest:
		case dark_forest:
		case wooded_hills:
		case birch_forest_hills:
		case flower_forest:
			p.base_height = 66.0f;
			p.amplitude = 10.0f;
			break;

		case taiga:
		case taiga_hills:
		case giant_tree_taiga:
		case giant_tree_taiga_hills:
		case giant_spruce_taiga:
		case giant_spruce_taiga_hills:
			p.base_height = 67.0f;
			p.amplitude = 12.0f;
			break;

		default:
			if(isSnowy(biome_id)) {
				p.base_height = 68.0f;
				p.amplitude = 8.0f;
			}
			break;
	}
	return p;
}

struct gen_biome_cache_view {
	bool valid;
	int origin_x;
	int origin_z;
	int* ids;
};

struct gen_biome_cache_storage {
	bool valid;
	int origin_x;
	int origin_z;
	int ids[GEN_BIOME_CACHE_WIDTH * GEN_BIOME_CACHE_WIDTH];
};

static struct gen_biome_cache_view gen_active_biome_cache;
static struct gen_biome_cache_storage gen_pending_biome_cache;

static void gen_biome_cache_fill(Generator* biome_gen, int world_x0, int world_z0,
								 bool* valid, int* origin_x, int* origin_z,
								 int* ids) {
	assert(valid && origin_x && origin_z && ids);

	*valid = false;
	*origin_x = world_x0 - GEN_BIOME_CACHE_MARGIN;
	*origin_z = world_z0 - GEN_BIOME_CACHE_MARGIN;
	for(int i = 0; i < GEN_BIOME_CACHE_WIDTH * GEN_BIOME_CACHE_WIDTH; i++)
		ids[i] = none;

	if(!biome_gen)
		return;

	Range r = {
		.scale = 4,
		.x = *origin_x,
		.z = *origin_z,
		.sx = GEN_BIOME_CACHE_WIDTH,
		.sz = GEN_BIOME_CACHE_WIDTH,
		.y = 64,
		.sy = 1,
	};
	int* cache = allocCache(biome_gen, r);
	if(!cache)
		return;

	if(genBiomes(biome_gen, cache, r) == 0) {
		memcpy(ids, cache, GEN_BIOME_CACHE_WIDTH * GEN_BIOME_CACHE_WIDTH * sizeof(*ids));
		*valid = true;
	}
	free(cache);
}

static void gen_biome_cache_activate(bool valid, int origin_x, int origin_z,
									 int* ids) {
	gen_active_biome_cache = (struct gen_biome_cache_view) {
		.valid = valid,
		.origin_x = origin_x,
		.origin_z = origin_z,
		.ids = ids,
	};
}

static void gen_biome_cache_clear_active(void) {
	gen_active_biome_cache = (struct gen_biome_cache_view) {0};
}

static int gen_biome_at_safe(Generator* biome_gen, int wx, int wz) {
	int biome_id = none;
	if(gen_active_biome_cache.valid && gen_active_biome_cache.ids) {
		int lx = wx - gen_active_biome_cache.origin_x;
		int lz = wz - gen_active_biome_cache.origin_z;
		if(lx >= 0 && lx < GEN_BIOME_CACHE_WIDTH
		   && lz >= 0 && lz < GEN_BIOME_CACHE_WIDTH)
			biome_id = gen_active_biome_cache.ids[lx + lz * GEN_BIOME_CACHE_WIDTH];
	}
	if(biome_id == none)
		biome_id = getBiomeAt(biome_gen, 4, wx, 64, wz);
	return (biome_id < 0) ? plains : biome_id;
}

static int gen_reduce_extreme_biomes(int biome_id, int wx, int wz) {
	float major_mask = gen_fbm2d((float)wx * 0.0055f, (float)wz * 0.0055f,
								 0x5EA0001U, 3, 2.0f, 0.5f);
	float variant = gen_fbm2d((float)wx * 0.013f, (float)wz * 0.013f,
							  0x5EA0002U, 2, 2.0f, 0.5f);

	if(isOceanic(biome_id) && major_mask > 0.0f) {
		if(variant > 0.35f)
			return forest;
		if(variant < -0.35f)
			return plains;
		return wooded_hills;
	}

	switch(biome_id) {
	case desert:
	case desert_hills:
	case desert_lakes:
	case beach:
	case snowy_beach:
	case badlands:
	case badlands_plateau:
	case wooded_badlands_plateau:
	case eroded_badlands:
	case modified_badlands_plateau:
	case modified_wooded_badlands_plateau:
		if(major_mask > 0.0f) {
			if(variant > 0.30f)
				return savanna;
			if(variant < -0.30f)
				return forest;
			return plains;
		}
		break;
	default:
		break;
	}

	return biome_id;
}

static int gen_apply_forest_biome_variation(int biome_id, int wx, int wz) {
	float forest_mask = gen_fbm2d((float)wx * 0.0075f, (float)wz * 0.0075f,
								  0xF07E571U, 3, 2.0f, 0.5f);
	float forest_type = gen_fbm2d((float)wx * 0.018f, (float)wz * 0.018f,
								  0xB17C4F5U, 2, 2.0f, 0.5f);

	switch(biome_id) {
	case plains:
		if(forest_mask > -0.08f) {
			if(forest_type > 0.52f)
				return dark_forest;
			if(forest_type > 0.18f)
				return birch_forest;
			if(forest_type < -0.42f)
				return flower_forest;
			return forest;
		}
		break;
	case wooded_hills:
		if(forest_mask > -0.05f) {
			if(forest_type > 0.25f)
				return birch_forest_hills;
			return forest;
		}
		break;
	case forest:
		if(forest_type > 0.60f)
			return dark_forest;
		if(forest_type < -0.48f)
			return flower_forest;
		break;
	default:
		break;
	}

	return biome_id;
}

static struct gen_biome_profile gen_blended_profile(Generator* biome_gen, int wx,
													int wz, int* center_biome) {
	static const int offs[5][2] = {
		{0, 0},
		{8, 0},
		{-8, 0},
		{0, 8},
		{0, -8},
	};
	static const float weights[5] = {4.0f, 1.0f, 1.0f, 1.0f, 1.0f};

	float base = 0.0f;
	float amp = 0.0f;
	float wsum = 0.0f;
	int ocean_votes = 0;
	int snowy_votes = 0;
	int river_votes = 0;

	int first = gen_apply_forest_biome_variation(
		gen_reduce_extreme_biomes(gen_biome_at_safe(biome_gen, wx, wz), wx, wz), wx, wz);
	struct gen_biome_profile center_profile = gen_profile_for_biome(first);
	if(center_biome)
		*center_biome = first;

	for(int i = 0; i < 5; i++) {
		int biome_id = (i == 0) ? first :
								gen_apply_forest_biome_variation(
									gen_reduce_extreme_biomes(
										gen_biome_at_safe(biome_gen, wx + offs[i][0],
														  wz + offs[i][1]),
										wx + offs[i][0], wz + offs[i][1]),
									wx + offs[i][0], wz + offs[i][1]);
		struct gen_biome_profile p = gen_profile_for_biome(biome_id);
		float w = weights[i];
		base += p.base_height * w;
		amp += p.amplitude * w;
		wsum += w;
		if(p.oceanic)
			ocean_votes++;
		if(p.riverine)
			river_votes++;
		if(isSnowy(biome_id))
			snowy_votes++;
	}

	struct gen_biome_profile out = {
		.base_height = base / wsum,
		.amplitude = amp / wsum,
		.top_block = center_profile.top_block,
		.filler_block = center_profile.filler_block,
		.oceanic = ocean_votes >= 3,
		.riverine = false,
	};

	if(snowy_votes >= 3 && out.top_block == BLOCK_GRASS)
		out.base_height += 1.0f;

	bool center_river = (first == river || first == frozen_river);
	if(center_river && river_votes >= 2) {
		out.base_height = (out.base_height * 2.0f + 61.0f) / 3.0f;
		out.amplitude = fminf(out.amplitude, 4.0f);
		out.oceanic = false;
		out.riverine = true;
	}

	return out;
}

static float gen_river_channel_strength(uint32_t seed, int32_t wx, int32_t wz) {
	float valley = gen_fbm2d((float)wx * 0.010f, (float)wz * 0.010f,
							 seed ^ 0xD1CEBEEFU, 3, 2.0f, 0.52f);
	float bend = gen_fbm2d((float)wx * 0.0035f, (float)wz * 0.0035f,
						   seed ^ 0x51DECAFEU, 2, 2.0f, 0.5f);
	float bank = gen_fbm2d((float)wx * 0.028f, (float)wz * 0.028f,
						   seed ^ 0x0A1154A1U, 1, 2.0f, 0.5f);
	float center_distance = fabsf(valley + bend * 0.34f);
	float width = 0.18f + fmaxf(0.0f, bank) * 0.05f;
	float feather = 0.18f;
	float strength = (width + feather - center_distance) / feather;
	strength = gen_clampf(strength, 0.0f, 1.0f);
	return strength * strength * (3.0f - 2.0f * strength);
}

static int gen_apply_river_channel(struct gen_biome_profile* profile,
								   const struct gen_cuberite_runtime_args* gen_args,
								   uint32_t seed, int32_t wx, int32_t wz,
								   int surface) {
	if(!GEN_ENABLE_RIVER_LOWERING || !profile->riverine)
		return surface;

	float strength = gen_river_channel_strength(seed, wx, wz);
	if(strength < 0.20f) {
		profile->riverine = false;
		if(profile->top_block == BLOCK_SAND) {
			profile->top_block = BLOCK_GRASS;
			profile->filler_block = BLOCK_DIRT;
		}
		if(surface < gen_args->sea_level + 1)
			surface = gen_args->sea_level + 1;
		return surface;
	}

	float edge_lift = (1.0f - strength) * 3.0f;
	int bank_surface = gen_args->sea_level + 1 + (int)edge_lift;
	int river_floor = gen_args->sea_level - 1;
	if(strength > 0.82f)
		river_floor--;

	float cut = gen_clampf((strength - 0.20f) / 0.80f, 0.0f, 1.0f);
	cut = cut * cut * (3.0f - 2.0f * cut);
	int target = (int)((float)bank_surface * (1.0f - cut)
		+ (float)river_floor * cut);
	if(surface > target)
		surface = (int)((float)surface * (1.0f - cut)
			+ (float)target * cut);
	if(surface < river_floor)
		surface = river_floor;
	if(strength < 0.52f && surface < bank_surface)
		surface = bank_surface;

	if(strength > 0.55f) {
		profile->top_block = BLOCK_GRAVEL;
		profile->filler_block = BLOCK_DIRT;
	} else {
		profile->top_block = BLOCK_GRASS;
		profile->filler_block = BLOCK_DIRT;
	}
	return surface;
}

static bool gen_biome_has_stony_surface(int biome_id) {
	switch(biome_id) {
	case mountains:
	case wooded_mountains:
	case mountain_edge:
	case gravelly_mountains:
	case modified_gravelly_mountains:
	case stone_shore:
		return true;
	default:
		return false;
	}
}

static bool gen_biome_has_sandy_surface(int biome_id) {
	switch(biome_id) {
	case desert:
	case desert_hills:
	case desert_lakes:
	case beach:
	case snowy_beach:
	case badlands:
	case badlands_plateau:
	case wooded_badlands_plateau:
	case eroded_badlands:
	case modified_badlands_plateau:
	case modified_wooded_badlands_plateau:
		return true;
	default:
		return false;
	}
}

static int gen_compute_surface_height_base(const struct gen_biome_profile* profile,
										   int biome_id,
										   const struct gen_cuberite_runtime_args* gen_args,
										   uint32_t seed, int choice_octaves,
										   int32_t wx, int32_t wz) {
	float continental = gen_fbm2d(wx * gen_args->land_frequency_x,
		wz * gen_args->land_frequency_z, seed ^ 0x13579BDFU,
		gen_args->land_octaves, 2.0f, 0.5f);
	float mountain = gen_fbm2d(wx * gen_args->mountain_frequency_x,
		wz * gen_args->mountain_frequency_z, seed ^ 0x4AFEB19DU,
		choice_octaves, 2.0f, 0.5f);
	float micro = gen_fbm2d(wx * gen_args->detail_frequency_x,
		wz * gen_args->detail_frequency_z, seed ^ 0xA53C9E3DU,
		gen_args->detail_octaves, 2.0f, 0.5f);

	float mountain_lift = fmaxf(0.0f, mountain);
	float mountain_factor = gen_args->mountain_gain;
	float micro_factor = 0.85f;
	if(profile->top_block == BLOCK_SAND) {
		mountain_factor = 0.35f;
		micro_factor = 0.35f;
	} else if(profile->top_block == BLOCK_STONE && !profile->oceanic) {
		mountain_factor = 0.95f;
		micro_factor = 1.1f;
	}

	int surface = (int)(profile->base_height
		+ continental * profile->amplitude
		+ mountain_lift * (profile->amplitude * mountain_factor)
		+ micro * micro_factor);

	if(GEN_ENABLE_OCEAN_ISLAND_MASK && profile->oceanic) {
		float island_mask = gen_fbm2d(wx * gen_args->island_mask_frequency_x,
			wz * gen_args->island_mask_frequency_z, seed ^ 0x6A1D51E1U,
			choice_octaves, 2.0f, 0.5f);
		if(island_mask < gen_args->island_mask_threshold) {
			surface = fminf(surface, gen_args->sea_level - 2);
		} else {
			float t = (island_mask - gen_args->island_mask_threshold)
				/ gen_args->island_mask_scale;
			if(t < 0.0f)
				t = 0.0f;
			if(t > 1.0f)
				t = 1.0f;
			float lift = t * t * (3.0f - 2.0f * t);
			int target = (int)(gen_args->sea_level - 1
						   + lift * gen_args->island_max_lift);
			if(target > surface)
				surface = target;
			if(lift < gen_args->island_flatten_threshold
			   && surface > gen_args->sea_level + 2) {
				surface = gen_args->sea_level + 2;
			}
		}
	}

	if(surface < 6)
		surface = 6;
	if(surface > WORLD_HEIGHT - 2)
		surface = WORLD_HEIGHT - 2;
	if(profile->oceanic && surface > gen_args->sea_level - 2)
		surface = gen_args->sea_level - 2;
	if(surface < gen_args->sea_level - 20)
		surface = gen_args->sea_level - 20;

	return surface;
}

static void gen_choose_surface_materials(const struct gen_biome_profile* profile,
										 int biome_id,
										 const struct gen_cuberite_runtime_args* gen_args,
										 uint32_t seed, int32_t wx, int32_t wz,
										 int surface, uint8_t* top_block,
										 uint8_t* filler_block,
										 int* dirt_depth) {
	(void)profile;
	struct gen_biome_profile surface_profile = gen_profile_for_biome(biome_id);
	uint8_t top = surface_profile.top_block;
	uint8_t filler = surface_profile.filler_block;
	int depth = 3 + (int)(gen_fbm2d((float)wx * 0.031f, (float)wz * 0.031f,
									seed ^ 0x5107ACEDU, 2, 2.0f, 0.5f)
						  * 1.5f);
	if(depth < 1)
		depth = 1;
	if(depth > 6)
		depth = 6;

	if(gen_biome_has_stony_surface(biome_id)) {
		float stone_noise = gen_fbm2d((float)wx * 0.045f, (float)wz * 0.045f,
									  seed ^ 0x570A1E55U, 2, 2.0f, 0.5f);
		int exposed_y = gen_args->sea_level + 34 + (int)(stone_noise * 8.0f);
		if(biome_id == stone_shore
		   || (surface >= exposed_y && stone_noise > 0.42f)) {
			top = BLOCK_STONE;
			filler = BLOCK_STONE;
			depth = 1 + (int)(fabsf(stone_noise) * 2.0f);
		}
	}

	if(gen_biome_has_sandy_surface(biome_id)) {
		top = BLOCK_SAND;
		float sandstone_noise = gen_fbm2d((float)wx * 0.026f, (float)wz * 0.026f,
										  seed ^ 0x5A9D5101U, 1, 2.0f, 0.5f);
		filler = (sandstone_noise > -0.35f) ? BLOCK_SAND : BLOCK_SANDSTONE;
		if(depth < 4)
			depth = 4;
	} else if(biome_id == river || biome_id == frozen_river) {
		top = (surface <= gen_args->sea_level) ? BLOCK_GRAVEL : BLOCK_GRASS;
		filler = BLOCK_DIRT;
	} else if(isOceanic(biome_id)) {
		top = BLOCK_GRAVEL;
		filler = BLOCK_DIRT;
	}

	*top_block = top;
	*filler_block = filler;
	*dirt_depth = depth;
}

static bool gen_is_mountainous_biome(int biome_id) {
	switch(biome_id) {
	case mountains:
	case wooded_mountains:
	case mountain_edge:
	case gravelly_mountains:
	case modified_gravelly_mountains:
	case taiga_hills:
	case giant_tree_taiga_hills:
	case giant_spruce_taiga_hills:
	case jungle_hills:
	case wooded_hills:
	case birch_forest_hills:
		return true;
	default:
		return false;
	}
}

static bool gen_inside_chunk(int x, int y, int z);
static uint8_t gen_get_block(const struct server_chunk* sc, int x, int y, int z);
static void gen_set_block(struct server_chunk* sc, int x, int y, int z,
						  uint8_t type);
static int gen_noise_int2d(uint32_t seed, int x, int z);
static bool gen_sample_neighbor_edge_height(struct server_world* w, w_coord_t cx,
											w_coord_t cz, int dir, int index,
											int* out_height);

enum {
	GEN_CAVE_MIN_RADIUS = 3,
	GEN_CAVE_MAX_RADIUS = 8,
	GEN_CAVE_MAX_POINTS = 512,
	GEN_CAVE_MAX_RECURSION = 5,
	GEN_CAVE_MAX_TUNNELS_PER_CHUNK = 18,
#ifdef PLATFORM_WII
	GEN_CHUNK_COLUMNS_PER_STEP = 10,
#else
	GEN_CHUNK_COLUMNS_PER_STEP = 64,
#endif
	GEN_FEATURE_STEP_COUNT = 16,
	GEN_FINALIZE_STEP_COUNT = 3,
};

void server_world_chunk_destroy(struct server_chunk* sc);

enum gen_chunk_cave_side {
	GEN_CHUNK_CAVE_WEST = 0,
	GEN_CHUNK_CAVE_EAST = 1,
	GEN_CHUNK_CAVE_NORTH = 2,
	GEN_CHUNK_CAVE_SOUTH = 3,
};

struct gen_cave_defpoint {
	int x;
	int y;
	int z;
	int radius;
};

struct gen_cave_tunnel {
	struct gen_cave_defpoint points[GEN_CAVE_MAX_POINTS];
	int count;
	int min_x, max_x;
	int min_y, max_y;
	int min_z, max_z;
};

struct gen_chunk_cave_connector {
	int x;
	int y;
	int z;
	int radius;
	uint32_t seed;
};

struct gen_chunk_cave_trunk_info {
	bool valid;
	int anchor_x;
	int anchor_z;
	bool horizontal;
	int start_chunk_x;
	int end_chunk_x;
	int start_chunk_z;
	int end_chunk_z;
	int y;
	int offset;
	uint32_t seed;
};

static void gen_cave_init_tunnel(struct gen_cave_tunnel* t,
								 int sx, int sy, int sz, int sr,
								 int ex, int ey, int ez, int er,
								 uint32_t seed);
static void gen_cave_process_chunk(const struct gen_cave_tunnel* t, struct server_chunk* sc,
								   int chunk_x, int chunk_z);

static int gen_noise_int3d(uint32_t seed, int x, int y, int z) {
	return (int)gen_hash3i(x, y, z, seed);
}

static int gen_mod_positive(int value, int mod) {
	if(mod <= 0)
		return 0;
	int out = value % mod;
	return (out < 0) ? (out + mod) : out;
}

static int gen_floor_div(int value, int div) {
	if(div <= 0)
		return 0;
	int out = value / div;
	int rem = value % div;
	if((rem != 0) && ((rem < 0) != (div < 0)))
		out -= 1;
	return out;
}

static int gen_cave_radius(uint32_t seed, int x, int y, int z) {
	int rnd = gen_noise_int3d(seed, x, y, z) / 11;
	unsigned int urnd = (unsigned int)rnd;
	int range = GEN_CAVE_MAX_RADIUS - GEN_CAVE_MIN_RADIUS;
	if(range <= 0)
		return GEN_CAVE_MIN_RADIUS;
	int span = (int)(urnd % (unsigned int)range) + 1;
	return GEN_CAVE_MIN_RADIUS + (int)((urnd >> 8) % (unsigned int)span);
}

static int gen_clamp_int(int value, int minv, int maxv) {
	if(value < minv)
		return minv;
	if(value > maxv)
		return maxv;
	return value;
}

static uint32_t gen_chunk_cave_border_hash(uint32_t seed, int chunk_x, int chunk_z,
										   enum gen_chunk_cave_side side, int salt) {
	int key_x = chunk_x;
	int key_z = chunk_z;
	int axis = 0;
	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		key_x = chunk_x - 1;
		axis = 0;
		break;
	case GEN_CHUNK_CAVE_EAST:
		key_x = chunk_x;
		axis = 0;
		break;
	case GEN_CHUNK_CAVE_NORTH:
		key_z = chunk_z - 1;
		axis = 1;
		break;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		key_z = chunk_z;
		axis = 1;
		break;
	}
	return gen_hash3i(key_x, axis * 8191 + salt, key_z, seed ^ 0x5CA1AB1EU);
}

static int gen_chunk_cave_base_border_count(uint32_t seed, int chunk_x, int chunk_z,
											enum gen_chunk_cave_side side) {
	uint32_t h = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 0);
	int roll = (int)(h % 100U);
	if(roll < 62)
		return 1;
	if(roll < 97)
		return 2;
	if(roll < 100)
		return 3;
	return 0;
}

static int gen_chunk_cave_total_base_connectors(uint32_t seed, int chunk_x, int chunk_z) {
	int total = 0;
	for(int side = GEN_CHUNK_CAVE_WEST; side <= GEN_CHUNK_CAVE_SOUTH; side++)
		total += gen_chunk_cave_base_border_count(seed, chunk_x, chunk_z,
												  (enum gen_chunk_cave_side)side);
	return total;
}

static enum gen_chunk_cave_side gen_chunk_cave_forced_side(uint32_t seed, int chunk_x, int chunk_z) {
	return (enum gen_chunk_cave_side)(gen_hash3i(chunk_x, 521, chunk_z, seed) % 4U);
}

static enum gen_chunk_cave_side gen_chunk_cave_opposite_side(enum gen_chunk_cave_side side) {
	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		return GEN_CHUNK_CAVE_EAST;
	case GEN_CHUNK_CAVE_EAST:
		return GEN_CHUNK_CAVE_WEST;
	case GEN_CHUNK_CAVE_NORTH:
		return GEN_CHUNK_CAVE_SOUTH;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		return GEN_CHUNK_CAVE_NORTH;
	}
}

static void gen_chunk_cave_neighbor_for_side(int chunk_x, int chunk_z,
											 enum gen_chunk_cave_side side,
											 int* out_chunk_x, int* out_chunk_z) {
	if(out_chunk_x == NULL || out_chunk_z == NULL)
		return;
	*out_chunk_x = chunk_x;
	*out_chunk_z = chunk_z;
	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		(*out_chunk_x)--;
		break;
	case GEN_CHUNK_CAVE_EAST:
		(*out_chunk_x)++;
		break;
	case GEN_CHUNK_CAVE_NORTH:
		(*out_chunk_z)--;
		break;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		(*out_chunk_z)++;
		break;
	}
}

static bool gen_chunk_cave_border_is_forced(uint32_t seed, int chunk_x, int chunk_z,
											enum gen_chunk_cave_side side) {
	if(gen_chunk_cave_total_base_connectors(seed, chunk_x, chunk_z) == 0
	   && gen_chunk_cave_forced_side(seed, chunk_x, chunk_z) == side)
		return true;

	int neigh_x = chunk_x;
	int neigh_z = chunk_z;
	gen_chunk_cave_neighbor_for_side(chunk_x, chunk_z, side, &neigh_x, &neigh_z);
	return gen_chunk_cave_total_base_connectors(seed, neigh_x, neigh_z) == 0
		&& gen_chunk_cave_forced_side(seed, neigh_x, neigh_z)
			== gen_chunk_cave_opposite_side(side);
}

static int gen_chunk_cave_border_count(uint32_t seed, int chunk_x, int chunk_z,
									   enum gen_chunk_cave_side side) {
	int count = gen_chunk_cave_base_border_count(seed, chunk_x, chunk_z, side);
	if(count > 0)
		return count;
	if(gen_chunk_cave_border_is_forced(seed, chunk_x, chunk_z, side))
		return 1;
	return 0;
}

static bool gen_chunk_cave_make_connector(struct gen_chunk_cave_connector* out,
											 uint32_t seed, int chunk_x, int chunk_z,
											 enum gen_chunk_cave_side side, int index) {
	if(out == NULL)
		return false;
	if(index < 0 || index >= gen_chunk_cave_border_count(seed, chunk_x, chunk_z, side))
		return false;
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	uint32_t pos_h = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 17 + index * 13);
	uint32_t y_h = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 41 + index * 29);
	uint32_t r_h = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 73 + index * 7);
	int edge_offset = 2 + (int)(pos_h % (unsigned int)(CHUNK_SIZE - 4));

	out->seed = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 101 + index * 19);
	out->y = 36 + (int)(y_h % 14U);
	out->radius = 1 + (int)(r_h % 2U);
	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		out->x = base_x;
		out->z = base_z + edge_offset;
		break;
	case GEN_CHUNK_CAVE_EAST:
		out->x = base_x + CHUNK_SIZE - 1;
		out->z = base_z + edge_offset;
		break;
	case GEN_CHUNK_CAVE_NORTH:
		out->x = base_x + edge_offset;
		out->z = base_z;
		break;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		out->x = base_x + edge_offset;
		out->z = base_z + CHUNK_SIZE - 1;
		break;
	}
	return true;
}

static void gen_chunk_cave_find_trunk(struct gen_chunk_cave_trunk_info* out,
									  uint32_t seed, int chunk_x, int chunk_z) {
	if(out == NULL)
		return;
	*out = (struct gen_chunk_cave_trunk_info) {0};

	for(int axis = 0; axis < 2; axis++) {
		for(int back = 0; back < 6; back++) {
			int anchor_x = chunk_x - (axis == 0 ? back : 0);
			int anchor_z = chunk_z - (axis == 1 ? back : 0);
			uint32_t h = gen_hash3i(anchor_x, 1301 + axis * 97, anchor_z, seed);
			if((h % 100U) >= 18U)
				continue;

			bool horizontal = ((h >> 8) & 1U) == 0U;
			if(horizontal && axis != 0)
				continue;
			if(!horizontal && axis != 1)
				continue;

			int length = 5 + (int)((h >> 16) % 2U);
			if(horizontal) {
				if(chunk_z != anchor_z)
					continue;
				if(chunk_x < anchor_x || chunk_x > anchor_x + length)
					continue;
			} else {
				if(chunk_x != anchor_x)
					continue;
				if(chunk_z < anchor_z || chunk_z > anchor_z + length)
					continue;
			}

			out->valid = true;
			out->anchor_x = anchor_x;
			out->anchor_z = anchor_z;
			out->horizontal = horizontal;
			out->start_chunk_x = anchor_x;
			out->end_chunk_x = horizontal ? (anchor_x + length) : anchor_x;
			out->start_chunk_z = anchor_z;
			out->end_chunk_z = horizontal ? anchor_z : (anchor_z + length);
			out->y = 38 + (int)((h >> 24) % 12U);
			out->offset = 3 + (int)((h >> 12) % (unsigned int)(CHUNK_SIZE - 6));
			out->seed = h ^ 0x5EEDC0DEU;
			return;
		}
	}
}

static bool gen_chunk_cave_trunk_make_connector(struct gen_chunk_cave_connector* out,
												const struct gen_chunk_cave_trunk_info* trunk,
												int chunk_x, int chunk_z,
												enum gen_chunk_cave_side side) {
	if(out == NULL || trunk == NULL || !trunk->valid)
		return false;

	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	if(trunk->horizontal) {
		if((side == GEN_CHUNK_CAVE_WEST) && (chunk_x > trunk->start_chunk_x)) {
			out->x = base_x;
			out->z = base_z + trunk->offset;
		} else if((side == GEN_CHUNK_CAVE_EAST) && (chunk_x < trunk->end_chunk_x)) {
			out->x = base_x + CHUNK_SIZE - 1;
			out->z = base_z + trunk->offset;
		} else {
			return false;
		}
	} else {
		if((side == GEN_CHUNK_CAVE_NORTH) && (chunk_z > trunk->start_chunk_z)) {
			out->x = base_x + trunk->offset;
			out->z = base_z;
		} else if((side == GEN_CHUNK_CAVE_SOUTH) && (chunk_z < trunk->end_chunk_z)) {
			out->x = base_x + trunk->offset;
			out->z = base_z + CHUNK_SIZE - 1;
		} else {
			return false;
		}
	}
	out->y = trunk->y;
	out->radius = 1;
	out->seed = trunk->seed ^ (uint32_t)(side * 131U + chunk_x * 17U + chunk_z * 31U);
	return true;
}

static int gen_chunk_cave_collect_connectors(struct gen_chunk_cave_connector* connectors,
											 int max_connectors,
											 uint32_t seed, int chunk_x, int chunk_z) {
	if(connectors == NULL || max_connectors <= 0)
		return 0;
	int connector_count = 0;
	for(int side = GEN_CHUNK_CAVE_WEST; side <= GEN_CHUNK_CAVE_SOUTH; side++) {
		int count = gen_chunk_cave_border_count(seed, chunk_x, chunk_z,
												 (enum gen_chunk_cave_side)side);
		for(int i = 0; i < count && connector_count < max_connectors; i++) {
			if(gen_chunk_cave_make_connector(&connectors[connector_count], seed, chunk_x, chunk_z,
											 (enum gen_chunk_cave_side)side, i)) {
				connector_count++;
			}
		}
	}
	struct gen_chunk_cave_trunk_info trunk;
	gen_chunk_cave_find_trunk(&trunk, seed, chunk_x, chunk_z);
	if(trunk.valid) {
		for(int side = GEN_CHUNK_CAVE_WEST;
			side <= GEN_CHUNK_CAVE_SOUTH && connector_count < max_connectors;
			side++) {
			if(gen_chunk_cave_trunk_make_connector(&connectors[connector_count], &trunk,
												 chunk_x, chunk_z, (enum gen_chunk_cave_side)side)) {
				connector_count++;
			}
		}
	}
	return connector_count;
}

static void gen_chunk_cave_compute_hub(uint32_t seed, int chunk_x, int chunk_z,
									   int* out_hub_x, int* out_hub_y, int* out_hub_z,
									   int* out_connector_count) {
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int hub_x = base_x + 4 + (int)(gen_hash3i(chunk_x, 211, chunk_z, seed) % 8U);
	int hub_z = base_z + 4 + (int)(gen_hash3i(chunk_x, 307, chunk_z, seed) % 8U);
	int hub_y = 26 + (int)(gen_hash3i(chunk_x, 401, chunk_z, seed) % 22U);
	struct gen_chunk_cave_connector connectors[16];
	int connector_count = gen_chunk_cave_collect_connectors(
		connectors, (int)(sizeof(connectors) / sizeof(connectors[0])), seed, chunk_x, chunk_z);

	if(connector_count > 0) {
		int avg_y = 0;
		for(int i = 0; i < connector_count; i++)
			avg_y += connectors[i].y;
		hub_y = gen_clamp_int((hub_y + avg_y / connector_count) / 2, 12, WORLD_HEIGHT - 12);
	}

	if(out_hub_x != NULL)
		*out_hub_x = hub_x;
	if(out_hub_y != NULL)
		*out_hub_y = hub_y;
	if(out_hub_z != NULL)
		*out_hub_z = hub_z;
	if(out_connector_count != NULL)
		*out_connector_count = connector_count;
}

static void gen_carve_chunk_cave_worm_branch(struct server_chunk* sc, uint32_t seed,
											 int chunk_x, int chunk_z,
											 int start_x, int start_y, int start_z,
											 int start_dir, int segments,
											 int split_depth) {
	if(sc == NULL || segments <= 0)
		return;
	int cur_x = start_x;
	int cur_y = start_y;
	int cur_z = start_z;
	int dir = start_dir & 3;

	for(int seg = 0; seg < segments; seg++) {
		int len = 10 + (int)(gen_hash3i(chunk_x, 1501 + seg * 41, chunk_z, seed) % 9U);
		int dy = (int)(gen_hash3i(chunk_x, 1543 + seg * 43, chunk_z, seed) % 3U) - 1;
		int end_x = cur_x;
		int end_z = cur_z;
		int end_y = gen_clamp_int(cur_y + dy, 10, WORLD_HEIGHT - 10);
		switch(dir) {
		case 0:
			end_x += len;
			break;
		case 1:
			end_x -= len;
			break;
		case 2:
			end_z += len;
			break;
		default:
			end_z -= len;
			break;
		}

		struct gen_cave_tunnel tunnel;
		gen_cave_init_tunnel(&tunnel,
							 cur_x, cur_y, cur_z, 1,
							 end_x, end_y, end_z, 1,
							 seed ^ (uint32_t)(0xD00D100U + seg * 59U));
		gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);

		if(split_depth > 0 && seg >= 1
		   && (int)(gen_hash3i(chunk_x, 1589 + seg * 47, chunk_z, seed) % 100U) < 18) {
			int side_dir = (dir + 1 + ((int)(gen_hash3i(chunk_x, 1627 + seg * 53, chunk_z, seed) & 1U) * 2)) % 4;
			gen_carve_chunk_cave_worm_branch(
				sc, seed ^ (uint32_t)(0xD00D900U + seg * 61U), chunk_x, chunk_z,
				end_x, end_y, end_z, side_dir, segments - seg + 1, split_depth - 1);
		}

		int turn = (int)(gen_hash3i(chunk_x, 1667 + seg * 67, chunk_z, seed) % 3U);
		if(turn == 0)
			dir = (dir + 1) % 4;
		else if(turn == 1)
			dir = (dir + 3) % 4;

		cur_x = end_x;
		cur_y = end_y;
		cur_z = end_z;
	}
}

static void gen_carve_chunk_cave_local_network(struct server_chunk* sc, uint32_t seed,
											   int chunk_x, int chunk_z,
											   int hub_x, int hub_y, int hub_z,
											   int max_branches) {
	struct gen_cave_network_node {
		int x;
		int y;
		int z;
		int r;
	};
	if(sc == NULL || max_branches <= 0)
		return;
	struct gen_cave_network_node nodes[18];
	int node_count = 7 + (int)(gen_hash3i(chunk_x, 887, chunk_z, seed) % 4U);
	if(node_count > 10)
		node_count = 10;
	if(node_count < 7)
		node_count = 7;

	nodes[0].x = hub_x;
	nodes[0].y = hub_y;
	nodes[0].z = hub_z;
	nodes[0].r = 1;

	for(int i = 1; i < node_count; i++) {
		int parent = (i == 1) ? 0 : (int)(gen_hash3i(chunk_x, 911 + i * 13, chunk_z, seed) % (unsigned int)i);
		int dir = (int)(gen_hash3i(chunk_x, 947 + i * 17, chunk_z, seed) % 4U);
		int len = 4 + (int)(gen_hash3i(chunk_x, 983 + i * 19, chunk_z, seed) % 4U);
		int dy = (int)(gen_hash3i(chunk_x, 1019 + i * 23, chunk_z, seed) % 3U) - 1;
		int x = nodes[parent].x;
		int z = nodes[parent].z;
		int y = gen_clamp_int(nodes[parent].y + dy, 10, WORLD_HEIGHT - 10);
		int r = 1;

		switch(dir) {
		case 0:
			x += len;
			break;
		case 1:
			x -= len;
			break;
		case 2:
			z += len;
			break;
		default:
			z -= len;
			break;
		}

		nodes[i].x = x;
		nodes[i].y = y;
		nodes[i].z = z;
		nodes[i].r = r;

		struct gen_cave_tunnel trunk;
		gen_cave_init_tunnel(&trunk,
							 nodes[parent].x, nodes[parent].y, nodes[parent].z, nodes[parent].r,
							 nodes[i].x, nodes[i].y, nodes[i].z, nodes[i].r,
							 seed ^ (uint32_t)(0xCA7E100U + i * 97U));
		gen_cave_process_chunk(&trunk, sc, chunk_x, chunk_z);
	}

	for(int i = 1; i + 2 < node_count; i++) {
		if((int)(gen_hash3i(chunk_x, 1097 + i * 31, chunk_z, seed) % 100U) >= 8U)
			continue;
		int j = i + 1 + (int)(gen_hash3i(chunk_x, 1129 + i * 37, chunk_z, seed)
							  % (unsigned int)(node_count - i - 1));
		if(j >= node_count)
			j = node_count - 1;
		struct gen_cave_tunnel cross;
		gen_cave_init_tunnel(&cross,
							 nodes[i].x, nodes[i].y, nodes[i].z, 1,
							 nodes[j].x, nodes[j].y, nodes[j].z, 1,
							 seed ^ (uint32_t)(0xCA7E500U + i * 53U + j * 17U));
		gen_cave_process_chunk(&cross, sc, chunk_x, chunk_z);
	}

	for(int i = 1; i < node_count; i++) {
		int branch_count = 1 + (int)(gen_hash3i(chunk_x, 1163 + i * 41, chunk_z, seed) % 2U);
		for(int b = 0; b < branch_count; b++) {
			int dir = (int)(gen_hash3i(chunk_x, 1201 + i * 43 + b * 11, chunk_z, seed) % 4U);
			int segs = 4 + (int)(gen_hash3i(chunk_x, 1237 + i * 47 + b * 13, chunk_z, seed) % 4U);
			gen_carve_chunk_cave_worm_branch(
				sc, seed ^ (uint32_t)(0xCA7E900U + i * 61U + b * 17U), chunk_x, chunk_z,
				nodes[i].x, nodes[i].y, nodes[i].z, dir, segs, 2);
		}
	}
}

static void gen_carve_chunk_linked_caves(struct server_chunk* sc, uint32_t seed,
											 int chunk_x, int chunk_z) {
	struct gen_chunk_cave_connector connectors[16];
	int connector_count = 0;
	int hub_x = 0;
	int hub_y = 0;
	int hub_z = 0;

	gen_chunk_cave_compute_hub(seed, chunk_x, chunk_z, &hub_x, &hub_y, &hub_z, &connector_count);
	connector_count = gen_chunk_cave_collect_connectors(
		connectors, (int)(sizeof(connectors) / sizeof(connectors[0])), seed, chunk_x, chunk_z);

	if(connector_count == 0) {
		uint32_t h = gen_hash3i(chunk_x, 509, chunk_z, seed);
		if((h % 100U) >= 22U)
			return;
		hub_y = 18 + (int)(gen_hash3i(chunk_x, 557, chunk_z, seed) % 30U);
		gen_carve_chunk_cave_local_network(sc, seed ^ 0x4410U, chunk_x, chunk_z,
										   hub_x, hub_y, hub_z, 3);
		return;
	}

	int tunnel_budget = 8;
	for(int i = 0; i < connector_count && tunnel_budget > 0; i++) {
		struct gen_cave_tunnel tunnel;
		int end_x = hub_x + (int)(gen_hash3i(chunk_x, 601 + i, chunk_z, connectors[i].seed) % 7U) - 3;
		int end_z = hub_z + (int)(gen_hash3i(chunk_x, 677 + i, chunk_z, connectors[i].seed) % 7U) - 3;
		int end_y = gen_clamp_int(hub_y + (int)(gen_hash3i(chunk_x, 733 + i, chunk_z, connectors[i].seed) % 3U) - 1,
								  10, WORLD_HEIGHT - 10);
		int end_r = 1;
		gen_cave_init_tunnel(&tunnel, connectors[i].x, connectors[i].y, connectors[i].z,
							 1, end_x, end_y, end_z, end_r,
							 connectors[i].seed);
		gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
		tunnel_budget--;
	}

	if(connector_count >= 2 && tunnel_budget > 0) {
		for(int i = 0; i + 1 < connector_count && tunnel_budget > 0; i += 2) {
			struct gen_cave_tunnel tunnel;
			gen_cave_init_tunnel(&tunnel,
								 connectors[i].x, connectors[i].y, connectors[i].z, connectors[i].radius,
								 connectors[i + 1].x, connectors[i + 1].y, connectors[i + 1].z, connectors[i + 1].radius,
								 seed ^ (uint32_t)(9001 + i * 37));
			gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
			tunnel_budget--;
		}
	}

	gen_carve_chunk_cave_local_network(sc, seed ^ 0xA11CEU, chunk_x, chunk_z,
									   hub_x, hub_y, hub_z, tunnel_budget);
}

/* Neuer Eingang-Generator: gräbt einen begehbaren Schacht von der Oberfläche
 * nach unten, bis er eine existierende Wurmhöhle (Luft) trifft. So verbinden
 * sich die Eingänge garantiert mit dem Tunnel-System (gen_carve_worm_caves,
 * das VOR diesem Step in derselben Chunk-Generierung läuft). */
static void gen_carve_cave_entrance_to_worm(struct server_chunk* sc, uint32_t seed,
                                            int chunk_x, int chunk_z,
                                            const int* surface_map, int sea_level) {
	if(sc == NULL || surface_map == NULL)
		return;

	/* Selten halten: sonst wirken die Eingänge wie dauernde Schluchten. */
	if((gen_hash3i(chunk_x, 941, chunk_z, seed ^ 0x3A7C001U) % 100U) >= 6U)
		return;

	/* Bis zu 3 Kandidaten-Positionen testen */
	for(int attempt = 0; attempt < 3; attempt++) {
		uint32_t h = gen_hash3i(chunk_x, 97 + attempt * 31, chunk_z,
		                        seed ^ (0xE17A100U + (uint32_t)attempt * 53U));
		/* zentraler platzieren (6..9), damit die grosse Spirale in den Chunk passt */
		int lx = 6 + (int)(h % 4U);
		int lz = 6 + (int)((h >> 8) % 4U);

		int surface = surface_map[lx + lz * CHUNK_SIZE];
		if(surface < sea_level + 8 || surface >= WORLD_HEIGHT - 2)
			continue; /* kein Eingang an Wasser/Küste oder zu hoch */

		/* Nach unten sondieren: gibt es eine Höhle (Luft) unter der Oberfläche? */
		int cave_y = -1;
		for(int y = surface - 4; y >= 6; y--) {
			if(gen_get_block(sc, lx, y, lz) == BLOCK_AIR) {
				cave_y = y;
				break;
			}
		}
		if(cave_y < 0)
			continue; /* keine Höhle in Reichweite → kein isolierter Eingang */

		int depth = surface - cave_y;
		float radius = 1.7f; /* Tunnel-Dicke */

		/* "Turtle"-Abstieg: der Pfad bewegt sich pro Schritt konstant vorwaerts
		 * und dreht dabei sanft. Die Drehrate oszilliert (cos) und geht durch
		 * Null -> am Nulldurchgang laeuft der Pfad gerade, dann dreht er
		 * andersherum: fluessiger Links-Rechts-Uebergang ohne Knick, der Anfang
		 * der Linkskurve schliesst nahtlos an das Ende der Rechtskurve an. */
		uint32_t h2 = gen_hash3i(chunk_x, 211 + attempt, chunk_z,
		                         seed ^ (0xB1A5000U + (uint32_t)attempt * 71U));
		float heading   = (float)(h % 628) / 100.0f;             /* Start-Richtung */
		float turn_amp  = 0.22f + (float)(h2 % 25) / 100.0f;     /* 0.22..0.47 Dreh-Amplitude */
		float turn_freq = 0.14f + (float)((h2 >> 5) % 14) / 100.0f; /* Wende-Frequenz */
		int   turn_dir  = (h2 & 1u) ? 1 : -1;
		float steep_ph  = (float)((h2 >> 9) % 628) / 100.0f;     /* Phase der Steilheits-Variation */

		float px = (float)lx, pz = (float)lz;
		float cur_y = (float)(surface + 1);
		int max_steps = depth * 4 + 24;

		/* Helfer-Makro: Kugel bei (cx,cy,cz) mit Tunnel-Radius carven */
#define WGEN_CARVE_SPHERE(CXI, CYI, CZI) do { \
			int _ir = (int)(radius) + 1; \
			for(int _dy = -_ir; _dy <= _ir; _dy++) \
				for(int _dx = -_ir; _dx <= _ir; _dx++) \
					for(int _dz = -_ir; _dz <= _ir; _dz++) { \
						if((float)(_dx*_dx + _dy*_dy + _dz*_dz) > radius*radius + 0.5f) continue; \
						int _nx = (CXI)+_dx, _ny = (CYI)+_dy, _nz = (CZI)+_dz; \
						if(_nx<0||_nx>=CHUNK_SIZE||_nz<0||_nz>=CHUNK_SIZE) continue; \
						if(_ny<1||_ny>=WORLD_HEIGHT) continue; \
						uint8_t _b = gen_get_block(sc, _nx, _ny, _nz); \
						if(_b!=BLOCK_BEDROCK) \
							gen_set_block(sc, _nx, _ny, _nz, BLOCK_AIR); \
					} \
		} while(0)

		for(int s = 0; s < max_steps && cur_y > (float)cave_y; s++) {
			WGEN_CARVE_SPHERE((int)(px + 0.5f), (int)(cur_y + 0.5f), (int)(pz + 0.5f));

			/* Heading sanft drehen (oszillierend → glatter Links/Rechts-Wechsel) */
			float omega = (float)turn_dir * turn_amp * cosf((float)s * turn_freq);
			heading += omega;

			/* horizontaler Schritt (konstant vorwaerts) */
			px += cosf(heading);
			pz += sinf(heading);

			/* Je naeher am Hoehlen-Ziel, desto staerker zurueck zur gefundenen
			 * Hoehlen-Position (lx,lz) lenken, damit der Eingang GARANTIERT dort
			 * endet und ins Tunnelsystem muendet (nicht im Stein endet). */
			float depth_remaining = cur_y - (float)cave_y;
			float to_cx = (float)lx - px, to_cz = (float)lz - pz;
			float dist2_c = to_cx*to_cx + to_cz*to_cz;
			if(depth_remaining < 12.0f && dist2_c > 1.0f) {
				/* direkt Richtung Ziel schreiten (Winkel-sicher, kein atan2-Wrap) */
				float inv = 1.0f / sqrtf(dist2_c);
				px += to_cx * inv * 0.6f;
				pz += to_cz * inv * 0.6f;
				heading = atan2f(to_cz, to_cx);
			} else if(dist2_c > 49.0f) {
				/* zu weit abgedriftet → sanft zurueck */
				px += to_cx / sqrtf(dist2_c) * 0.3f;
				pz += to_cz / sqrtf(dist2_c) * 0.3f;
			}

			/* Steilheit: oben sehr steil (~1 vor, 1 runter = 45°+), unten
			 * flacher und hin und wieder steiler/flacher (sinus-moduliert) */
			float depth_done = (float)(surface + 1) - cur_y;
			float y_step;
			if(depth_done < 5.0f) {
				y_step = 1.3f;           /* steiler Einstieg an der Oberflaeche */
			} else {
				y_step = 0.55f + 0.55f * (0.5f + 0.5f * sinf((float)s * 0.22f + steep_ph));
				/* → ca. 0.55..1.1: meist flach, hin und wieder steiler */
			}
			cur_y -= y_step;
		}

		/* Sicherheits-Verbindung: vom Turtle-Ende gerade hinunter zur Hoehle
		 * graben, bis echte Hoehlen-Luft getroffen ist (garantierte Mündung). */
		for(int vy = (int)(cur_y + 0.5f) + 2; vy >= cave_y - 1 && vy >= 1; vy--) {
			WGEN_CARVE_SPHERE(lx, vy, lz);
		}
#undef WGEN_CARVE_SPHERE
		return; /* ein Eingang pro Chunk reicht */
	}
}

static void gen_carve_chunk_cave_entrance(struct server_chunk* sc, uint32_t seed,
										  int chunk_x, int chunk_z,
										  const int* surface_map,
										  Generator* biome_gen,
										  const struct gen_cuberite_runtime_args* gen_args) {
	if((sc == NULL) || (surface_map == NULL))
		return;

	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int center_biome = plains;
	if(biome_gen != NULL) {
		center_biome = gen_biome_at_safe(
			biome_gen, base_x + CHUNK_SIZE / 2, base_z + CHUNK_SIZE / 2);
	}
	int min_surface = WORLD_HEIGHT;
	int max_surface = 0;
	for(int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; i++) {
		int s = surface_map[i];
		if(s < min_surface)
			min_surface = s;
		if(s > max_surface)
			max_surface = s;
	}
	bool mountainous_chunk = gen_is_mountainous_biome(center_biome)
		|| ((max_surface - min_surface) >= 14);
	unsigned int entrance_chance = mountainous_chunk ? 60U : 25U;
	if((gen_hash3i(chunk_x, 941, chunk_z, seed) % 100U) >= entrance_chance)
		return;

	int hub_x = 0;
	int hub_y = 0;
	int hub_z = 0;
	gen_chunk_cave_compute_hub(seed, chunk_x, chunk_z, &hub_x, &hub_y, &hub_z, NULL);
	struct gen_chunk_cave_trunk_info trunk;
	gen_chunk_cave_find_trunk(&trunk, seed, chunk_x, chunk_z);
	if(!trunk.valid)
		return;
	struct gen_chunk_cave_connector connectors[16];
	int connector_count = gen_chunk_cave_collect_connectors(
		connectors, (int)(sizeof(connectors) / sizeof(connectors[0])), seed, chunk_x, chunk_z);
	if(connector_count <= 0)
		return;
	int entrance_lx = -1;
	int entrance_lz = -1;
	int best_score = -999999;
	int required_surface = mountainous_chunk ? 62 : 58;
	int required_slope = mountainous_chunk ? 4 : 3;
	for(int attempt = 0; attempt < 32; attempt++) {
		int lx = 4 + (int)(gen_hash3i(chunk_x, 953 + attempt * 17, chunk_z, seed) % (unsigned int)(CHUNK_SIZE - 8));
		int lz = 4 + (int)(gen_hash3i(chunk_x, 967 + attempt * 19, chunk_z, seed) % (unsigned int)(CHUNK_SIZE - 8));
		int surface = surface_map[lx + lz * CHUNK_SIZE];
		if(surface < required_surface)
			continue;
		int min_neigh = surface;
		int max_neigh = surface;
		const int nx[4] = {1, -1, 0, 0};
		const int nz[4] = {0, 0, 1, -1};
		for(int i = 0; i < 4; i++) {
			int nh = surface_map[(lx + nx[i]) + (lz + nz[i]) * CHUNK_SIZE];
			if(nh < min_neigh)
				min_neigh = nh;
			if(nh > max_neigh)
				max_neigh = nh;
		}
		int slope = max_neigh - min_neigh;
		if(slope < required_slope)
			continue;
		int score = surface * 2 + slope * 6 - abs(lx - CHUNK_SIZE / 2) - abs(lz - CHUNK_SIZE / 2);
		if(score > best_score) {
			best_score = score;
			entrance_lx = lx;
			entrance_lz = lz;
		}
	}
	if(entrance_lx < 0 || entrance_lz < 0) {
		for(int attempt = 0; attempt < 16; attempt++) {
			int lx = 4 + (int)(gen_hash3i(chunk_x, 1153 + attempt * 23, chunk_z, seed)
							   % (unsigned int)(CHUNK_SIZE - 8));
			int lz = 4 + (int)(gen_hash3i(chunk_x, 1181 + attempt * 29, chunk_z, seed)
							   % (unsigned int)(CHUNK_SIZE - 8));
			int surface = surface_map[lx + lz * CHUNK_SIZE];
			if(surface < 54)
				continue;
			int score = surface - abs(lx - CHUNK_SIZE / 2) - abs(lz - CHUNK_SIZE / 2);
			if(score > best_score) {
				best_score = score;
				entrance_lx = lx;
				entrance_lz = lz;
			}
		}
	}
	if(entrance_lx < 0 || entrance_lz < 0)
		return;
	int surface = surface_map[entrance_lx + entrance_lz * CHUNK_SIZE];
	if(surface < hub_y + 6)
		surface = hub_y + 6;
	if(surface > WORLD_HEIGHT - 4)
		surface = WORLD_HEIGHT - 4;
	int entrance_min_neigh = surface;
	int entrance_max_neigh = surface;
	const int enx[4] = {1, -1, 0, 0};
	const int enz[4] = {0, 0, 1, -1};
	for(int i = 0; i < 4; i++) {
		int nh = surface_map[(entrance_lx + enx[i]) + (entrance_lz + enz[i]) * CHUNK_SIZE];
		if(nh < entrance_min_neigh)
			entrance_min_neigh = nh;
		if(nh > entrance_max_neigh)
			entrance_max_neigh = nh;
	}
	int entrance_slope = entrance_max_neigh - entrance_min_neigh;
	bool underwater_entrance = false;
	if(gen_args != NULL) {
		underwater_entrance = (surface <= gen_args->sea_level)
			|| gen_is_water(sc, entrance_lx, gen_clamp_int(surface + 1, 1, WORLD_HEIGHT - 1), entrance_lz);
	}
	bool flat_entrance = !underwater_entrance && (entrance_slope <= 1);

	int entrance_x = base_x + entrance_lx;
	int entrance_z = base_z + entrance_lz;
	struct gen_chunk_cave_connector target_connector;
	bool have_target = false;
	int target_score = 0x7fffffff;
	for(int side = GEN_CHUNK_CAVE_WEST; side <= GEN_CHUNK_CAVE_SOUTH; side++) {
		struct gen_chunk_cave_connector cand;
		if(!gen_chunk_cave_trunk_make_connector(&cand, &trunk, chunk_x, chunk_z,
												 (enum gen_chunk_cave_side)side))
			continue;
		int dx = abs(cand.x - entrance_x);
		int dz = abs(cand.z - entrance_z);
		int dy = abs(cand.y - hub_y);
		int score = dx + dz + dy * 2;
		if(score < target_score) {
			target_score = score;
			target_connector = cand;
			have_target = true;
		}
	}
	if(!have_target) {
		for(int i = 0; i < connector_count; i++) {
			int dx = abs(connectors[i].x - entrance_x);
			int dz = abs(connectors[i].z - entrance_z);
			int dy = abs(connectors[i].y - hub_y);
			int score = dx + dz + dy * 2;
			if(score < target_score) {
				target_score = score;
				target_connector = connectors[i];
				have_target = true;
			}
		}
	}
	if(!have_target)
		return;
	const struct gen_chunk_cave_connector* target = &target_connector;
	int mouth_y = underwater_entrance ? gen_clamp_int(surface + 1, 1, WORLD_HEIGHT - 3) : surface;
	int step1_x = entrance_x + (target->x - entrance_x) / 4;
	int step1_z = entrance_z + (target->z - entrance_z) / 4;
	int step1_y = 0;
	int step2_x = entrance_x + ((target->x - entrance_x) * 2) / 4;
	int step2_z = entrance_z + ((target->z - entrance_z) * 2) / 4;
	int step2_y = 0;
	int step3_x = entrance_x + ((target->x - entrance_x) * 3) / 4;
	int step3_z = entrance_z + ((target->z - entrance_z) * 3) / 4;
	int step3_y = 0;
	int target_y = gen_clamp_int(target->y, 10, WORLD_HEIGHT - 10);
	int approach_y = target_y;
	if(approach_y < surface - 10)
		approach_y = surface - 10;
	approach_y = gen_clamp_int(approach_y, 12, WORLD_HEIGHT - 10);

	if(underwater_entrance) {
		step1_y = gen_clamp_int(surface, approach_y + 6, surface);
		step2_y = gen_clamp_int(step1_y - 1, approach_y + 4, step1_y);
		step3_y = gen_clamp_int(step2_y - 1, approach_y + 2, step2_y);
	} else if(flat_entrance) {
		int pit_floor = gen_clamp_int(surface, approach_y + 6, surface);
		step1_y = pit_floor;
		step2_y = gen_clamp_int(step1_y - 1, approach_y + 4, step1_y);
		step3_y = gen_clamp_int(step2_y - 1, approach_y + 2, step2_y);
	} else {
		step1_y = gen_clamp_int(surface, approach_y + 8, surface);
		step2_y = gen_clamp_int(step1_y - 1, approach_y + 5, step1_y);
		step3_y = gen_clamp_int(step2_y - 1, approach_y + 2, step2_y);
	}

	struct gen_cave_tunnel mouth;
	gen_cave_init_tunnel(&mouth,
						 entrance_x, mouth_y, entrance_z, 1,
						 step1_x, step1_y, step1_z, 1,
						 seed ^ 0xE17A001U);
	gen_cave_process_chunk(&mouth, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel link1;
	gen_cave_init_tunnel(&link1,
						 step1_x, step1_y, step1_z, 1,
						 step2_x, step2_y, step2_z, 1,
						 seed ^ 0xE17A021U);
	gen_cave_process_chunk(&link1, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel link2;
	gen_cave_init_tunnel(&link2,
						 step2_x, step2_y, step2_z, 1,
						 step3_x, step3_y, step3_z, 1,
						 seed ^ 0xE17A041U);
	gen_cave_process_chunk(&link2, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel final_link;
	gen_cave_init_tunnel(&final_link,
						 step3_x, step3_y, step3_z, 1,
						 target->x, target_y, target->z, 1,
						 seed ^ 0xE17A061U);
	gen_cave_process_chunk(&final_link, sc, chunk_x, chunk_z);

	int open_top = flat_entrance ? surface : mouth_y + 1;
	for(int y = surface; y <= open_top && y < WORLD_HEIGHT; y++) {
		uint8_t b = gen_get_block(sc, entrance_lx, y, entrance_lz);
		if(b != BLOCK_BEDROCK)
			gen_set_block(sc, entrance_lx, y, entrance_lz, BLOCK_AIR);
	}

	if(underwater_entrance) {
		int min_x = gen_clamp_int(entrance_lx - 3, 0, CHUNK_SIZE - 1);
		int max_x = gen_clamp_int(entrance_lx + 3, 0, CHUNK_SIZE - 1);
		int min_z = gen_clamp_int(entrance_lz - 3, 0, CHUNK_SIZE - 1);
		int max_z = gen_clamp_int(entrance_lz + 3, 0, CHUNK_SIZE - 1);
		int min_y = gen_clamp_int(hub_y - 1, 1, WORLD_HEIGHT - 1);
		int max_y = gen_clamp_int(surface + 2, 1, WORLD_HEIGHT - 1);
		for(int lx = min_x; lx <= max_x; lx++) {
			for(int lz = min_z; lz <= max_z; lz++) {
				for(int y = min_y; y <= max_y; y++) {
					if(gen_get_block(sc, lx, y, lz) == BLOCK_AIR)
						gen_set_block(sc, lx, y, lz, BLOCK_WATER_STILL);
				}
			}
		}
	}
}

static bool gen_cave_push_point(struct gen_cave_tunnel* t, int x, int y, int z, int radius) {
	if(t->count >= GEN_CAVE_MAX_POINTS)
		return false;
	t->points[t->count++] = (struct gen_cave_defpoint) {
		.x = x, .y = y, .z = z, .radius = radius
	};
	return true;
}

static void gen_cave_randomize(struct gen_cave_tunnel* t, uint32_t seed) {
	for(int round = 0; round < 4; round++) {
		struct gen_cave_defpoint tmp[GEN_CAVE_MAX_POINTS];
		int new_count = 0;
		struct gen_cave_defpoint prev = t->points[0];
		tmp[new_count++] = prev;
		for(int i = 1; i < t->count && new_count + 2 < GEN_CAVE_MAX_POINTS; i++) {
			struct gen_cave_defpoint cur = t->points[i];
			int random = gen_noise_int3d(seed, prev.x, prev.y, prev.z + round) / 11;
			int len = (prev.x - cur.x) * (prev.x - cur.x)
				+ (prev.y - cur.y) * (prev.y - cur.y)
				+ (prev.z - cur.z) * (prev.z - cur.z);
			len = 3 * (int)sqrt((double)len) / 4;
			int rad = (prev.radius + cur.radius) / 2 + (random % 3) - 1;
			if(rad < GEN_CAVE_MIN_RADIUS)
				rad = GEN_CAVE_MIN_RADIUS;
			if(rad > GEN_CAVE_MAX_RADIUS)
				rad = GEN_CAVE_MAX_RADIUS;
			random /= 4;
			int x = (cur.x + prev.x) / 2 + (random % (len + 1) - len / 2);
			random /= 256;
			int y = (cur.y + prev.y) / 2 + (random % (len / 2 + 1) - len / 4);
			random /= 256;
			int z = (cur.z + prev.z) / 2 + (random % (len + 1) - len / 2);
			tmp[new_count++] = (struct gen_cave_defpoint) {.x = x, .y = y, .z = z, .radius = rad};
			tmp[new_count++] = cur;
			prev = cur;
		}
		memcpy(t->points, tmp, sizeof(struct gen_cave_defpoint) * (size_t)new_count);
		t->count = new_count;
	}
}

static bool gen_cave_refine(const struct gen_cave_defpoint* src, int src_count,
							struct gen_cave_defpoint* dst, int* dst_count) {
	if(src_count < 2) {
		*dst_count = src_count;
		if(src_count > 0)
			memcpy(dst, src, sizeof(struct gen_cave_defpoint) * (size_t)src_count);
		return false;
	}
	bool res = false;
	int out = 0;
	dst[out++] = src[0];
	int prev_x = src[0].x, prev_y = src[0].y, prev_z = src[0].z, prev_r = src[0].radius;
	for(int i = 1; i < src_count && out + 3 < GEN_CAVE_MAX_POINTS; i++) {
		int dx = src[i].x - prev_x;
		int dy = src[i].y - prev_y;
		int dz = src[i].z - prev_z;
		if(abs(dx) + abs(dy) + abs(dz) < 6) {
			prev_x = src[i].x; prev_y = src[i].y; prev_z = src[i].z; prev_r = src[i].radius;
			continue;
		}
		int dr = src[i].radius - prev_r;
		int rad1 = prev_r + dr / 4;
		int rad2 = prev_r + (3 * dr) / 4;
		if(rad1 < 1) rad1 = 1;
		if(rad2 < 1) rad2 = 1;
		dst[out++] = (struct gen_cave_defpoint){prev_x + dx / 4, prev_y + dy / 4, prev_z + dz / 4, rad1};
		dst[out++] = (struct gen_cave_defpoint){prev_x + (3 * dx) / 4, prev_y + (3 * dy) / 4, prev_z + (3 * dz) / 4, rad2};
		prev_x = src[i].x; prev_y = src[i].y; prev_z = src[i].z; prev_r = src[i].radius;
		res = true;
	}
	dst[out++] = src[src_count - 1];
	*dst_count = out;
	return res && (out > src_count);
}

static void gen_cave_smooth(struct gen_cave_tunnel* t) {
	struct gen_cave_defpoint tmp[GEN_CAVE_MAX_POINTS];
	int tmp_count = 0;
	for(;;) {
		if(!gen_cave_refine(t->points, t->count, tmp, &tmp_count)) {
			memcpy(t->points, tmp, sizeof(struct gen_cave_defpoint) * (size_t)tmp_count);
			t->count = tmp_count;
			return;
		}
		if(!gen_cave_refine(tmp, tmp_count, t->points, &t->count))
			return;
	}
}

static void gen_cave_finish_linear(struct gen_cave_tunnel* t) {
	struct gen_cave_defpoint src[GEN_CAVE_MAX_POINTS];
	int src_count = t->count;
	memcpy(src, t->points, sizeof(struct gen_cave_defpoint) * (size_t)src_count);
	t->count = 0;
	int prev_x = src[0].x, prev_y = src[0].y, prev_z = src[0].z;
	for(int i = 1; i < src_count; i++) {
		int x1 = src[i].x, y1 = src[i].y, z1 = src[i].z;
		int dx = abs(x1 - prev_x), dy = abs(y1 - prev_y), dz = abs(z1 - prev_z);
		int sx = (prev_x < x1) ? 1 : -1;
		int sy = (prev_y < y1) ? 1 : -1;
		int sz = (prev_z < z1) ? 1 : -1;
		int r = src[i].radius;
		if(dx >= dy && dx >= dz) {
			int yd = dy - dx / 2;
			int zd = dz - dx / 2;
			for(;;) {
				if(!gen_cave_push_point(t, prev_x, prev_y, prev_z, r))
					return;
				if(prev_x == x1) break;
				if(yd >= 0) { prev_y += sy; yd -= dx; }
				if(zd >= 0) { prev_z += sz; zd -= dx; }
				prev_x += sx; yd += dy; zd += dz;
			}
		} else if(dy >= dx && dy >= dz) {
			int xd = dx - dy / 2;
			int zd = dz - dy / 2;
			for(;;) {
				if(!gen_cave_push_point(t, prev_x, prev_y, prev_z, r))
					return;
				if(prev_y == y1) break;
				if(xd >= 0) { prev_x += sx; xd -= dy; }
				if(zd >= 0) { prev_z += sz; zd -= dy; }
				prev_y += sy; xd += dx; zd += dz;
			}
		} else {
			int xd = dx - dz / 2;
			int yd = dy - dz / 2;
			for(;;) {
				if(!gen_cave_push_point(t, prev_x, prev_y, prev_z, r))
					return;
				if(prev_z == z1) break;
				if(xd >= 0) { prev_x += sx; xd -= dz; }
				if(yd >= 0) { prev_y += sy; yd -= dz; }
				prev_z += sz; xd += dx; yd += dy;
			}
		}
	}
}

static void gen_cave_calc_bounds(struct gen_cave_tunnel* t) {
	if(t->count <= 0) {
		t->min_x = t->max_x = t->min_y = t->max_y = t->min_z = t->max_z = 0;
		return;
	}
	t->min_x = t->max_x = t->points[0].x;
	t->min_y = t->max_y = t->points[0].y;
	t->min_z = t->max_z = t->points[0].z;
	for(int i = 1; i < t->count; i++) {
		t->min_x = (t->min_x < t->points[i].x - t->points[i].radius) ? t->min_x : t->points[i].x - t->points[i].radius;
		t->max_x = (t->max_x > t->points[i].x + t->points[i].radius) ? t->max_x : t->points[i].x + t->points[i].radius;
		t->min_y = (t->min_y < t->points[i].y - t->points[i].radius) ? t->min_y : t->points[i].y - t->points[i].radius;
		t->max_y = (t->max_y > t->points[i].y + t->points[i].radius) ? t->max_y : t->points[i].y + t->points[i].radius;
		t->min_z = (t->min_z < t->points[i].z - t->points[i].radius) ? t->min_z : t->points[i].z - t->points[i].radius;
		t->max_z = (t->max_z > t->points[i].z + t->points[i].radius) ? t->max_z : t->points[i].z + t->points[i].radius;
	}
}

static void gen_cave_init_tunnel(struct gen_cave_tunnel* t,
								 int sx, int sy, int sz, int sr,
								 int ex, int ey, int ez, int er,
								 uint32_t seed) {
	t->count = 0;
	gen_cave_push_point(t, sx, sy, sz, sr);
	gen_cave_push_point(t, ex, ey, ez, er);
	if((sy <= 0) && (ey <= 0)) {
		t->min_x = t->max_x = 0;
		t->min_y = t->max_y = -1;
		t->min_z = t->max_z = 0;
		return;
	}
	gen_cave_randomize(t, seed);
	gen_cave_smooth(t);
	gen_cave_calc_bounds(t);
	gen_cave_finish_linear(t);
}

static void gen_cave_process_chunk(const struct gen_cave_tunnel* t, struct server_chunk* sc,
								   int chunk_x, int chunk_z) {
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	if((base_x > t->max_x) || (base_x + CHUNK_SIZE < t->min_x)
	   || (base_z > t->max_z) || (base_z + CHUNK_SIZE < t->min_z)) {
		return;
	}
	int block_start_x = base_x;
	int block_start_z = base_z;
	int block_end_x = block_start_x + CHUNK_SIZE;
	int block_end_z = block_start_z + CHUNK_SIZE;
	for(int i = 0; i < t->count; i++) {
		const struct gen_cave_defpoint* p = &t->points[i];
		if((p->x + p->radius < block_start_x) || (p->x - p->radius > block_end_x)
		   || (p->z + p->radius < block_start_z) || (p->z - p->radius > block_end_z)) {
			continue;
		}
		int dif_x = p->x - block_start_x;
		int dif_y = p->y;
		int dif_z = p->z - block_start_z;
		int bottom = p->y - p->radius;
		int top = p->y + p->radius;
		if(bottom < 1) bottom = 1;
		if(top > WORLD_HEIGHT - 1) top = WORLD_HEIGHT - 1;
		int x0 = dif_x - p->radius - 1;
		int x1 = dif_x + p->radius + 1;
		int z0 = dif_z - p->radius - 1;
		int z1 = dif_z + p->radius + 1;
		if(x0 < 0) x0 = 0;
		if(z0 < 0) z0 = 0;
		if(x1 >= CHUNK_SIZE) x1 = CHUNK_SIZE - 1;
		if(z1 >= CHUNK_SIZE) z1 = CHUNK_SIZE - 1;
		float sq_rad = (float)(p->radius * p->radius);
		float y_scale = 1.35f;
		for(int z = z0; z <= z1; z++) {
			for(int x = x0; x <= x1; x++) {
				for(int y = bottom; y <= top; y++) {
					float dy = ((float)(dif_y - y)) * y_scale;
					float sq_dist = (float)((dif_x - x) * (dif_x - x)
						+ (dif_z - z) * (dif_z - z)) + dy * dy;
					float edge_noise = gen_rand01_from_hash(
						gen_hash3i(base_x + x, y, base_z + z, t->points[0].x ^ 0xCA7E5U));
					float carve_rad = sq_rad * (0.82f + edge_noise * 0.22f);
					if(sq_dist <= carve_rad) {
						uint8_t b = gen_get_block(sc, x, y, z);
						if(b == BLOCK_STONE || b == BLOCK_DIRT || b == BLOCK_GRASS
						   || b == BLOCK_SAND || b == BLOCK_SANDSTONE
						   || b == BLOCK_GRAVEL) {
							gen_set_block(sc, x, y, z, BLOCK_AIR);
						}
					} else if(sq_dist <= sq_rad * 1.55f) {
						if(gen_get_block(sc, x, y, z) == BLOCK_SAND) {
							gen_set_block(sc, x, y, z, BLOCK_SANDSTONE);
						}
					}
				}
			}
		}
	}
}

static void gen_cave_generate_segments_and_process(
	struct server_chunk* sc, uint32_t seed, int cave_size, int chunk_x, int chunk_z,
	int ox, int oy, int oz, int segments, int recursion_left, int* tunnel_budget) {
	if((sc == NULL) || (segments <= 0) || (recursion_left <= 0)
	   || (tunnel_budget == NULL) || (*tunnel_budget <= 0))
		return;
	int double_size = cave_size * 2;
	int radius = gen_cave_radius(seed, ox + oy, oy + oz, oz + ox);
	for(int i = segments - 1; i >= 0; --i) {
		if(*tunnel_budget <= 0)
			return;
		int end_x = ox + (gen_mod_positive(gen_noise_int3d(seed, ox, oy, oz + 11 * segments) / 7, double_size) - cave_size) / 2;
		int end_y = oy + (gen_mod_positive(gen_noise_int3d(seed, oy, 13 * segments, oz + ox) / 7, double_size) - cave_size) / 4;
		int end_z = oz + (gen_mod_positive(gen_noise_int3d(seed, oz + 17 * segments, ox, oy) / 7, double_size) - cave_size) / 2;
		end_y = gen_clamp_int(end_y, 1, WORLD_HEIGHT - 2);
		int end_r = gen_cave_radius(seed, ox + 7 * i, oy + 11 * i, oz + ox);

		struct gen_cave_tunnel tunnel;
		gen_cave_init_tunnel(&tunnel, ox, oy, oz, radius, end_x, end_y, end_z, end_r, seed);
		(*tunnel_budget)--;
		gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
		gen_cave_generate_segments_and_process(
			sc, seed, cave_size, chunk_x, chunk_z, end_x, end_y, end_z, i, recursion_left - 1, tunnel_budget);
		ox = end_x; oy = end_y; oz = end_z; radius = end_r;
	}
}

static void gen_carve_worm_nest_caves(struct server_chunk* sc, uint32_t seed,
									  int32_t world_x0, int32_t world_z0,
									  const struct server_world_cuberite_config* cfg) {
	(void)cfg;
	if(sc == NULL)
		return;
	gen_carve_chunk_linked_caves(sc, seed, gen_floor_div(world_x0, CHUNK_SIZE),
								 gen_floor_div(world_z0, CHUNK_SIZE));
}

static bool gen_can_carve_density_block(uint8_t block) {
	return block == BLOCK_STONE || block == BLOCK_DIRT || block == BLOCK_GRASS
		|| block == BLOCK_SAND || block == BLOCK_SANDSTONE || block == BLOCK_GRAVEL
		|| gen_is_ore_block(block);
}

static void gen_set_nibble(uint8_t* data, size_t idx, uint8_t value);

static void gen_carve_density_block(struct server_chunk* sc, int lx, int y,
									int lz, int sea_level) {
	(void)sea_level;
	uint8_t block = gen_get_block(sc, lx, y, lz);
	if(!gen_can_carve_density_block(block))
		return;
	/* Schluchten/Höhlen immer AIR — kein Wasser am Grund (User-Wunsch) */
	gen_set_block(sc, lx, y, lz, BLOCK_AIR);
}

static void gen_drain_density_block(struct server_chunk* sc, int lx, int y,
										int lz) {
	uint8_t block = gen_get_block(sc, lx, y, lz);
	if(block == BLOCK_WATER_STILL || block == BLOCK_WATER_FLOW)
		gen_set_block(sc, lx, y, lz, BLOCK_AIR);
}

/* Metadaten von Luft sind für den Renderer bedeutungslos.  Der Marker hält
 * jedoch die Wasser-Physik aus generierten Schluchten heraus. */
static void gen_mark_density_canyon_cell(struct server_chunk* sc, int lx, int y,
											 int lz) {
	if(gen_inside_chunk(lx, y, lz)
	   && gen_get_block(sc, lx, y, lz) == BLOCK_AIR)
		gen_set_nibble(sc->metadata, S_CHUNK_IDX(lx, y, lz), 0xF);
}

static void gen_carve_air_sphere(struct server_chunk* sc, int chunk_x, int chunk_z,
								 int wx, int y, int wz, int radius) {
	if(sc == NULL || radius <= 0)
		return;
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int lx0 = wx - base_x;
	int lz0 = wz - base_z;
	for(int dz = -radius; dz <= radius; dz++) {
		for(int dx = -radius; dx <= radius; dx++) {
			for(int dy = -radius; dy <= radius; dy++) {
				float vy = (float)dy * 1.25f;
				if((float)(dx * dx + dz * dz) + vy * vy
				   > (float)(radius * radius) + 0.5f)
					continue;
				int lx = lx0 + dx;
				int lz = lz0 + dz;
				int ly = y + dy;
				if(lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE
				   || ly < 1 || ly >= WORLD_HEIGHT)
					continue;
				uint8_t b = gen_get_block(sc, lx, ly, lz);
				if(b != BLOCK_BEDROCK)
					gen_set_block(sc, lx, ly, lz, BLOCK_AIR);
			}
		}
	}
}

static void gen_carve_density_canyon_bottom_caves(struct server_chunk* sc,
												  uint32_t seed, int chunk_x,
												  int chunk_z, int32_t world_x0,
												  int32_t world_z0, float dx,
												  float dz, float cx, float cz,
												  float width, float center_y,
												  float height) {
	if(sc == NULL)
		return;
	int openings = 3 + (int)(gen_hash3i(chunk_x, 409, chunk_z,
										seed ^ 0xB0770C4U) % 4U);
	float ravine_len = 20.0f;
	int bottom_y = (int)(center_y - height * 0.55f);
	bottom_y = gen_clamp_int(bottom_y, 9, WORLD_HEIGHT - 14);
	int hub_x = 0;
	int hub_y = 0;
	int hub_z = 0;
	gen_chunk_cave_compute_hub(seed, chunk_x, chunk_z, &hub_x, &hub_y, &hub_z,
							   NULL);

	for(int i = 0; i < openings; i++) {
		uint32_t h = gen_hash3i(chunk_x, 431 + i * 37, chunk_z,
								seed ^ 0xB0770C5U);
		float along = (((float)(h & 0xFFFFU) / 65535.0f) - 0.5f) * ravine_len;
		int side = (h & 0x10000U) ? 1 : -1;
		float wall = width * (0.72f + ((float)((h >> 17) & 255U) / 255.0f) * 0.30f);
		int sy = gen_clamp_int(bottom_y + (int)((h >> 25) % 5U) - 2,
							   7, WORLD_HEIGHT - 12);

		int start_x = world_x0 + (int)(cx + dx * along + dz * wall * (float)side);
		int start_z = world_z0 + (int)(cz + dz * along - dx * wall * (float)side);
		int end_x = start_x + (int)(dz * (float)side * (8 + (int)((h >> 8) % 9U)));
		int end_z = start_z - (int)(dx * (float)side * (8 + (int)((h >> 12) % 9U)));
		int end_y = gen_clamp_int(sy - 1 - (int)((h >> 20) % 5U),
								  6, WORLD_HEIGHT - 12);

		int mouth_r = 2 + (int)((h >> 4) % 2U);
		int chamber_r = ((h >> 23) & 1U) ? 5 : 3;
		gen_carve_air_sphere(sc, chunk_x, chunk_z, start_x, sy, start_z, mouth_r);

		struct gen_cave_tunnel tunnel;
		gen_cave_init_tunnel(&tunnel, start_x, sy, start_z, mouth_r,
							 end_x, end_y, end_z, 2,
							 seed ^ (uint32_t)(0xB077A11U + i * 97U));
		gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
		gen_carve_air_sphere(sc, chunk_x, chunk_z, end_x, end_y, end_z,
							 chamber_r);

		if(i < 2) {
			int link_y = gen_clamp_int((end_y + hub_y) / 2, 6, WORLD_HEIGHT - 12);
			gen_cave_init_tunnel(&tunnel, end_x, end_y, end_z, 2,
								 hub_x, link_y, hub_z, 2,
								 seed ^ (uint32_t)(0xB077D44U + i * 103U));
			gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
			gen_carve_air_sphere(sc, chunk_x, chunk_z, hub_x, link_y, hub_z, 3);
		}

		int branches = ((h >> 27) & 1U) ? 2 : 1;
		for(int b = 0; b < branches; b++) {
			uint32_t bh = gen_hash3i(chunk_x, 503 + i * 43 + b * 17,
									 chunk_z, seed ^ 0xB077B22U);
			float a = ((float)(bh % 628U) / 100.0f);
			int bx = end_x + (int)(cosf(a) * (10.0f + (float)((bh >> 8) % 9U)));
			int bz = end_z + (int)(sinf(a) * (10.0f + (float)((bh >> 12) % 9U)));
			int by = gen_clamp_int(end_y + (int)((bh >> 18) % 7U) - 3,
								   6, WORLD_HEIGHT - 12);
			gen_cave_init_tunnel(&tunnel, end_x, end_y, end_z, 2,
								 bx, by, bz, 1 + (int)((bh >> 24) % 2U),
								 seed ^ (uint32_t)(0xB077C33U + i * 101U + b * 19U));
			gen_cave_process_chunk(&tunnel, sc, chunk_x, chunk_z);
		}
	}
}

static void gen_carve_density_caves(struct server_chunk* sc, uint32_t seed,
									int32_t world_x0, int32_t world_z0,
									const int* surface_map, int sea_level) {
	if(sc == NULL || surface_map == NULL)
		return;

	for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
		for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
			int surface = surface_map[lx + lz * CHUNK_SIZE];
			int max_y = surface - 7;
			if(max_y > 56)
				max_y = 56;
			bool high_or_steep = surface >= sea_level + 18;
			if(!high_or_steep) {
				const int nx[4] = {1, -1, 0, 0};
				const int nz[4] = {0, 0, 1, -1};
				for(int i = 0; i < 4; i++) {
					int nh = surface_map[(lx + nx[i]) + (lz + nz[i]) * CHUNK_SIZE];
					if(abs(nh - surface) >= 7) {
						high_or_steep = true;
						break;
					}
				}
			}
			if(!high_or_steep && max_y > surface - 18)
				max_y = surface - 18;
			if(max_y < 14)
				continue;

			int wx = world_x0 + lx;
			int wz = world_z0 + lz;
			float cave_region = gen_fbm2d((float)wx * 0.010f,
										  (float)wz * 0.010f,
										  seed ^ 0xCA6E2111U, 2, 2.0f, 0.5f);
			float density_boost = gen_clampf((cave_region + 0.45f) * 0.78f,
																		 0.0f, 0.56f);
			if(cave_region < -0.78f)
				continue;
			/* Dieser Layer-Wert hängt nur von x/z ab.  Er wurde vorher für
			 * jede einzelne y-Stufe erneut berechnet. */
			float layer = gen_fbm2d((float)wx * 0.017f, (float)wz * 0.017f,
									seed ^ 0x1A7ECA6EU, 2, 2.0f, 0.5f);
			float center_y = 22.0f + layer * 12.0f;
			float thickness = 0.135f + density_boost * 0.15f;

			for(int y = 8; y <= max_y; y += 2) {
				float depth = (float)(surface - y);
				float surface_fade = gen_clampf((depth - 6.0f) / 10.0f, 0.0f, 1.0f);
				float bottom_fade = gen_clampf(((float)y - 6.0f) / 10.0f, 0.0f, 1.0f);
				if(surface_fade * bottom_fade <= 0.15f)
					continue;

				float cheese = gen_fbm3d((float)wx * 0.034f, (float)y * 0.052f,
										 (float)wz * 0.034f,
										 seed ^ 0xC4EEC0DEU, 3, 2.0f, 0.52f);
				float roughness = gen_fbm3d((float)wx * 0.090f, (float)y * 0.110f,
											(float)wz * 0.090f,
											seed ^ 0x5A1AD00DU, 2, 2.0f, 0.45f);
				float cheese_threshold = 0.43f - density_boost
					+ fabsf(roughness) * 0.06f;
				bool carve_cheese = cheese > cheese_threshold;

				float tunnel_a = fabsf(gen_fbm3d((float)wx * 0.020f,
												 (float)y * 0.035f,
												 (float)wz * 0.020f,
												 seed ^ 0x5A6E7711U, 2, 2.0f, 0.5f));
				float tunnel_b = fabsf(gen_fbm3d((float)wx * 0.024f,
												 (float)y * 0.030f,
												 (float)wz * 0.024f,
												 seed ^ 0xB00D1E5U, 2, 2.0f, 0.5f));
				float noodle = gen_fbm3d((float)wx * 0.060f, (float)y * 0.065f,
										 (float)wz * 0.060f,
										 seed ^ 0xD00D1E5U, 1, 2.0f, 0.5f);
				bool carve_spaghetti = tunnel_a < thickness
					&& tunnel_b < thickness * 1.35f
					&& noodle > -0.48f;

				bool carve_layer = fabsf((float)y - center_y) < 3.5f
					&& cheese > 0.16f
					&& tunnel_a < 0.34f;

				if(carve_cheese || carve_spaghetti || carve_layer) {
					gen_carve_density_block(sc, lx, y, lz, sea_level);
					if(y + 1 <= max_y)
						gen_carve_density_block(sc, lx, y + 1, lz, sea_level);
				}
			}
		}
	}
}

static void gen_apply_density_canyon(struct server_chunk* sc, uint32_t seed,
									 int32_t world_x0, int32_t world_z0,
									 const int* surface_map, int sea_level,
									 bool drain_only) {
	if(sc == NULL || surface_map == NULL)
		return;
	int chunk_x = gen_floor_div(world_x0, CHUNK_SIZE);
	int chunk_z = gen_floor_div(world_z0, CHUNK_SIZE);

	/* Seeding: world_seed XOR chunk-Hash */
	uint32_t canyon_seed = (uint32_t)((int64_t)seed * 6364136223846793005LL
	                       + (int64_t)chunk_x * 1664525L
	                       + (int64_t)chunk_z * 22695477L);

	if((gen_hash3i(chunk_x, 331, chunk_z, canyon_seed ^ 0xCA110A11U) % 23U) != 0U)
		return;

	float angle = gen_rand01_from_hash(
		gen_hash3i(chunk_x, 337, chunk_z, canyon_seed ^ 0xCA110A12U)) * 6.2831853f;
	float dx = cosf(angle);
	float dz = sinf(angle);
	float cx = 7.5f + (gen_rand01_from_hash(
		gen_hash3i(chunk_x, 341, chunk_z, canyon_seed ^ 0xCA110A13U)) - 0.5f) * 8.0f;
	float cz = 7.5f + (gen_rand01_from_hash(
		gen_hash3i(chunk_x, 347, chunk_z, canyon_seed ^ 0xCA110A14U)) - 0.5f) * 8.0f;
	/* Breiter (4..9) und höher (tiefe Schlucht) als zuvor */
	float width = 4.0f + gen_rand01_from_hash(
		gen_hash3i(chunk_x, 349, chunk_z, canyon_seed ^ 0xCA110A15U)) * 5.0f;
	float center_y = 22.0f + gen_rand01_from_hash(
		gen_hash3i(chunk_x, 353, chunk_z, canyon_seed ^ 0xCA110A16U)) * 16.0f;
	float height = 11.0f + gen_rand01_from_hash(
		gen_hash3i(chunk_x, 359, chunk_z, canyon_seed ^ 0xCA110A17U)) * 10.0f;

	for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
		for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
			float rx = (float)lx - cx;
			float rz = (float)lz - cz;
			float across = fabsf(rx * dz - rz * dx);
			if(across > width + 1.0f)
				continue;
			int surface = surface_map[lx + lz * CHUNK_SIZE];
			/* -6: Schlucht darf oben offen sein, aber nicht die ganze Erdschicht
			 * als schmalen Kanal durchschneiden */
			int max_y = surface - 6;
			if(max_y > WORLD_HEIGHT - 2)
				max_y = WORLD_HEIGHT - 2;
			float horizontal = across / width;
			for(int y = 8; y <= max_y; y++) {
				float vertical = ((float)y - center_y) / height;
				float rough = gen_fbm3d((float)(world_x0 + lx) * 0.070f,
										(float)y * 0.060f,
										(float)(world_z0 + lz) * 0.070f,
										canyon_seed ^ 0xCA110A18U, 1, 2.0f, 0.5f);
				float shape = horizontal * horizontal + vertical * vertical * 0.58f
					+ rough * 0.10f;
				if(shape < 1.0f) {
					if(drain_only)
						gen_drain_density_block(sc, lx, y, lz);
					else
						gen_carve_density_block(sc, lx, y, lz, sea_level);
					gen_mark_density_canyon_cell(sc, lx, y, lz);
				}
			}
		}
	}

	if(!drain_only) {
		gen_carve_density_canyon_bottom_caves(
			sc, canyon_seed, chunk_x, chunk_z, world_x0, world_z0,
			dx, dz, cx, cz, width, center_y, height);
	}
}

static void gen_carve_density_canyon(struct server_chunk* sc, uint32_t seed,
									 int32_t world_x0, int32_t world_z0,
									 const int* surface_map, int sea_level) {
	gen_apply_density_canyon(sc, seed, world_x0, world_z0, surface_map,
							 sea_level, false);
}

static void gen_drain_density_canyon(struct server_chunk* sc, uint32_t seed,
									 int32_t world_x0, int32_t world_z0,
									 const int* surface_map, int sea_level) {
	gen_apply_density_canyon(sc, seed, world_x0, world_z0, surface_map,
							 sea_level, true);
}

struct gen_chunk_ravine_connector {
	int x;
	int z;
	int width;
	int top;
	int bottom;
	uint32_t seed;
};

static int gen_noise_int2d(uint32_t seed, int x, int z) {
	return (int)gen_hash3i(x, 0, z, seed);
}

static float gen_noise_range2d(uint32_t seed, int x, int z, float minv, float maxv) {
	float t = gen_rand01_from_hash(gen_hash3i(x, 0, z, seed));
	return minv + (maxv - minv) * t;
}

static uint32_t gen_chunk_ravine_border_hash(uint32_t seed, int chunk_x, int chunk_z,
											 enum gen_chunk_cave_side side, int salt) {
	int key_x = chunk_x;
	int key_z = chunk_z;
	int axis = 0;
	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		key_x = chunk_x - 1;
		axis = 0;
		break;
	case GEN_CHUNK_CAVE_EAST:
		key_x = chunk_x;
		axis = 0;
		break;
	case GEN_CHUNK_CAVE_NORTH:
		key_z = chunk_z - 1;
		axis = 1;
		break;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		key_z = chunk_z;
		axis = 1;
		break;
	}
	return gen_hash3i(key_x, axis * 12289 + salt, key_z, seed ^ 0x7711AA44U);
}

static int gen_chunk_ravine_border_count(uint32_t seed, int chunk_x, int chunk_z,
										 enum gen_chunk_cave_side side) {
	uint32_t h = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 0);
	return ((h % 100U) < 1U) ? 1 : 0;
}

static bool gen_chunk_ravine_make_connector(struct gen_chunk_ravine_connector* out,
											uint32_t seed, int chunk_x, int chunk_z,
											enum gen_chunk_cave_side side, int index,
											const struct server_world_cuberite_config* cfg) {
	if(out == NULL || cfg == NULL)
		return false;
	if(index < 0 || index >= gen_chunk_ravine_border_count(seed, chunk_x, chunk_z, side))
		return false;

	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	uint32_t pos_h = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 19 + index * 13);
	uint32_t width_h = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 43 + index * 17);
	uint32_t top_h = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 71 + index * 29);
	uint32_t bottom_h = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 97 + index * 31);
	int edge_offset = 3 + (int)(pos_h % (unsigned int)(CHUNK_SIZE - 6));

	int min_width = (int)floorf(cfg->rough_ravines_min_center_width);
	int max_width = (int)ceilf(cfg->rough_ravines_max_center_width);
	if(min_width < 2)
		min_width = 2;
	if(max_width < min_width)
		max_width = min_width;
	if(max_width > 3)
		max_width = 3;

	out->seed = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 131 + index * 23);
	out->width = min_width + (int)(width_h % (unsigned int)(max_width - min_width + 1));
	out->top = 38 + (int)(top_h % 10U);
	out->bottom = 18 + (int)(bottom_h % 8U);
	if(out->top < out->bottom + 8)
		out->top = out->bottom + 8;
	if(out->top > WORLD_HEIGHT - 2)
		out->top = WORLD_HEIGHT - 2;

	switch(side) {
	case GEN_CHUNK_CAVE_WEST:
		out->x = base_x;
		out->z = base_z + edge_offset;
		break;
	case GEN_CHUNK_CAVE_EAST:
		out->x = base_x + CHUNK_SIZE - 1;
		out->z = base_z + edge_offset;
		break;
	case GEN_CHUNK_CAVE_NORTH:
		out->x = base_x + edge_offset;
		out->z = base_z;
		break;
	case GEN_CHUNK_CAVE_SOUTH:
	default:
		out->x = base_x + edge_offset;
		out->z = base_z + CHUNK_SIZE - 1;
		break;
	}
	return true;
}

static void gen_carve_ravine_segment(struct server_chunk* sc, int chunk_x, int chunk_z,
									 float x1, float z1, float x2, float z2,
									 int width1, int width2,
									 int bottom1, int bottom2,
									 int top1, int top2,
									 uint32_t seed) {
	if(sc == NULL)
		return;
	float base_x = (float)(chunk_x * CHUNK_SIZE);
	float base_z = (float)(chunk_z * CHUNK_SIZE);
	float dx = x2 - x1;
	float dz = z2 - z1;
	int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dz)) * 2.0f);
	if(steps < 1)
		steps = 1;

	for(int step = 0; step <= steps; step++) {
		float t = (float)step / (float)steps;
		float cx = x1 + dx * t;
		float cz = z1 + dz * t;
		float jitter_x = ((float)((int)(gen_hash3i((int)cx, step, (int)cz, seed) % 5U)) - 2.0f) * 0.18f;
		float jitter_z = ((float)((int)(gen_hash3i((int)cz, step + 41, (int)cx, seed) % 5U)) - 2.0f) * 0.18f;
		cx += jitter_x;
		cz += jitter_z;

		float width = (float)width1 + ((float)(width2 - width1) * t) + 0.15f;
		int top = bottom1 + 1;
		top = top1 + (int)((float)(top2 - top1) * t);
		int bottom = bottom1 + (int)((float)(bottom2 - bottom1) * t);
		if(bottom < 1)
			bottom = 1;
		if(top > WORLD_HEIGHT - 1)
			top = WORLD_HEIGHT - 1;
		if(top <= bottom + 2)
			top = bottom + 3;

		float local_x = cx - base_x;
		float local_z = cz - base_z;
		int x0 = (int)floorf(local_x - width - 1.0f);
		int x1i = (int)ceilf(local_x + width + 1.0f);
		int z0 = (int)floorf(local_z - width - 1.0f);
		int z1i = (int)ceilf(local_z + width + 1.0f);
		if(x0 < 0)
			x0 = 0;
		if(z0 < 0)
			z0 = 0;
		if(x1i >= CHUNK_SIZE)
			x1i = CHUNK_SIZE - 1;
		if(z1i >= CHUNK_SIZE)
			z1i = CHUNK_SIZE - 1;

		for(int lx = x0; lx <= x1i; lx++) {
			for(int lz = z0; lz <= z1i; lz++) {
				float off_x = ((float)lx + 0.5f) - local_x;
				float off_z = ((float)lz + 0.5f) - local_z;
				float dist_sq = off_x * off_x + off_z * off_z;
				float max_dist_sq = width * width;
				if(dist_sq > max_dist_sq)
					continue;

				for(int y = bottom; y <= top; y++) {
					float rel = (float)(y - bottom) / (float)(top - bottom);
					float level_width = width * (0.75f - 0.40f * rel);
					if(level_width < 0.85f)
						level_width = 0.85f;
					if(dist_sq > level_width * level_width)
						continue;
					uint8_t b = gen_get_block(sc, lx, y, lz);
					if(b == BLOCK_STONE || b == BLOCK_DIRT || b == BLOCK_GRASS
					   || b == BLOCK_SAND || b == BLOCK_SANDSTONE
					   || b == BLOCK_GRAVEL) {
						gen_set_block(sc, lx, y, lz, BLOCK_AIR);
					}
				}
			}
		}
	}
}

static void gen_carve_ravine_pass(struct server_chunk* sc, uint32_t seed,
								  int32_t world_x0, int32_t world_z0,
								  const struct gen_cuberite_runtime_args* args,
								  const struct server_world_cuberite_config* cfg) {
	if(sc == NULL || cfg == NULL)
		return;
	int chunk_x = gen_floor_div(world_x0, CHUNK_SIZE);
	int chunk_z = gen_floor_div(world_z0, CHUNK_SIZE);
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int center_surface = gen_chunk_actual_surface_height(sc, CHUNK_SIZE / 2, CHUNK_SIZE / 2);
	if((args != NULL) && (center_surface <= args->sea_level + 2))
		return;
	struct gen_chunk_ravine_connector connectors[4];
	int connector_count = 0;

	for(int side = GEN_CHUNK_CAVE_WEST; side <= GEN_CHUNK_CAVE_SOUTH; side++) {
		if(gen_chunk_ravine_border_count(seed, chunk_x, chunk_z, (enum gen_chunk_cave_side)side) <= 0)
			continue;
		if(gen_chunk_ravine_make_connector(&connectors[connector_count], seed, chunk_x, chunk_z,
										   (enum gen_chunk_cave_side)side, 0, cfg)) {
			connector_count++;
		}
	}

	if(connector_count == 0)
		return;

	for(int i = 0; i < connector_count; i++) {
		float end_x = (float)connectors[i].x;
		float end_z = (float)connectors[i].z;
		switch(gen_hash3i(connectors[i].x, 1703 + i, connectors[i].z, seed) % 4U) {
		case GEN_CHUNK_CAVE_WEST:
			end_x = (float)base_x;
			end_z = (float)(base_z + 2
					   + gen_mod_positive(connectors[i].z + i * 5, CHUNK_SIZE - 4));
			break;
		case GEN_CHUNK_CAVE_EAST:
			end_x = (float)(base_x + CHUNK_SIZE - 1);
			end_z = (float)(base_z + 2
					   + gen_mod_positive(connectors[i].z + i * 5, CHUNK_SIZE - 4));
			break;
		case GEN_CHUNK_CAVE_NORTH:
			end_x = (float)(base_x + 2
					   + gen_mod_positive(connectors[i].x + i * 5, CHUNK_SIZE - 4));
			end_z = (float)base_z;
			break;
		case GEN_CHUNK_CAVE_SOUTH:
		default:
			end_x = (float)(base_x + 2
					   + gen_mod_positive(connectors[i].x + i * 5, CHUNK_SIZE - 4));
			end_z = (float)(base_z + CHUNK_SIZE - 1);
			break;
		}

		if(fabsf(end_x - (float)connectors[i].x) + fabsf(end_z - (float)connectors[i].z) < 10.0f) {
			if(end_x < (float)(base_x + CHUNK_SIZE / 2))
				end_x = (float)(base_x + CHUNK_SIZE - 1);
			else
				end_x = (float)base_x;
		}

		int end_top = connectors[i].top - 2 + (int)(gen_hash3i(i, 1759, connectors[i].x, seed) % 5U);
		int end_bottom = connectors[i].bottom + (int)(gen_hash3i(i, 1783, connectors[i].z, seed) % 4U);
		if(end_top < end_bottom + 6)
			end_top = end_bottom + 6;
		gen_carve_ravine_segment(sc, chunk_x, chunk_z,
								 (float)connectors[i].x, (float)connectors[i].z,
								 end_x, end_z,
								 connectors[i].width, connectors[i].width,
								 connectors[i].bottom, end_bottom,
								 connectors[i].top, end_top,
								 connectors[i].seed);
	}
}

static void gen_set_nibble(uint8_t* data, size_t idx, uint8_t value) {
	nibble_write(data, idx, value & 0xF);
}

static uint8_t gen_get_nibble(const uint8_t* data, size_t idx) {
	return nibble_read(data, idx);
}

static bool gen_inside_chunk(int x, int y, int z) {
	return x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE && y >= 0
		&& y < WORLD_HEIGHT;
}

static uint8_t gen_get_block(const struct server_chunk* sc, int x, int y, int z) {
	if(!gen_inside_chunk(x, y, z))
		return BLOCK_AIR;
	return sc->ids[S_CHUNK_IDX(x, y, z)];
}

static void gen_set_block(struct server_chunk* sc, int x, int y, int z,
						  uint8_t type) {
	if(!gen_inside_chunk(x, y, z))
		return;
	size_t idx = S_CHUNK_IDX(x, y, z);
	sc->ids[idx] = type;
}

static void gen_set_block_with_meta(struct server_chunk* sc, int x, int y, int z,
									uint8_t type, uint8_t meta) {
	if(!gen_inside_chunk(x, y, z))
		return;
	size_t idx = S_CHUNK_IDX(x, y, z);
	sc->ids[idx] = type;
	gen_set_nibble(sc->metadata, idx, meta & 0xF);
}

static bool gen_is_water(const struct server_chunk* sc, int x, int y, int z) {
	uint8_t b = gen_get_block(sc, x, y, z);
	return b == BLOCK_WATER_STILL || b == BLOCK_WATER_FLOW;
}

static bool gen_is_air(const struct server_chunk* sc, int x, int y, int z) {
	return gen_get_block(sc, x, y, z) == BLOCK_AIR;
}

static bool gen_dungeon_has_earth_cover(const struct server_chunk* sc,
										int x0, int x1, int y0, int y1,
										int z0, int z1) {
	for(int x = x0; x <= x1; x++) {
		for(int z = z0; z <= z1; z++) {
			for(int y = y0; y <= y1; y++) {
				if(!gen_inside_chunk(x, y, z))
					return false;
				if(gen_is_water(sc, x, y, z))
					return false;
			}

			for(int y = y1 + 1; y < WORLD_HEIGHT; y++) {
				if(gen_is_water(sc, x, y, z))
					return false;
			}

			for(int y = y1 + 1; y <= y1 + 4; y++) {
				if(!gen_inside_chunk(x, y, z))
					return false;
				if(!gen_is_opaque(gen_get_block(sc, x, y, z)))
					return false;
			}
		}
	}
	return true;
}

static bool gen_dungeon_connected_to_cave(const struct server_chunk* sc,
										  int x0, int x1, int y0, int y1,
										  int z0, int z1) {
	for(int y = y0 + 1; y <= y1 - 1; y++) {
		for(int x = x0; x <= x1; x++) {
			if(gen_inside_chunk(x, y, z0 - 1)
			   && gen_get_block(sc, x, y, z0 - 1) == BLOCK_AIR)
				return true;
			if(gen_inside_chunk(x, y, z1 + 1)
			   && gen_get_block(sc, x, y, z1 + 1) == BLOCK_AIR)
				return true;
		}
		for(int z = z0; z <= z1; z++) {
			if(gen_inside_chunk(x0 - 1, y, z)
			   && gen_get_block(sc, x0 - 1, y, z) == BLOCK_AIR)
				return true;
			if(gen_inside_chunk(x1 + 1, y, z)
			   && gen_get_block(sc, x1 + 1, y, z) == BLOCK_AIR)
				return true;
		}
	}
	return false;
}

struct gen_coord2i { int dx, dz; };
static const struct gen_coord2i GEN_BIG_O1[] = {{-1,0},{1,0},{0,-1},{0,1}};
static const struct gen_coord2i GEN_BIG_O2[] = {
	{-2,0},{-1,-1},{-1,0},{-1,1},{0,-2},{0,-1},{0,0},{0,1},{0,2},{1,-1},{1,0},{1,1},{2,0}
};
static const struct gen_coord2i GEN_BIG_O3[] = {
	{-2,-1},{-2,0},{-2,1},{-1,-2},{-1,-1},{-1,0},{-1,1},{-1,2},{0,-2},{0,-1},{0,0},
	{0,1},{0,2},{1,-2},{1,-1},{1,0},{1,1},{1,2},{2,-1},{2,0},{2,1}
};

static void gen_place_leaf_pattern(struct server_chunk* sc, int cx, int y, int cz,
								   const struct gen_coord2i* coords, int count,
								   uint8_t leaves_meta) {
	for(int i = 0; i < count; i++) {
		int x = cx + coords[i].dx;
		int z = cz + coords[i].dz;
		if(gen_get_block(sc, x, y, z) == BLOCK_AIR)
			gen_set_block_with_meta(sc, x, y, z, BLOCK_LEAVES, leaves_meta);
	}
}

static void gen_place_log(struct server_chunk* sc, int x, int y, int z, uint8_t meta) {
	gen_set_block_with_meta(sc, x, y, z, BLOCK_LOG, meta);
}

static bool gen_tree_can_replace(uint8_t b) {
	return (b == BLOCK_AIR || b == BLOCK_LEAVES || b == BLOCK_VINE
		|| b == BLOCK_WATER_STILL || b == BLOCK_WATER_FLOW);
}

static bool gen_tree_space_clear(const struct server_chunk* sc, int x, int y, int z, int radius, int height) {
	for(int yy = y; yy <= y + height; yy++) {
		for(int dz = -radius; dz <= radius; dz++) {
			for(int dx = -radius; dx <= radius; dx++) {
				uint8_t b = gen_get_block(sc, x + dx, yy, z + dz);
				if(!gen_tree_can_replace(b))
					return false;
			}
		}
	}
	return true;
}

static bool gen_tree_ground_ok(const struct server_chunk* sc, int x, int y, int z) {
	if(!gen_inside_chunk(x, y - 1, z) || !gen_inside_chunk(x, y, z))
		return false;
	uint8_t ground = gen_get_block(sc, x, y - 1, z);
	return (ground == BLOCK_GRASS || ground == BLOCK_DIRT)
		&& gen_tree_can_replace(gen_get_block(sc, x, y, z));
}

static bool gen_tree_2x2_ground_ok(const struct server_chunk* sc, int x, int y, int z) {
	for(int dz = 0; dz < 2; dz++) {
		for(int dx = 0; dx < 2; dx++) {
			if(!gen_tree_ground_ok(sc, x + dx, y, z + dz))
				return false;
		}
	}
	return true;
}

static void gen_place_leaf_square(struct server_chunk* sc, int cx, int y, int cz,
								  int radius, uint8_t leaves_meta,
								  uint32_t corner_mask) {
	for(int dz = -radius; dz <= radius; dz++) {
		for(int dx = -radius; dx <= radius; dx++) {
			if((abs(dx) == radius) && (abs(dz) == radius)) {
				uint32_t bit = (uint32_t)((dx + radius) * 4 + (dz + radius));
				if(((corner_mask >> (bit & 31)) & 1U) == 0U)
					continue;
			}
			if(gen_get_block(sc, cx + dx, y, cz + dz) == BLOCK_AIR)
				gen_set_block_with_meta(sc, cx + dx, y, cz + dz, BLOCK_LEAVES, leaves_meta);
		}
	}
}

static void gen_place_hanging_vines(struct server_chunk* sc, int x, int y, int z,
									uint32_t seed, int wx, int wz) {
	static const int dirs[4][3] = {{-1, 0, 8}, {1, 0, 2}, {0, -1, 1}, {0, 1, 4}};
	for(int i = 0; i < 4; i++) {
		int vx = x + dirs[i][0];
		int vz = z + dirs[i][1];
		if(gen_get_block(sc, vx, y, vz) != BLOCK_AIR)
			continue;
		uint32_t h = gen_hash3i(wx + vx, y, wz + vz, seed ^ 0x71AE5000U);
		if((h & 3U) != 0U)
			continue;
		int len = 1 + (int)((h >> 3) & 3U);
		for(int k = 0; k < len && (y - k) > 1; k++) {
			if(gen_get_block(sc, vx, y - k, vz) != BLOCK_AIR)
				break;
			gen_set_block_with_meta(sc, vx, y - k, vz, BLOCK_VINE, dirs[i][2]);
		}
	}
}

static void gen_place_small_oak_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int rnd = gen_noise_int3d(seed ^ 0xACCE5511U, x, y, z) >> 3;
	static const int heights[] = {4, 5, 5, 6};
	int trunk_h = heights[rnd & 3];
	if(!gen_tree_space_clear(sc, x, y, z, 3, trunk_h + 4))
		return;
	for(int i = 0; i < trunk_h; i++) gen_place_log(sc, x, y + i, z, 0);
	int top = y + trunk_h;
	gen_place_leaf_square(sc, x, top - 2, z, 2, 0, 0x0f0f0f0fU);
	gen_place_leaf_square(sc, x, top - 1, z, 2, 0, 0xffffffffU);
	gen_place_leaf_square(sc, x, top, z, 1, 0, 0xffffffffU);
	gen_set_block_with_meta(sc, x, top + 1, z, BLOCK_LEAVES, 0);
}

static void gen_place_fancy_oak_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int trunk_h = 5 + (abs(gen_noise_int3d(seed ^ 0x0A4F11U, x, y, z)) % 3);
	if(!gen_tree_space_clear(sc, x, y, z, 4, trunk_h + 5))
		return;
	for(int i = 0; i < trunk_h; i++) gen_place_log(sc, x, y + i, z, 0);
	int top = y + trunk_h;
	gen_place_leaf_square(sc, x, top - 2, z, 2, 0, 0xffffffffU);
	gen_place_leaf_square(sc, x, top - 1, z, 2, 0, 0x3c3c3c3cU);
	gen_place_leaf_square(sc, x, top, z, 1, 0, 0xffffffffU);
	gen_set_block_with_meta(sc, x, top + 1, z, BLOCK_LEAVES, 0);
}

static void gen_place_birch_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed, bool tall) {
	int height = tall ? (8 + (abs(gen_noise_int3d(seed ^ 0xB17C0AA1U, x, y, z)) % 3))
					  : (5 + (abs(gen_noise_int3d(seed ^ 0xB17C0021U, x, y, z)) % 3));
	int radius = tall ? 3 : 2;
	if(!gen_tree_space_clear(sc, x, y, z, radius, height + 4))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 2);
	int h = y + height;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 2);
	gen_set_block_with_meta(sc, x, h, z, BLOCK_LEAVES, 2);
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 2);
	if(tall) {
		h--;
		gen_place_leaf_square(sc, x, h, z, 2, 2, 0x3c3c3c3cU);
	}
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 2);
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 2);
}

static void gen_place_conifer_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed, bool giant) {
	int height = giant ? (9 + (abs(gen_noise_int3d(seed ^ 0x5A71CE77U, x, y, z)) % 4))
					   : (7 + (abs(gen_noise_int3d(seed ^ 0x5A71CE11U, x, y, z)) % 3));
	int clear_h = giant ? 3 : 2;
	if(!gen_tree_space_clear(sc, x, y, z, giant ? 4 : 3, height + 3))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 1);
	int leaf_layers = height - clear_h;
	for(int layer = 0; layer < leaf_layers; layer++) {
		int yy = y + clear_h + layer;
		int from_top = leaf_layers - 1 - layer;
		int radius;
		if(from_top <= 0) {
			radius = 0;
		} else if(from_top == 1) {
			radius = 1;
		} else if(from_top == 2) {
			radius = 1;
		} else if(from_top == 3) {
			radius = 2;
		} else {
			radius = ((layer & 1) == 0) ? 2 : 1;
			if(giant && from_top > 5 && (layer % 3 == 0))
				radius = 3;
		}
		if(radius == 0) {
			gen_set_block_with_meta(sc, x, yy, z, BLOCK_LEAVES, 1);
			continue;
		}
		uint32_t mask = 0x3c3c3c3cU;
		if(radius == 1)
			mask = 0xffffffffU;
		else if(radius >= 3)
			mask = 0x0f0f0f0fU;
		gen_place_leaf_square(sc, x, yy, z, radius, 1, mask);
	}
	gen_set_block_with_meta(sc, x, y + height, z, BLOCK_LEAVES, 1);
	gen_set_block_with_meta(sc, x, y + height + 1, z, BLOCK_LEAVES, 1);
}

static void gen_place_swamp_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed,
								 int world_x, int world_z) {
	int height = 4 + (abs(gen_noise_int3d(seed ^ 0x5AAA4411U, x, y, z)) % 3);
	if(!gen_tree_space_clear(sc, x, y, z, 3, height + 4))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 0);
	int top = y + height;
	gen_place_leaf_square(sc, x, top - 2, z, 2, 0, 0xffffffffU);
	gen_place_leaf_square(sc, x, top - 1, z, 2, 0, 0x3c3c3c3cU);
	gen_place_leaf_square(sc, x, top, z, 1, 0, 0xffffffffU);
	gen_set_block_with_meta(sc, x, top + 1, z, BLOCK_LEAVES, 0);
	for(int yy = top; yy >= top - 2; yy--) {
		gen_place_hanging_vines(sc, x - 2, yy, z, seed, world_x, world_z);
		gen_place_hanging_vines(sc, x + 2, yy, z, seed ^ 0x100U, world_x, world_z);
		gen_place_hanging_vines(sc, x, yy, z - 2, seed ^ 0x200U, world_x, world_z);
		gen_place_hanging_vines(sc, x, yy, z + 2, seed ^ 0x300U, world_x, world_z);
	}
}

static void gen_place_jungle_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed,
								  int world_x, int world_z) {
	int height = 8 + (abs(gen_noise_int3d(seed ^ 0x3A6A11E1U, x, y, z)) % 4);
	if(!gen_tree_space_clear(sc, x, y, z, 4, height + 4))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 3);
	int top = y + height;
	gen_place_leaf_square(sc, x, top - 2, z, 2, 3, 0xffffffffU);
	gen_place_leaf_square(sc, x, top - 1, z, 2, 3, 0x3c3c3c3cU);
	gen_place_leaf_square(sc, x, top, z, 1, 3, 0xffffffffU);
	gen_set_block_with_meta(sc, x, top + 1, z, BLOCK_LEAVES, 3);
	for(int i = 0; i < 2; i++) {
		int dir = (abs(gen_noise_int3d(seed ^ (0x3A6A1200U + i), x, y, z)) % 4);
		static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
		int dx = dirs[dir][0], dz = dirs[dir][1];
		int bx = x, bz = z, by = top - 1 - i;
		for(int k = 0; k < 2 + i; k++) {
			bx += dx;
			bz += dz;
			gen_place_log(sc, bx, by, bz, 3);
		}
		gen_place_leaf_square(sc, bx, by, bz, 1, 3, 0xffffffffU);
		gen_place_hanging_vines(sc, bx, by, bz, seed ^ (0x3A6A1300U + i), world_x, world_z);
	}
	gen_place_hanging_vines(sc, x - 2, top - 1, z, seed, world_x, world_z);
	gen_place_hanging_vines(sc, x + 2, top - 1, z, seed ^ 0x101U, world_x, world_z);
	gen_place_hanging_vines(sc, x, top - 1, z - 2, seed ^ 0x202U, world_x, world_z);
	gen_place_hanging_vines(sc, x, top - 1, z + 2, seed ^ 0x303U, world_x, world_z);
}

static void gen_place_acacia_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int height = 4 + (abs(gen_noise_int3d(seed ^ 0xACAC1A11U, x, y, z)) % 3);
	if(!gen_tree_space_clear(sc, x, y, z, 4, height + 5))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 0);
	static const int dirs[8][2] = {{-1,0},{0,-1},{-1,1},{-1,-1},{1,1},{1,-1},{1,0},{0,1}};
	int dir_idx = abs(gen_noise_int3d(seed ^ 0xACAC1A12U, x, y, z)) % 8;
	int dx = dirs[dir_idx][0], dz = dirs[dir_idx][1];
	int branch_h = abs(gen_noise_int3d(seed ^ 0xACAC1A13U, x, y, z)) % 3 + 1;
	int bx = x, by = y + height - 1, bz = z;
	for(int i = 0; i < branch_h; i++) {
		bx += dx; by += 1; bz += dz;
		gen_place_log(sc, bx, by, bz, 0);
	}
	gen_place_leaf_square(sc, bx, by, bz, 2, 0, 0x3c3c3c3cU);
	gen_place_leaf_square(sc, bx, by + 1, bz, 1, 0, 0xffffffffU);
	gen_set_block_with_meta(sc, bx, by + 1, bz, BLOCK_LEAVES, 0);
	if((seed & 1U) == 0U) {
		int dx2 = -dx;
		int dz2 = -dz;
		int bx2 = x, by2 = y + height - 2, bz2 = z;
		for(int i = 0; i < branch_h - 1; i++) {
			bx2 += dx2; by2 += 1; bz2 += dz2;
			gen_place_log(sc, bx2, by2, bz2, 0);
		}
		gen_place_leaf_square(sc, bx2, by2, bz2, 1, 0, 0xffffffffU);
	}
}

static void gen_place_dark_forest_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int height = 5 + (abs(gen_noise_int3d(seed ^ 0xDA4B0A11U, x, y, z)) % 3);
	if(!gen_tree_2x2_ground_ok(sc, x, y, z))
		return;
	if(!gen_tree_space_clear(sc, x, y, z, 4, height + 4))
		return;
	for(int dz = 0; dz < 2; dz++) {
		for(int dx = 0; dx < 2; dx++) {
			for(int i = 0; i < height; i++)
				gen_place_log(sc, x + dx, y + i, z + dz, 0);
		}
	}
	int top = y + height;
	for(int yy = top - 2; yy <= top; yy++)
		gen_place_leaf_square(sc, x, yy, z, 3, 0, 0x3c3c3c3cU);
	gen_place_leaf_square(sc, x + 1, top + 1, z + 1, 1, 0, 0xffffffffU);
}

static void gen_try_place_tree(struct server_chunk* sc, int lx, int lz, int y,
							   uint32_t seed, int world_x, int world_z,
							   int biome_id) {
	uint8_t ground = gen_get_block(sc, lx, y - 1, lz);
	if(ground != BLOCK_GRASS && ground != BLOCK_DIRT)
		return;
	if(y <= 1 || y >= WORLD_HEIGHT - 12)
		return;
	if(biome_id == dark_forest || biome_id == roofedForest) {
		gen_place_dark_forest_tree(sc, lx, y, lz, seed ^ 0xDAA60A11U);
	} else if(biome_id == jungle || biome_id == jungle_hills) {
		gen_place_jungle_tree(sc, lx, y, lz, seed ^ 0x3A6A0001U, world_x, world_z);
	} else if(biome_id == swamp) {
		gen_place_swamp_tree(sc, lx, y, lz, seed ^ 0x5AAA0001U, world_x, world_z);
	} else if(biome_id == birch_forest || biome_id == birch_forest_hills) {
		bool tall = (biome_id == birch_forest_hills)
			|| ((gen_hash3i(world_x, y, world_z, seed ^ 0xB17C0F11U) & 3U) == 0U);
		gen_place_birch_tree(sc, lx, y, lz, seed ^ 0xB17C0001U, tall);
	} else if(biome_id == taiga || biome_id == taiga_hills
			  || biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills
			  || biome_id == snowy_taiga || biome_id == snowy_taiga_hills) {
		bool giant = (biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills);
		gen_place_conifer_tree(sc, lx, y, lz, seed ^ 0x5A71CE01U, giant);
	} else if(biome_id == savanna || biome_id == savanna_plateau) {
		gen_place_acacia_tree(sc, lx, y, lz, seed ^ 0xACAC1A01U);
	} else {
		bool fancy = ((gen_hash3i(world_x, y, world_z, seed ^ 0x0A4F0001U) & 3U) == 0U)
			|| biome_id == flower_forest;
		if(fancy)
			gen_place_fancy_oak_tree(sc, lx, y, lz, seed ^ 0x0A4F0001U);
		else
			gen_place_small_oak_tree(sc, lx, y, lz, seed ^ 0xA991E001U);
	}
}

static bool gen_tree_density_rule(int biome_id, int world_x, int world_z,
								  uint32_t seed, float deco,
								  float* out_threshold) {
	int spacing = 7;
	float threshold = 0.82f;

	if(biome_id == dark_forest || biome_id == roofedForest) {
		spacing = 4;
		threshold = 0.62f;
	} else if(biome_id == jungle || biome_id == jungle_hills) {
		spacing = 5;
		threshold = 0.66f;
	} else if(biome_id == forest || biome_id == birch_forest
			  || biome_id == birch_forest_hills || biome_id == wooded_hills
			  || biome_id == flower_forest) {
		spacing = 6;
		threshold = 0.70f;
	} else if(biome_id == taiga || biome_id == taiga_hills
			  || biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills
			  || biome_id == snowy_taiga || biome_id == snowy_taiga_hills) {
		spacing = 6;
		threshold = 0.74f;
	} else if(biome_id == swamp) {
		spacing = 7;
		threshold = 0.78f;
	} else if(biome_id == savanna || biome_id == savanna_plateau) {
		spacing = 9;
		threshold = 0.88f;
	}

	float grove = gen_fbm2d((float)world_x * 0.018f, (float)world_z * 0.018f,
							seed ^ 0x70A57EEU, 2, 2.0f, 0.5f);
	threshold = gen_clampf(threshold - grove * 0.10f, 0.52f, 0.94f);
	if(out_threshold)
		*out_threshold = threshold;
	if(deco <= threshold)
		return false;

	int cell_x = gen_floor_div(world_x, spacing);
	int cell_z = gen_floor_div(world_z, spacing);
	uint32_t h = gen_hash3i(cell_x, 311, cell_z, seed ^ 0x7AEE5001U);
	int pick_x = cell_x * spacing + (int)(h % (uint32_t)spacing);
	int pick_z = cell_z * spacing + (int)((h >> 8) % (uint32_t)spacing);
	return abs(world_x - pick_x) <= 1 && abs(world_z - pick_z) <= 1;
}

static void gen_try_place_dungeon(struct server_chunk* sc, uint32_t seed,
								  int world_x0, int world_z0) {
	float chance = gen_rand01_from_hash(
		gen_hash3i(world_x0, 17, world_z0, seed ^ 0xD06E0A1FU));
	if(chance < 0.965f)
		return;

	int cx = 3 + (int)(gen_rand01_from_hash(
						   gen_hash3i(world_x0, 1, world_z0, seed ^ 0x0F11CE01U))
					   * 10.0f);
	int cz = 3 + (int)(gen_rand01_from_hash(
						   gen_hash3i(world_x0, 2, world_z0, seed ^ 0x0F11CE02U))
					   * 10.0f);
	int cy = 12 + (int)(gen_rand01_from_hash(
							gen_hash3i(world_x0, 3, world_z0, seed ^ 0x0F11CE03U))
						* 28.0f);

	for(int x = cx - 2; x <= cx + 2; x++) {
		for(int z = cz - 2; z <= cz + 2; z++) {
			for(int y = cy - 2; y <= cy + 2; y++) {
				if(!gen_inside_chunk(x, y, z))
					return;
			}
		}
	}
	if(!gen_dungeon_has_earth_cover(sc, cx - 2, cx + 2, cy - 2, cy + 2,
									cz - 2, cz + 2))
		return;
	if(!gen_dungeon_connected_to_cave(sc, cx - 2, cx + 2, cy - 2, cy + 2,
									  cz - 2, cz + 2))
		return;

	for(int x = cx - 2; x <= cx + 2; x++) {
		for(int z = cz - 2; z <= cz + 2; z++) {
			for(int y = cy - 2; y <= cy + 2; y++) {
				bool wall = (x == cx - 2 || x == cx + 2 || y == cy - 2
							 || y == cy + 2 || z == cz - 2 || z == cz + 2);
				if(wall) {
					float moss = gen_rand01_from_hash(gen_hash3i(
						world_x0 + x, y, world_z0 + z, seed ^ 0xD06E0A2FU));
					gen_set_block(sc, x, y, z,
								  (moss > 0.75f) ? BLOCK_MOSSY_COBBLE :
												   BLOCK_COBBLESTONE);
				} else {
					gen_set_block(sc, x, y, z, BLOCK_AIR);
				}
			}
		}
	}

	gen_set_block(sc, cx, cy - 1, cz, BLOCK_COBBLESTONE);
	gen_set_block(sc, cx, cy, cz, BLOCK_SPAWNER);
}

static void gen_fill_box(struct server_chunk* sc, int x0, int y0, int z0,
						 int x1, int y1, int z1, uint8_t block) {
	for(int x = x0; x <= x1; x++) {
		for(int y = y0; y <= y1; y++) {
			for(int z = z0; z <= z1; z++)
				gen_set_block(sc, x, y, z, block);
		}
	}
}

static bool gen_can_surface_structure(const struct server_chunk* sc, int cx, int cz,
									  int radius, int* out_y) {
	int min_h = WORLD_HEIGHT;
	int max_h = -1;
	int center_h = gen_chunk_actual_surface_height(sc, cx, cz);
	if(center_h < 1)
		return false;
	for(int x = cx - radius; x <= cx + radius; x++) {
		for(int z = cz - radius; z <= cz + radius; z++) {
			if(x < 1 || x >= CHUNK_SIZE - 1 || z < 1 || z >= CHUNK_SIZE - 1)
				return false;
			int h = gen_chunk_actual_surface_height(sc, x, z);
			if(h < 1)
				return false;
			if(h < min_h)
				min_h = h;
			if(h > max_h)
				max_h = h;
		}
	}
	if(max_h - min_h > 2)
		return false;
	if(out_y)
		*out_y = center_h;
	return true;
}

static void gen_flatten_surface_patch(struct server_chunk* sc, int cx, int cz,
									  int radius, int target_y,
									  uint8_t top_block, uint8_t filler_block) {
	for(int x = cx - radius; x <= cx + radius; x++) {
		for(int z = cz - radius; z <= cz + radius; z++) {
			if(x < 1 || x >= CHUNK_SIZE - 1 || z < 1 || z >= CHUNK_SIZE - 1)
				continue;
			int h = gen_chunk_actual_surface_height(sc, x, z);
			if(h < 1)
				continue;
			if(h > target_y) {
				for(int y = h; y > target_y; y--)
					gen_set_block(sc, x, y, z, BLOCK_AIR);
			} else if(h < target_y) {
				for(int y = h + 1; y <= target_y; y++) {
					uint8_t fill = (y == target_y) ? top_block : filler_block;
					gen_set_block(sc, x, y, z, fill);
				}
			}
			gen_set_block(sc, x, target_y, z, top_block);
			if(target_y > 0)
				gen_set_block(sc, x, target_y - 1, z, filler_block);
		}
	}
}

static void gen_place_desert_well(struct server_chunk* sc, int cx, int y, int cz) {
	gen_flatten_surface_patch(sc, cx, cz, 3, y, BLOCK_SAND, BLOCK_SANDSTONE);
	for(int x = cx - 2; x <= cx + 2; x++) {
		for(int z = cz - 2; z <= cz + 2; z++) {
			gen_set_block(sc, x, y, z, BLOCK_SANDSTONE);
			if(abs(x - cx) <= 1 && abs(z - cz) <= 1)
				gen_set_block(sc, x, y - 1, z, BLOCK_WATER_STILL);
		}
	}
	for(int dx = -1; dx <= 1; dx += 2) {
		for(int dz = -1; dz <= 1; dz += 2) {
			gen_fill_box(sc, cx + dx, y + 1, cz + dz, cx + dx, y + 3, cz + dz,
						 BLOCK_SANDSTONE);
		}
	}
	gen_fill_box(sc, cx - 2, y + 4, cz - 2, cx + 2, y + 4, cz + 2, BLOCK_SLAB);
}

static void gen_place_forest_boulder(struct server_chunk* sc, int cx, int y, int cz,
									 uint32_t seed) {
	for(int x = cx - 2; x <= cx + 2; x++) {
		for(int z = cz - 2; z <= cz + 2; z++) {
			for(int yy = y; yy <= y + 2; yy++) {
				float dx = (float)(x - cx) / 2.1f;
				float dy = (float)(yy - (y + 1)) / 1.6f;
				float dz = (float)(z - cz) / 2.1f;
				if(dx * dx + dy * dy + dz * dz > 1.0f)
					continue;
				uint32_t h = gen_hash3i(x, yy, z, seed ^ 0xB041D3F1U);
				gen_set_block(sc, x, yy, z,
							  ((h % 100U) < 30U) ? BLOCK_MOSSY_COBBLE :
												   BLOCK_COBBLESTONE);
			}
		}
	}
}

static void gen_place_single_piece_structure(struct server_chunk* sc, uint32_t seed,
											 int world_x0, int world_z0,
											 Generator* biome_gen) {
	if((gen_hash3i(world_x0, 1401, world_z0, seed ^ 0x51061EC3U) % 100U) >= 2U)
		return;

	int cx = 4 + (int)(gen_hash3i(world_x0, 1403, world_z0, seed ^ 0x51061EC4U) % 8U);
	int cz = 4 + (int)(gen_hash3i(world_x0, 1409, world_z0, seed ^ 0x51061EC5U) % 8U);
	int wx = world_x0 + cx;
	int wz = world_z0 + cz;
	int biome_id = gen_biome_at_safe(biome_gen, wx, wz);
	int y = 0;
	if(!gen_can_surface_structure(sc, cx, cz, 3, &y))
		return;

	if(biome_id == desert || biome_id == desert_hills || biome_id == desert_lakes) {
		gen_place_desert_well(sc, cx, y, cz);
		return;
	}

	if(biome_id == forest || biome_id == flower_forest || biome_id == birch_forest
	   || biome_id == taiga || biome_id == taiga_hills) {
		gen_place_forest_boulder(sc, cx, y, cz, seed);
	}
}

static void gen_place_mineshaft(struct server_chunk* sc, uint32_t seed,
								int world_x0, int world_z0) {
	if((gen_hash3i(world_x0, 1501, world_z0, seed ^ 0x611E5AF7U) % 100U) >= 3U)
		return;

	int cx = 3 + (int)(gen_hash3i(world_x0, 1511, world_z0, seed ^ 0x5AF71001U) % 10U);
	int cz = 3 + (int)(gen_hash3i(world_x0, 1517, world_z0, seed ^ 0x5AF71002U) % 10U);
	int cy = 16 + (int)(gen_hash3i(world_x0, 1523, world_z0, seed ^ 0x5AF71003U) % 18U);
	bool along_x = (gen_hash3i(world_x0, 1529, world_z0, seed ^ 0x5AF71004U) & 1U) == 0U;
	int len = 8 + (int)(gen_hash3i(world_x0, 1531, world_z0, seed ^ 0x5AF71005U) % 5U);

	if(along_x) {
		int x0 = cx - len / 2;
		int x1 = cx + len / 2;
		for(int x = x0; x <= x1; x++) {
			for(int y = cy; y <= cy + 2; y++) {
				for(int z = cz - 1; z <= cz + 1; z++)
					gen_set_block(sc, x, y, z, BLOCK_AIR);
			}
			gen_set_block(sc, x, cy - 1, cz, BLOCK_PLANKS);
			if((x - x0) % 4 == 0) {
				gen_fill_box(sc, x, cy, cz - 1, x, cy + 2, cz - 1, BLOCK_FENCE);
				gen_fill_box(sc, x, cy, cz + 1, x, cy + 2, cz + 1, BLOCK_FENCE);
				gen_fill_box(sc, x - 1, cy + 2, cz - 1, x + 1, cy + 2, cz + 1,
							 BLOCK_PLANKS);
				gen_set_block_with_meta(sc, x, cy + 1, cz - 2, BLOCK_TORCH, 3); /* Nordseite → zeigt Nord */
				gen_set_block_with_meta(sc, x, cy + 1, cz + 2, BLOCK_TORCH, 4); /* Südseite → zeigt Süd */
			}
			if((gen_hash3i(x, cy, cz, seed ^ 0x5AF72001U) % 100U) < 55U)
				gen_set_block(sc, x, cy, cz, BLOCK_RAIL);
		}
	} else {
		int z0 = cz - len / 2;
		int z1 = cz + len / 2;
		for(int z = z0; z <= z1; z++) {
			for(int y = cy; y <= cy + 2; y++) {
				for(int x = cx - 1; x <= cx + 1; x++)
					gen_set_block(sc, x, y, z, BLOCK_AIR);
			}
			gen_set_block(sc, cx, cy - 1, z, BLOCK_PLANKS);
			if((z - z0) % 4 == 0) {
				gen_fill_box(sc, cx - 1, cy, z, cx - 1, cy + 2, z, BLOCK_FENCE);
				gen_fill_box(sc, cx + 1, cy, z, cx + 1, cy + 2, z, BLOCK_FENCE);
				gen_fill_box(sc, cx - 1, cy + 2, z - 1, cx + 1, cy + 2, z + 1,
							 BLOCK_PLANKS);
				gen_set_block_with_meta(sc, cx - 2, cy + 1, z, BLOCK_TORCH, 2); /* Westseite → zeigt West */
				gen_set_block_with_meta(sc, cx + 2, cy + 1, z, BLOCK_TORCH, 1); /* Ostseite → zeigt Ost */
			}
			if((gen_hash3i(cx, cy, z, seed ^ 0x5AF72002U) % 100U) < 55U)
				gen_set_block(sc, cx, cy, z, BLOCK_RAIL);
		}
	}
}

static void gen_place_small_house(struct server_chunk* sc, int cx, int y, int cz,
								  uint8_t wall, uint8_t roof, uint8_t floor,
								  uint8_t glass) {
	gen_flatten_surface_patch(sc, cx, cz, 3, y, floor, floor);
	for(int x = cx - 2; x <= cx + 2; x++) {
		for(int z = cz - 2; z <= cz + 2; z++) {
			gen_set_block(sc, x, y, z, floor);
			for(int yy = y + 1; yy <= y + 3; yy++) {
				bool wall_block = (x == cx - 2 || x == cx + 2 || z == cz - 2 || z == cz + 2);
				if(!wall_block) {
					gen_set_block(sc, x, yy, z, BLOCK_AIR);
					continue;
				}
				gen_set_block(sc, x, yy, z, wall);
			}
			gen_set_block(sc, x, y + 4, z, roof);
		}
	}
	gen_set_block(sc, cx, y + 1, cz - 2, BLOCK_AIR);
	gen_set_block(sc, cx, y + 2, cz - 2, BLOCK_AIR);
	gen_set_block_with_meta(sc, cx, y + 1, cz - 1, BLOCK_TORCH, 5);
	gen_set_block(sc, cx - 2, y + 2, cz, glass);
	gen_set_block(sc, cx + 2, y + 2, cz, glass);
	gen_set_block(sc, cx, y + 2, cz + 2, glass);
	gen_set_block_with_meta(sc, cx, y + 1, cz, BLOCK_TORCH, 5);
	gen_set_block(sc, cx - 1, y + 1, cz - 1, BLOCK_WORKBENCH);
	gen_set_block(sc, cx + 1, y + 1, cz - 1, BLOCK_CHEST);
}

/* Dorfhaus:
 * - Kopfstein-Fundament
 * - Eichenholz-Planken Wände
 * - Log-Ecksäulen
 * - Glasscheiben-Fenster
 * - Dachüberhang mit Platten
 * Fußpunkt (ox,y,oz) = Vordermitte der Haustür (Tür öffnet nach außen in -z)
 * Haus ist 5 breit (x: -2..+2), 7 tief (z: -1..+5), 4 hoch */

/* plains_fountain: Groesse 9x4x9 */
static void gen_place_plains_fountain(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_set_block(sc, ox+2, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+2, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+0, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+0, oy+0, oz+2, BLOCK_STONE);
    gen_set_block(sc, ox+0, oy+0, oz+3, BLOCK_STONE);
    gen_set_block(sc, ox+0, oy+0, oz+4, BLOCK_STONE);
    gen_set_block(sc, ox+0, oy+0, oz+5, BLOCK_STONE);
    gen_set_block(sc, ox+0, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+5, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+6, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+1, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+2, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+2, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+4, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+4, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+5, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+5, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+5, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+6, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+6, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+6, oy+0, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+6, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+0, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+3, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+4, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+5, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+6, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+0, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+1, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+2, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+3, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+4, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+5, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+6, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+0, oz+7, BLOCK_STONE);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_TORCH, 5);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+6, BLOCK_TORCH, 5);
    gen_set_block(sc, ox+3, oy+1, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+1, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+1, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+1, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+1, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+5, oy+1, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+5, oy+1, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+5, oy+1, oz+5, BLOCK_WATER_STILL);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+2, BLOCK_TORCH, 5);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+6, BLOCK_TORCH, 5);
    gen_set_block(sc, ox+3, oy+2, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+2, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+2, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+5, oy+2, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+3, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+3, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+3, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+3, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+5, oy+3, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+2, oz+2, BLOCK_STONE);
}

/* plains_small_house: Groesse 7x7x7 */
static void gen_place_plains_small_house(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Innenraum leeren (Dirt/Stein durch Luft ersetzen) */
    for(int iy=1; iy<=3; iy++)
      for(int ix=1; ix<=5; ix++)
        for(int iz=1; iz<=5; iz++) {
          uint8_t b=gen_get_block(sc,ox+ix,oy+iy,oz+iz);
          if(b!=BLOCK_AIR && b!=BLOCK_WATER_STILL && b!=BLOCK_WATER_FLOW)
            gen_set_block(sc,ox+ix,oy+iy,oz+iz,BLOCK_AIR);
        }
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+3, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+5, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+2, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+5, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+2, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+2, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+3, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+5, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+3, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+3, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+3, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+3, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+3, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+3, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+2, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+4, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+3, BLOCK_DOOR_WOOD, 8);
    gen_set_block(sc, ox+3, oy+2, oz+1, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+3, oy+2, oz+5, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+5, oy+2, oz+3, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+3, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+5, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+2, BLOCK_BED, 3);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+2, BLOCK_BED, 11);
}

/* plains_medium_house: Groesse 13x8x11 */
static void gen_place_plains_medium_house(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Innenraum leeren (Dirt/Stein durch Luft ersetzen) */
    for(int iy=1; iy<=5; iy++)
      for(int ix=1; ix<=11; ix++)
        for(int iz=1; iz<=9; iz++) {
          uint8_t b=gen_get_block(sc,ox+ix,oy+iy,oz+iz);
          if(b!=BLOCK_AIR && b!=BLOCK_WATER_STILL && b!=BLOCK_WATER_FLOW)
            gen_set_block(sc,ox+ix,oy+iy,oz+iz,BLOCK_AIR);
        }
    gen_set_block(sc, ox+0, oy+0, oz+3, BLOCK_GRASS);
    gen_set_block(sc, ox+0, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+1, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+2, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+3, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+4, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+2, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+7, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+8, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+9, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+10, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+3, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+4, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+5, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+6, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+8, BLOCK_DIRT);
    gen_set_block(sc, ox+11, oy+0, oz+9, BLOCK_DIRT);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+9, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+3, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+4, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+5, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+6, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+6, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+7, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+8, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+9, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+10, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+10, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+3, BLOCK_LOG, 0);
    gen_set_block(sc, ox+11, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+1, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+1, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+2, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+9, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+2, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+6, oy+2, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+2, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+2, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+2, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+2, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+10, oy+2, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+10, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+2, oz+3, BLOCK_LOG, 0);
    gen_set_block(sc, ox+11, oy+2, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+2, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+2, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+2, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+2, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+2, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+4, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+8, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+9, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+11, oy+3, oz+3, BLOCK_LOG, 0);
    gen_set_block(sc, ox+11, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+3, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+3, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+3, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+3, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+10, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+4, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+4, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+4, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+4, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+3, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+6, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+7, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+10, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+10, oy+4, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+4, oz+3, BLOCK_LOG, 0);
    gen_set_block(sc, ox+11, oy+4, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+4, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+4, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+4, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+11, oy+4, oz+8, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+4, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+4, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+4, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+10, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+3, oy+5, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+3, oy+5, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+5, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+4, oy+5, oz+9, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+11, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+11, oy+6, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+11, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+10, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+11, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+12, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+1, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+3, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block(sc, ox+2, oy+2, oz+6, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+7, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block(sc, ox+6, oy+2, oz+8, BLOCK_FENCE);
    gen_set_block(sc, ox+10, oy+2, oz+4, BLOCK_FENCE);
    gen_set_block(sc, ox+10, oy+2, oz+8, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+2, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+4, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+3, BLOCK_DOOR_WOOD, 8);
    gen_set_block(sc, ox+1, oy+3, oz+6, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+3, oz+6, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+3, oz+1, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+3, oy+3, oz+9, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+3, oz+1, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+3, oz+9, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+6, oy+3, oz+8, BLOCK_STONE);
    gen_set_block(sc, ox+8, oy+3, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+8, oy+3, oz+9, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+3, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+3, oz+9, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+10, oy+3, oz+4, BLOCK_STONE);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+6, BLOCK_TORCH, 2);
    gen_set_block(sc, ox+10, oy+3, oz+8, BLOCK_STONE);
    gen_set_block_with_meta(sc, ox+12, oy+3, oz+6, BLOCK_TORCH, 1);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+7, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+8, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+9, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+3, BLOCK_TORCH, 1);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+8, BLOCK_TORCH, 4);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+11, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+11, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+12, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+12, oy+4, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+6, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+7, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+8, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+10, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block(sc, ox+11, oy+5, oz+6, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+11, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+12, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+12, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+6, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+7, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+9, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+10, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+11, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+11, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+12, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+12, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+6, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+8, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+9, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+10, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+11, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+11, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+12, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+12, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+5, BLOCK_BED, 3);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+7, BLOCK_BED, 3);
    gen_set_block_with_meta(sc, ox+10, oy+2, oz+5, BLOCK_BED, 11);
    gen_set_block_with_meta(sc, ox+10, oy+2, oz+7, BLOCK_BED, 11);
}

/* ---- Dorfhaus-Strukturen ---- */

/* Hilfsfunktion: Hohlbox (Rand=block_wall, Innen=block_fill) */
static void gen_fill_box_hollow(struct server_chunk* sc,
                                int x1, int y1, int z1,
                                int x2, int y2, int z2,
                                uint8_t block_wall, uint8_t block_fill) {
    for(int y = y1; y <= y2; y++)
      for(int x = x1; x <= x2; x++)
        for(int z = z1; z <= z2; z++) {
            bool on_boundary = (x==x1||x==x2||y==y1||y==y2||z==z1||z==z2);
            gen_set_block(sc, x, y, z, on_boundary ? block_wall : block_fill);
        }
}

/* uy — Kleine Huette, 5×6×5 */
static void gen_mc10_house_uy(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Boden + Dachrahmen + Dachfuellung */
    gen_fill_box(sc, ox+0, oy+0, oz+0, ox+4, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+0, oy+4, oz+0, ox+4, oy+4, oz+4, BLOCK_LOG);
    gen_fill_box(sc, ox+1, oy+4, oz+1, ox+3, oy+4, oz+3, BLOCK_PLANKS);
    /* Cobblestone-Ecksaeulen */
    gen_fill_box(sc, ox+0, oy+1, oz+0, ox+0, oy+3, oz+0, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+4, oy+1, oz+0, ox+4, oy+3, oz+0, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+0, oy+1, oz+4, ox+0, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+4, oy+1, oz+4, ox+4, oy+3, oz+4, BLOCK_COBBLESTONE);
    /* Planken-Waende */
    gen_fill_box(sc, ox+0, oy+1, oz+1, ox+0, oy+3, oz+3, BLOCK_PLANKS);
    gen_fill_box(sc, ox+4, oy+1, oz+1, ox+4, oy+3, oz+3, BLOCK_PLANKS);
    gen_fill_box(sc, ox+1, oy+1, oz+4, ox+3, oy+3, oz+4, BLOCK_PLANKS);
    /* Fenster */
    gen_set_block(sc, ox+0, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+2, oz+4, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+2, oz+2, BLOCK_GLASS_PANE);
    /* Vorderwand mit Tueroeffnung */
    gen_set_block(sc, ox+1, oy+1, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+1, oy+2, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+1, oy+3, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+2, oy+3, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+3, oy+3, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+3, oy+2, oz+0, BLOCK_PLANKS);
    gen_set_block(sc, ox+3, oy+1, oz+0, BLOCK_PLANKS);
    /* Innenraum leeren */
    gen_fill_box(sc, ox+1, oy+1, oz+1, ox+3, oy+3, oz+3, BLOCK_AIR);
    /* Fackel innen (stehend) */
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+1, BLOCK_TORCH, 5);
    /* Einrichtung: Bett (Fuss +z) + Werkbank */
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_BED, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+2, BLOCK_BED, 0 | 8);
    gen_set_block(sc, ox+3, oy+1, oz+3, BLOCK_WORKBENCH);
    /* Tuer */
    gen_set_block(sc, ox+2, oy+1, oz+0, BLOCK_AIR);
    gen_set_block(sc, ox+2, oy+2, oz+0, BLOCK_AIR);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+0, BLOCK_DOOR_WOOD, 1);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+0, BLOCK_DOOR_WOOD, 9);
}

/* wi — Kleine Huette (Variante), 4×6×5 */
static void gen_mc10_house_wi(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_fill_box(sc, ox+1, oy+1, oz+1, ox+3, oy+5, oz+4, BLOCK_AIR);
    gen_fill_box(sc, ox, oy, oz, ox+3, oy, oz+4, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy, oz+1, ox+2, oy, oz+3, BLOCK_DIRT);
    gen_fill_box(sc, ox+1, oy+4, oz+1, ox+2, oy+4, oz+3, BLOCK_LOG);
    gen_set_block(sc, ox+1, oy+4, oz, BLOCK_LOG);
    gen_set_block(sc, ox+2, oy+4, oz, BLOCK_LOG);
    gen_set_block(sc, ox+1, oy+4, oz+4, BLOCK_LOG);
    gen_set_block(sc, ox+2, oy+4, oz+4, BLOCK_LOG);
    gen_set_block(sc, ox, oy+4, oz+1, BLOCK_LOG);
    gen_set_block(sc, ox, oy+4, oz+2, BLOCK_LOG);
    gen_set_block(sc, ox, oy+4, oz+3, BLOCK_LOG);
    gen_set_block(sc, ox+3, oy+4, oz+1, BLOCK_LOG);
    gen_set_block(sc, ox+3, oy+4, oz+2, BLOCK_LOG);
    gen_set_block(sc, ox+3, oy+4, oz+3, BLOCK_LOG);
    gen_fill_box(sc, ox, oy+1, oz, ox, oy+3, oz, BLOCK_LOG);
    gen_fill_box(sc, ox+3, oy+1, oz, ox+3, oy+3, oz, BLOCK_LOG);
    gen_fill_box(sc, ox, oy+1, oz+4, ox, oy+3, oz+4, BLOCK_LOG);
    gen_fill_box(sc, ox+3, oy+1, oz+4, ox+3, oy+3, oz+4, BLOCK_LOG);
    gen_fill_box(sc, ox, oy+1, oz+1, ox, oy+3, oz+3, BLOCK_PLANKS);
    gen_fill_box(sc, ox+3, oy+1, oz+1, ox+3, oy+3, oz+3, BLOCK_PLANKS);
    gen_fill_box(sc, ox+1, oy+1, oz, ox+2, oy+3, oz, BLOCK_PLANKS);
    gen_fill_box(sc, ox+1, oy+1, oz+4, ox+2, oy+3, oz+4, BLOCK_PLANKS);
    gen_set_block(sc, ox, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+3, oy+2, oz+2, BLOCK_GLASS_PANE);
    /* Einrichtung: Bett (Fuss +z) + Truhe */
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_BED, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+3, BLOCK_BED, 0 | 8);
    gen_set_block(sc, ox+1, oy+1, oz+3, BLOCK_CHEST);
    gen_set_block(sc, ox+1, oy+1, oz, BLOCK_AIR);
    gen_set_block(sc, ox+1, oy+2, oz, BLOCK_AIR);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz, BLOCK_DOOR_WOOD, 1);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz, BLOCK_DOOR_WOOD, 9);
}

/* ko — Haus mit Werkstatt, 10×6×7 */
static void gen_mc10_house_ko(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_fill_box(sc, ox, oy+1, oz, ox+9, oy+4, oz+6, BLOCK_AIR);
    gen_fill_box(sc, ox, oy, oz, ox+9, oy, oz+6, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox, oy+4, oz, ox+9, oy+4, oz+6, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox, oy+5, oz, ox+9, oy+5, oz+6, BLOCK_SLAB);
    gen_fill_box(sc, ox+1, oy+1, oz, ox+2, oy+3, oz, BLOCK_PLANKS);
    gen_fill_box(sc, ox, oy+1, oz, ox, oy+4, oz, BLOCK_LOG);
    gen_fill_box(sc, ox+3, oy+1, oz, ox+3, oy+4, oz, BLOCK_LOG);
    gen_fill_box(sc, ox, oy+1, oz+6, ox, oy+4, oz+6, BLOCK_LOG);
    gen_set_block(sc, ox+3, oy+3, oz+1, BLOCK_PLANKS);
    gen_fill_box(sc, ox+3, oy+1, oz+2, ox+3, oy+3, oz+2, BLOCK_PLANKS);
    gen_fill_box(sc, ox+4, oy+1, oz+3, ox+5, oy+3, oz+3, BLOCK_PLANKS);
    gen_fill_box(sc, ox, oy+1, oz+1, ox, oy+3, oz+5, BLOCK_PLANKS);
    gen_fill_box(sc, ox+1, oy+1, oz+6, ox+5, oy+3, oz+6, BLOCK_PLANKS);
    gen_fill_box(sc, ox+5, oy+1, oz, ox+5, oy+3, oz, BLOCK_FENCE);
    gen_fill_box(sc, ox+9, oy+1, oz, ox+9, oy+3, oz, BLOCK_FENCE);
    gen_fill_box(sc, ox+6, oy+4, oz+4, ox+9, oy+4, oz+6, BLOCK_COBBLESTONE);
    /* Lava-Grube (BLOCK_LAVA_STILL=11) */
    gen_set_block(sc, ox+7, oy+1, oz+5, BLOCK_LAVA_STILL);
    gen_set_block(sc, ox+8, oy+1, oz+5, BLOCK_LAVA_STILL);
    gen_fill_box(sc, ox+7, oy+2, oz+4, ox+8, oy+2, oz+5, BLOCK_AIR);
    /* Oefen */
    gen_set_block(sc, ox+6, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+2, oz+3, BLOCK_FURNACE);
    gen_set_block(sc, ox+6, oy+3, oz+3, BLOCK_FURNACE);
    /* Steinplatte als Arbeitsflaeche */
    gen_set_block(sc, ox+8, oy+1, oz+1, BLOCK_SLAB);
    gen_set_block(sc, ox, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox, oy+2, oz+4, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+2, oz+6, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+2, oz+6, BLOCK_GLASS_PANE);
    /* Einrichtung: Truhe (Schmied-Loot) + Werkbank im Wohnbereich */
    gen_set_block(sc, ox+1, oy+1, oz+1, BLOCK_CHEST);
    gen_set_block(sc, ox+4, oy+1, oz+5, BLOCK_WORKBENCH);
    gen_set_block(sc, ox+7, oy+1, oz, BLOCK_AIR);
    gen_set_block(sc, ox+7, oy+2, oz, BLOCK_AIR);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz, BLOCK_DOOR_WOOD, 1);
    gen_set_block_with_meta(sc, ox+7, oy+2, oz, BLOCK_DOOR_WOOD, 9);
}

/* tf — Grosses L-Haus, 9×7×12 */
static void gen_mc10_house_tf(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_fill_box(sc, ox+1, oy+1, oz+1, ox+7, oy+4, oz+4, BLOCK_AIR);
    gen_fill_box(sc, ox+2, oy+1, oz+6, ox+8, oy+4, oz+10, BLOCK_AIR);
    gen_fill_box(sc, ox+2, oy, oz+5, ox+8, oy, oz+10, BLOCK_PLANKS);
    gen_fill_box(sc, ox+1, oy, oz+1, ox+7, oy, oz+4, BLOCK_PLANKS);
    gen_fill_box(sc, ox, oy, oz, ox, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+8, oy, oz, ox+8, oy+3, oz+10, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy, oz, ox+7, oy+2, oz, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy+2, oz, ox+7, oy+3, oz, BLOCK_PLANKS);
    gen_fill_box(sc, ox+2, oy, oz+6, ox+2, oy+3, oz+10, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+3, oy, oz+10, ox+7, oy+3, oz+10, BLOCK_COBBLESTONE);
    /* Dach (Holztreppe) */
    gen_fill_box(sc, ox, oy+4, oz+1, ox+8, oy+4, oz+1, BLOCK_PLANKS);
    gen_fill_box(sc, ox, oy+5, oz+2, ox+8, oy+5, oz+3, BLOCK_PLANKS);
    gen_set_block(sc, ox, oy+4, oz+2, BLOCK_PLANKS);
    gen_set_block(sc, ox, oy+4, oz+3, BLOCK_PLANKS);
    gen_set_block(sc, ox+8, oy+4, oz+2, BLOCK_PLANKS);
    gen_set_block(sc, ox+8, oy+4, oz+3, BLOCK_PLANKS);
    for(int n5 = -1; n5 <= 2; n5++)
        for(int n4 = 0; n4 <= 8; n4++) {
            gen_set_block_with_meta(sc, ox+n4, oy+4+n5, oz+n5, BLOCK_WOODEN_STAIRS, 3);
            if(!(n5<=-1 && n4>1) && !(n5<=0 && n4>3))
                gen_set_block_with_meta(sc, ox+n4, oy+4+n5, oz+5-n5, BLOCK_WOODEN_STAIRS, 2);
        }
    /* Fenster und Saeulen */
    gen_set_block(sc, ox, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox, oy+2, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+5, oy+2, oz, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+8, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+8, oy+2, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+8, oy+2, oz+7, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+8, oy+2, oz+8, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+2, oz+7, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+2, oz+8, BLOCK_GLASS_PANE);
    /* Einrichtung im Wohnbereich (x1-7, z1-4): Bett + Werkbank + Ofen */
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+1, BLOCK_BED, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+2, BLOCK_BED, 0 | 8);
    gen_set_block(sc, ox+1, oy+1, oz+3, BLOCK_WORKBENCH);
    gen_set_block(sc, ox+1, oy+1, oz+1, BLOCK_FURNACE);
    /* Fackel innen (stehend) */
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+1, BLOCK_TORCH, 5);
    /* Tuer */
    gen_set_block(sc, ox+2, oy+1, oz, BLOCK_AIR);
    gen_set_block(sc, ox+2, oy+2, oz, BLOCK_AIR);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz, BLOCK_DOOR_WOOD, 1);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz, BLOCK_DOOR_WOOD, 9);
    /* Eingang vor der Tuer freiraeumen */
    gen_fill_box(sc, ox+1, oy+1, oz-1, ox+3, oy+2, oz-1, BLOCK_AIR);
}

/* uz — Grosses Haus/Turm (2-stoeckig), 5×12×9 */
static void gen_mc10_house_uz(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_fill_box(sc, ox+1, oy+1, oz+1, ox+3, oy+3, oz+7, BLOCK_AIR);
    gen_fill_box(sc, ox+1, oy+5, oz+1, ox+3, oy+9, oz+3, BLOCK_AIR);
    gen_fill_box(sc, ox+1, oy, oz, ox+3, oy, oz+8, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy+1, oz+0, ox+3, oy+10, oz+0, BLOCK_COBBLESTONE); /* Vorderwand z=0 */
    gen_fill_box(sc, ox, oy+1, oz+1, ox, oy+10, oz+3, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+4, oy+1, oz+1, ox+4, oy+10, oz+3, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox, oy, oz+4, ox, oy+4, oz+7, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+4, oy, oz+4, ox+4, oy+4, oz+7, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy+1, oz+8, ox+3, oy+4, oz+8, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox+1, oy+5, oz+4, ox+3, oy+10, oz+4, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox, oy+9, oz, ox+4, oy+9, oz+4, BLOCK_COBBLESTONE);
    gen_fill_box(sc, ox, oy+4, oz, ox+4, oy+4, oz+4, BLOCK_COBBLESTONE);
    /* Fenster */
    gen_set_block(sc, ox, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox, oy+3, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+2, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+3, oz+2, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+2, oy+3, oz+8, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox, oy+3, oz+6, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+4, oy+3, oz+6, BLOCK_GLASS_PANE);
    /* Fackeln innen (stehend) */
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+7, BLOCK_TORCH, 5);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+6, BLOCK_TORCH, 5);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+6, BLOCK_TORCH, 5);
    /* Leiter innen */
    for(int i = 1; i <= 9; i++)
        gen_set_block_with_meta(sc, ox+3, oy+i, oz+3, BLOCK_LADDER, 4);
    /* Einrichtung: Bett (Fuss +z) + Truhe im Erdgeschoss */
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_BED, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+2, BLOCK_BED, 0 | 8);
    gen_set_block(sc, ox+3, oy+1, oz+1, BLOCK_CHEST);
    /* Tuer */
    gen_set_block(sc, ox+2, oy+1, oz, BLOCK_AIR);
    gen_set_block(sc, ox+2, oy+2, oz, BLOCK_AIR);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz, BLOCK_DOOR_WOOD, 1);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz, BLOCK_DOOR_WOOD, 9);
}

/* ---- Ende Dorfhaus-Strukturen ---- */

/* plains_lamp: Groesse 3x4x3 */
/* Zaun-Pfahl 3 hoch, Planken-Kopf, 4 Fackeln rundum */
static void gen_place_plains_lamp(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Pfahl aus Zaun */
    gen_set_block(sc, ox, oy+0, oz, BLOCK_FENCE);
    gen_set_block(sc, ox, oy+1, oz, BLOCK_FENCE);
    gen_set_block(sc, ox, oy+2, oz, BLOCK_FENCE);
    /* Holzbrett als Lampenkopf */
    gen_set_block_with_meta(sc, ox, oy+3, oz, BLOCK_PLANKS, 0);
    /* 4 Fackeln an allen Seiten des Lampenkopfs */
    gen_set_block_with_meta(sc, ox-1, oy+3, oz, BLOCK_TORCH, 2); /* West: lehnt Ost=Kopf */
    gen_set_block_with_meta(sc, ox+1, oy+3, oz, BLOCK_TORCH, 1); /* Ost:  lehnt West=Kopf */
    gen_set_block_with_meta(sc, ox, oy+3, oz-1, BLOCK_TORCH, 4); /* Nord: lehnt Sued=Kopf */
    gen_set_block_with_meta(sc, ox, oy+3, oz+1, BLOCK_TORCH, 3); /* Sued: lehnt Nord=Kopf */
}

/* plains_small_farm: Groesse 7x6x9 */
static void gen_place_plains_small_farm(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+4, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+2, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+7, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block(sc, ox+5, oy+1, oz+7, BLOCK_WORKBENCH);
}

/* plains_large_farm: Groesse 13x6x9 */
static void gen_place_plains_large_farm(struct server_chunk* sc, int ox, int oy, int oz) {
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+0, oz+1, BLOCK_DIRT);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+4, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+10, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+10, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+11, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block(sc, ox+11, oy+0, oz+1, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+11, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+0, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+3, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+4, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+12, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+2, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+2, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+3, oy+0, oz+7, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+7, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+8, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+9, oy+0, oz+1, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+2, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+3, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+4, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+5, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+6, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+9, oy+0, oz+7, BLOCK_WATER_STILL);
    gen_set_block(sc, ox+10, oy+0, oz+1, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+10, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+2, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+3, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+4, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+5, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+6, BLOCK_FARMLAND);
    gen_set_block(sc, ox+11, oy+0, oz+7, BLOCK_FARMLAND);
    gen_set_block(sc, ox+1, oy+1, oz+1, BLOCK_WORKBENCH);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+1, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+1, oz+7, BLOCK_CROPS, 7);
    gen_set_block(sc, ox+11, oy+1, oz+1, BLOCK_WORKBENCH);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+2, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+3, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+4, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+5, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+6, BLOCK_CROPS, 7);
    gen_set_block_with_meta(sc, ox+11, oy+1, oz+7, BLOCK_CROPS, 7);
}

/* plains_library: Groesse 11x10x17 */
static void gen_place_plains_library(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Innenraum leeren (Dirt/Stein durch Luft ersetzen) */
    for(int iy=1; iy<=7; iy++)
      for(int ix=1; ix<=9; ix++)
        for(int iz=1; iz<=15; iz++) {
          uint8_t b=gen_get_block(sc,ox+ix,oy+iy,oz+iz);
          if(b!=BLOCK_AIR && b!=BLOCK_WATER_STILL && b!=BLOCK_WATER_FLOW)
            gen_set_block(sc,ox+ix,oy+iy,oz+iz,BLOCK_AIR);
        }
    gen_set_block(sc, ox+1, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+3, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+5, BLOCK_LOG, 0);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block(sc, ox+3, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+15, BLOCK_LOG, 0);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+15, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+15, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+15, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+15, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+15, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+9, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+10, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+12, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+13, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+14, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+15, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+1, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+1, oz+10, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+1, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+1, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+5, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+11, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+12, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+1, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+1, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+8, oy+1, oz+5, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+1, oz+6, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+1, oz+7, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+1, oz+9, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+1, oz+10, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+1, oz+11, BLOCK_BOOKSHELF);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+15, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+2, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+2, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+2, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+2, oz+10, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+2, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+2, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+1, BLOCK_LOG, 4);
    gen_set_block(sc, ox+5, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+2, oz+11, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+2, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+2, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+8, oy+2, oz+1, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+8, oy+2, oz+6, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+8, oy+2, oz+10, BLOCK_BOOKSHELF);
    gen_set_block_with_meta(sc, ox+8, oy+2, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+3, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+5, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+7, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+9, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+11, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+13, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+15, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+3, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+8, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+9, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+2, oy+3, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+9, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+3, oz+10, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+2, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+3, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+4, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+12, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+13, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+14, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+2, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+3, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+4, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+5, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+6, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+7, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+8, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+9, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+10, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+11, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+12, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+13, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+14, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+15, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+6, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+7, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+8, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+9, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+10, BLOCK_LOG, 8);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+6, oy+7, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+6, oy+7, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+1, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+15, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+3, oy+8, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+8, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+8, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+8, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+8, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+0, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+7, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+9, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+10, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+11, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+12, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+13, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+14, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+15, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+8, oz+16, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+9, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+9, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+9, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+9, oz+8, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+8, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+3, BLOCK_STONE_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+13, BLOCK_STONE_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+2, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+14, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+2, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+12, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+13, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+1, oz+14, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+7, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+9, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+8, BLOCK_DOOR_WOOD, 8);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+1, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+15, BLOCK_TORCH, 2);
    gen_set_block(sc, ox+3, oy+2, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+3, oy+2, oz+13, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+4, BLOCK_STONE_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+12, BLOCK_STONE_STAIRS, 3);
    gen_set_block(sc, ox+6, oy+2, oz+1, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+6, oy+2, oz+15, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+2, oz+4, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+2, oz+8, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+2, oz+12, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+10, oy+2, oz+6, BLOCK_TORCH, 1);
    gen_set_block_with_meta(sc, ox+10, oy+2, oz+10, BLOCK_TORCH, 1);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+5, BLOCK_STONE_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+11, BLOCK_STONE_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+6, BLOCK_TORCH, 1);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+10, BLOCK_TORCH, 1);
    gen_set_block(sc, ox+1, oy+4, oz+7, BLOCK_FENCE);
    gen_set_block(sc, ox+1, oy+4, oz+8, BLOCK_FENCE);
    gen_set_block(sc, ox+1, oy+4, oz+9, BLOCK_FENCE);
    gen_set_block(sc, ox+2, oy+4, oz+6, BLOCK_FENCE);
    gen_set_block(sc, ox+2, oy+4, oz+10, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+7, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+9, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+12, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+13, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+14, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+15, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+16, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+7, BLOCK_DOOR_WOOD, 8);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+9, BLOCK_DOOR_WOOD, 8);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+16, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+16, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+6, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+7, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+11, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+12, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+13, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+14, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+15, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+16, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+12, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+13, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+14, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+15, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+16, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+16, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+2, BLOCK_TORCH, 3);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+14, BLOCK_TORCH, 4);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+16, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+5, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+6, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+7, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+11, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+12, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+13, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+14, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+15, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+16, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+2, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+7, oz+6, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+7, oz+10, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+2, oy+7, oz+11, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+7, oz+11, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+11, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+12, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+13, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+14, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+15, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+7, oz+16, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+5, oy+7, oz+16, BLOCK_WOODEN_STAIRS, 5);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+7, oy+7, oz+16, BLOCK_WOODEN_STAIRS, 4);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+5, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+6, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+7, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+11, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+12, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+13, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+14, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+15, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+8, oy+7, oz+16, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+2, oy+8, oz+6, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+8, oz+7, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+2, oy+8, oz+9, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+2, oy+8, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+8, oz+6, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+8, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+8, oz+6, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+8, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+0, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+1, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+3, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+5, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+6, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+10, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+11, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+12, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+13, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+14, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+15, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+8, oz+16, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+0, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+1, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+3, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+4, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+5, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+6, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+7, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+10, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+11, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+12, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+13, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+14, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+15, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+7, oy+8, oz+16, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+2, oy+9, oz+7, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+9, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+9, oz+7, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+9, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+9, oz+7, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+9, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+9, oz+7, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+9, oz+9, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+9, oz+7, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+9, oz+8, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block_with_meta(sc, ox+6, oy+9, oz+9, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block(sc, ox+5, oy+1, oz+6, BLOCK_BOOKSHELF);
    gen_set_block(sc, ox+5, oy+1, oz+10, BLOCK_BOOKSHELF);
}

/* plains_butcher: Groesse 11x8x12 */
static void gen_place_plains_butcher(struct server_chunk* sc, int ox, int oy, int oz) {
    /* Innenraum leeren (Dirt/Stein durch Luft ersetzen) */
    for(int iy=1; iy<=5; iy++)
      for(int ix=1; ix<=9; ix++)
        for(int iz=1; iz<=10; iz++) {
          uint8_t b=gen_get_block(sc,ox+ix,oy+iy,oz+iz);
          if(b!=BLOCK_AIR && b!=BLOCK_WATER_STILL && b!=BLOCK_WATER_FLOW)
            gen_set_block(sc,ox+ix,oy+iy,oz+iz,BLOCK_AIR);
        }
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+3, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+10, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+3, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block(sc, ox+4, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+4, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+0, oz+7, BLOCK_GRASS);
    gen_set_block(sc, ox+4, oy+0, oz+8, BLOCK_GRASS);
    gen_set_block(sc, ox+4, oy+0, oz+9, BLOCK_GRASS);
    gen_set_block(sc, ox+4, oy+0, oz+10, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+4, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+5, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+5, oy+0, oz+8, BLOCK_GRASS);
    gen_set_block(sc, ox+5, oy+0, oz+9, BLOCK_GRASS);
    gen_set_block(sc, ox+5, oy+0, oz+10, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+5, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block(sc, ox+6, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+2, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+3, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+4, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+6, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+0, oz+7, BLOCK_DIRT);
    gen_set_block(sc, ox+6, oy+0, oz+8, BLOCK_GRASS);
    gen_set_block(sc, ox+6, oy+0, oz+10, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+6, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block(sc, ox+7, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+2, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+3, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+4, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+7, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+7, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+7, oy+0, oz+8, BLOCK_GRASS);
    gen_set_block(sc, ox+7, oy+0, oz+9, BLOCK_GRASS);
    gen_set_block(sc, ox+7, oy+0, oz+10, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+7, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block(sc, ox+8, oy+0, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+2, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+3, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+4, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+8, oy+0, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+0, oz+7, BLOCK_GRASS);
    gen_set_block(sc, ox+8, oy+0, oz+8, BLOCK_GRASS);
    gen_set_block(sc, ox+8, oy+0, oz+9, BLOCK_GRASS);
    gen_set_block(sc, ox+8, oy+0, oz+10, BLOCK_GRASS);
    gen_set_block_with_meta(sc, ox+8, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+9, oy+0, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+0, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+7, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+8, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+9, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+10, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+0, oz+11, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+6, BLOCK_LOG, 0);
    gen_set_block(sc, ox+2, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+5, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+2, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+3, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+4, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+5, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+5, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+5, oy+1, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+6, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+6, oy+1, oz+7, BLOCK_STONE);
    gen_set_block(sc, ox+7, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+2, BLOCK_SLAB, 0);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+3, BLOCK_SLAB, 0);
    gen_set_block(sc, ox+8, oy+1, oz+1, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+1, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+9, oy+1, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+1, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+1, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+1, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+1, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+2, oz+6, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+4, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+6, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+6, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+6, oy+2, oz+6, BLOCK_LOG, 4);
    gen_set_block_with_meta(sc, ox+8, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+8, oy+2, oz+6, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+1, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+2, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+5, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+3, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+6, BLOCK_LOG, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+1, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+6, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+1, BLOCK_LOG, 0);
    gen_set_block(sc, ox+9, oy+3, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+3, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+3, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+6, BLOCK_LOG, 0);
    gen_set_block(sc, ox+1, oy+4, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+4, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+4, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+5, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+2, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+9, oy+4, oz+2, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+4, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+4, oz+4, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+4, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+5, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+1, oy+5, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+3, BLOCK_PLANKS, 0);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+4, BLOCK_PLANKS, 0);
    gen_set_block(sc, ox+8, oy+5, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+5, oz+3, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+5, oz+4, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+0, oy+0, oz+4, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+3, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+1, oy+1, oz+4, BLOCK_DOOR_WOOD, 0);
    gen_set_block_with_meta(sc, ox+2, oy+1, oz+2, BLOCK_WOODEN_STAIRS, 1);
    gen_set_block(sc, ox+3, oy+1, oz+2, BLOCK_FENCE);
    gen_set_block(sc, ox+3, oy+1, oz+7, BLOCK_FENCE);
    gen_set_block(sc, ox+3, oy+1, oz+8, BLOCK_FENCE);
    gen_set_block(sc, ox+3, oy+1, oz+9, BLOCK_FENCE);
    gen_set_block(sc, ox+3, oy+1, oz+10, BLOCK_FENCE);
    gen_set_block(sc, ox+3, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+4, oy+1, oz+2, BLOCK_WOODEN_STAIRS, 0);
    gen_set_block(sc, ox+4, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block(sc, ox+5, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block(sc, ox+6, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+7, oy+1, oz+6, BLOCK_DOOR_WOOD, 3);
    gen_set_block(sc, ox+7, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block(sc, ox+8, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block(sc, ox+9, oy+1, oz+7, BLOCK_FENCE);
    gen_set_block(sc, ox+9, oy+1, oz+8, BLOCK_FENCE);
    gen_set_block(sc, ox+9, oy+1, oz+9, BLOCK_FENCE);
    gen_set_block(sc, ox+9, oy+1, oz+10, BLOCK_FENCE);
    gen_set_block(sc, ox+9, oy+1, oz+11, BLOCK_FENCE);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+2, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+0, oy+2, oz+5, BLOCK_TORCH, 2);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+3, BLOCK_DOOR_WOOD, 8);
    gen_set_block_with_meta(sc, ox+1, oy+2, oz+4, BLOCK_DOOR_WOOD, 8);
    gen_set_block(sc, ox+2, oy+2, oz+5, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+2, oz+1, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+3, oy+2, oz+2, BLOCK_STONE);
    gen_set_block(sc, ox+3, oy+2, oz+6, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+3, oy+2, oz+11, BLOCK_TORCH, 5);
    gen_set_block(sc, ox+4, oy+2, oz+6, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+5, oy+2, oz+0, BLOCK_TORCH, 4);
    gen_set_block(sc, ox+7, oy+2, oz+1, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+7, oy+2, oz+6, BLOCK_DOOR_WOOD, 11);
    gen_set_block(sc, ox+8, oy+2, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+9, oy+2, oz+3, BLOCK_GLASS_PANE);
    gen_set_block(sc, ox+9, oy+2, oz+4, BLOCK_GLASS_PANE);
    gen_set_block_with_meta(sc, ox+9, oy+2, oz+11, BLOCK_TORCH, 5);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+1, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+6, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+0, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+1, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+2, BLOCK_TORCH, 3);
    gen_set_block_with_meta(sc, ox+3, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+5, BLOCK_TORCH, 4);
    gen_set_block_with_meta(sc, ox+7, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block(sc, ox+8, oy+3, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+8, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+0, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+1, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+6, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+10, oy+3, oz+7, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+5, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+0, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+1, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block(sc, ox+8, oy+4, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+8, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+1, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+2, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+5, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+10, oy+4, oz+6, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+0, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+0, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+0, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+1, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+2, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+3, BLOCK_WOODEN_STAIRS, 7);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+4, BLOCK_WOODEN_STAIRS, 6);
    gen_set_block_with_meta(sc, ox+10, oy+5, oz+5, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+0, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+0, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+1, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+1, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+2, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+3, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+4, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+5, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+6, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+7, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+8, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block(sc, ox+8, oy+6, oz+5, BLOCK_COBBLESTONE);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+9, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+3, BLOCK_WOODEN_STAIRS, 2);
    gen_set_block_with_meta(sc, ox+10, oy+6, oz+4, BLOCK_WOODEN_STAIRS, 3);
    gen_set_block(sc, ox+8, oy+7, oz+5, BLOCK_COBBLESTONE);
    gen_set_block(sc, ox+8, oy+1, oz+5, BLOCK_FURNACE);
}





static void gen_fill_village_chest(struct server_chunk* sc, int lx, int ly, int lz,
                                    int64_t world_seed) {
	/* Pruefen ob an dieser Position eine Truhe ist */
	if(lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
	if(gen_get_block(sc, lx, ly, lz) != BLOCK_CHEST) return;
	/* Truhe mit zufaelligen Dorf-Items fuellen (vereinfacht) */
	/* In CavEX werden Truheninhalte per complex_block_archive gespeichert */
	/* Vorerst: Truhe bleibt leer -- Loot-System ist komplex */
	(void)world_seed;
}

static void gen_place_village_house(struct server_chunk* sc,
                                     int ox, int y, int oz,
                                     uint8_t planks, uint8_t cobble,
                                     bool has_library) {
	int i, j;
	/* Fundament (Kopfstein eine Ebene tief) */
	for(i = -2; i <= 2; i++)
		for(j = -1; j <= 5; j++)
			gen_set_block(sc, ox+i, y-1, oz+j, cobble);

	/* Boden (Holzplanken) */
	for(i = -2; i <= 2; i++)
		for(j = -1; j <= 5; j++)
			gen_set_block(sc, ox+i, y, oz+j, planks);

	/* Innenraum leeren */
	for(i = -1; i <= 1; i++)
		for(j = 0; j <= 4; j++)
			for(int h = 1; h <= 4; h++)
				gen_set_block(sc, ox+i, y+h, oz+j, BLOCK_AIR);

	/* Wände — 4 Blöcke hoch */
	for(int h = 1; h <= 4; h++) {
		for(i = -2; i <= 2; i++) {
			gen_set_block(sc, ox+i, y+h, oz-1, planks); /* Vorderwand */
			gen_set_block(sc, ox+i, y+h, oz+5, planks); /* Rückwand */
		}
		for(j = 0; j <= 4; j++) {
			gen_set_block(sc, ox-2, y+h, oz+j, planks); /* linke Wand */
			gen_set_block(sc, ox+2, y+h, oz+j, planks); /* rechte Wand */
		}
		/* Log-Ecksäulen an allen 4 Ecken */
		gen_set_block_with_meta(sc, ox-2, y+h, oz-1, BLOCK_LOG, 0);
		gen_set_block_with_meta(sc, ox+2, y+h, oz-1, BLOCK_LOG, 0);
		gen_set_block_with_meta(sc, ox-2, y+h, oz+5, BLOCK_LOG, 0);
		gen_set_block_with_meta(sc, ox+2, y+h, oz+5, BLOCK_LOG, 0);
	}

	/* Fenster (Glasscheiben) bei h=2 */
	gen_set_block(sc, ox-1, y+2, oz-1, BLOCK_GLASS_PANE); /* Vorderwand links */
	gen_set_block(sc, ox+1, y+2, oz-1, BLOCK_GLASS_PANE); /* Vorderwand rechts */
	gen_set_block(sc, ox, y+2, oz+5, BLOCK_GLASS_PANE);   /* Rückwand Mitte */
	gen_set_block(sc, ox-2, y+2, oz+2, BLOCK_GLASS_PANE); /* linke Wand */
	gen_set_block(sc, ox+2, y+2, oz+2, BLOCK_GLASS_PANE); /* rechte Wand */

	/* Tür (zweiteilig) — vorne Mitte */
	gen_set_block(sc, ox, y+1, oz-1, BLOCK_DOOR_WOOD);
	gen_set_block(sc, ox, y+2, oz-1, BLOCK_AIR);

	/* Dach mit Überhang (+1 Block) */
	for(i = -3; i <= 3; i++)
		for(j = -2; j <= 6; j++)
			gen_set_block(sc, ox+i, y+5, oz+j, planks);
	/* Dach-Treppe-Effekt: Plankenrand eine Ebene höher als Wände */
	for(i = -2; i <= 2; i++)
		for(j = -1; j <= 5; j++)
			gen_set_block(sc, ox+i, y+5, oz+j, planks);

	/* Einrichtung */
	gen_set_block_with_meta(sc, ox, y+1, oz+4, BLOCK_TORCH, 3); /* Fackel hinten: haengt an Rueckwand oz+5, zeigt ins Zimmer */
	gen_set_block(sc, ox-1, y+1, oz+3, BLOCK_WORKBENCH);
	if(has_library) {
		gen_set_block(sc, ox+1, y+1, oz+3, BLOCK_BOOKSHELF);
		gen_set_block(sc, ox-1, y+1, oz+4, BLOCK_BOOKSHELF);
	} else {
		gen_set_block(sc, ox+1, y+1, oz+3, BLOCK_CHEST);
		gen_set_block(sc, ox-1, y+1, oz+4, BLOCK_FURNACE);
	}
	/* Bett */
	gen_set_block(sc, ox, y+1, oz+1, BLOCK_BED);
}

/* MC-Farm: zwei Streifen Farmland mit Wasserkanal in der Mitte */
static void gen_place_mc_farm(struct server_chunk* sc, int cx, int y, int cz) {
	/* Farmland: 9 breit, 5 tief (z: -2..+2) mit Wasserkanal in der Mitte (z=0) */
	for(int x = -4; x <= 4; x++) {
		for(int z = -3; z <= 3; z++) {
			if(!gen_inside_chunk(cx+x, y, cz+z)) continue;
			if(x == 0) {
				/* Wasserkanal in der Mitte */
				gen_set_block(sc, cx+x, y, cz+z, BLOCK_WATER_STILL);
			} else {
				gen_set_block(sc, cx+x, y, cz+z, BLOCK_FARMLAND);
				if(gen_inside_chunk(cx+x, y+1, cz+z))
					gen_set_block(sc, cx+x, y+1, cz+z, BLOCK_CROPS);
			}
		}
	}
	/* Zaun-Rand */
	for(int x = -5; x <= 5; x++) {
		gen_set_block(sc, cx+x, y, cz-4, BLOCK_FENCE);
		gen_set_block(sc, cx+x, y, cz+4, BLOCK_FENCE);
	}
	for(int z = -4; z <= 4; z++) {
		gen_set_block(sc, cx-5, y, cz+z, BLOCK_FENCE);
		gen_set_block(sc, cx+5, y, cz+z, BLOCK_FENCE);
	}
	/* Eck-Fackeln */
	gen_set_block_with_meta(sc, cx-5, y+1, cz-4, BLOCK_TORCH, 5);
	gen_set_block_with_meta(sc, cx+5, y+1, cz-4, BLOCK_TORCH, 5);
}

static wgen_gen_t* server_world_wgen_gen(struct server_world* w); /* forward decl */
static int64_t gen_mc_legacy_next_long(uint64_t* s);              /* forward decl */
static int gen_mc_feat_int(uint64_t* s, int n);                   /* forward decl */

static bool gen_village_area_available(const bool* occupied,
									   int x0, int z0, int sx, int sz,
									   int margin) {
	for(int x = x0 - margin; x < x0 + sx + margin; x++) {
		if(x < 0 || x >= CHUNK_SIZE)
			return false;
		for(int z = z0 - margin; z < z0 + sz + margin; z++) {
			if(z < 0 || z >= CHUNK_SIZE)
				return false;
			if(occupied[x + z * CHUNK_SIZE])
				return false;
		}
	}
	return true;
}

static void gen_village_area_mark(bool* occupied,
								  int x0, int z0, int sx, int sz,
								  int margin) {
	for(int x = x0 - margin; x < x0 + sx + margin; x++) {
		if(x < 0 || x >= CHUNK_SIZE)
			continue;
		for(int z = z0 - margin; z < z0 + sz + margin; z++) {
			if(z < 0 || z >= CHUNK_SIZE)
				continue;
			occupied[x + z * CHUNK_SIZE] = true;
		}
	}
}

static void gen_village_set_path_at(struct server_chunk* sc, int lx, int lz,
									int target_y, uint8_t path_block,
									bool snowy_village) {
	if(lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE
	   || target_y < 1 || target_y >= WORLD_HEIGHT - 3)
		return;

	int surf = gen_chunk_actual_surface_height(sc, lx, lz);
	if(surf < 1)
		surf = target_y;
	if(snowy_village && gen_get_block(sc, lx, surf, lz) == BLOCK_SNOW)
		surf--;
	if(surf < 1)
		surf = target_y;

	if(surf > target_y) {
		for(int y = surf; y > target_y; y--)
			gen_set_block(sc, lx, y, lz, BLOCK_AIR);
	} else if(surf < target_y) {
		for(int y = surf + 1; y < target_y; y++)
			gen_set_block(sc, lx, y, lz, BLOCK_DIRT);
	}

	gen_set_block(sc, lx, target_y, lz, path_block);
	for(int y = target_y + 1; y <= target_y + 3 && y < WORLD_HEIGHT; y++)
		gen_set_block(sc, lx, y, lz, BLOCK_AIR);
}

static void gen_village_set_path_wave_at(struct server_chunk* sc, int lx, int lz,
										 int door_y, uint8_t path_block,
										 bool snowy_village) {
	for(int dx = -3; dx <= 3; dx++) {
		for(int dz = -2; dz <= 0; dz++) {
			int dist = abs(dx) + abs(dz);
			int px = lx + dx;
			int pz = lz + dz;
			if(px < 0 || px >= CHUNK_SIZE || pz < 0 || pz >= CHUNK_SIZE)
				continue;
			int normal_y = gen_chunk_actual_surface_height(sc, px, pz);
			if(normal_y > 0 && snowy_village
			   && gen_get_block(sc, px, normal_y, pz) == BLOCK_SNOW)
				normal_y--;
			if(normal_y < door_y)
				normal_y = door_y;
			int y = door_y + dist;
			if(y > normal_y)
				y = normal_y;
			gen_village_set_path_at(sc, px, pz, y, path_block, snowy_village);
		}
	}
}

static void gen_place_village(struct server_chunk* sc, struct server_world* w,
							  uint32_t seed,
							  int world_x0, int world_z0, int sea_level,
							  Generator* biome_gen) {
	wgen_gen_t* wg = server_world_wgen_gen(w);
	int cur_cx = world_x0 / CHUNK_SIZE;
	int cur_cz = world_z0 / CHUNK_SIZE;
	bool occupied[CHUNK_SIZE * CHUNK_SIZE] = {false};

	/* Dorf-Grid: 32×32 Chunks, Offset 0..23.
	 * Prüfe 3×3 benachbarte Grid-Zellen — jede Zelle kann genau ein Dorf haben. */
	int base_cell_x = (cur_cx >= 0 ? cur_cx : cur_cx - 31) / 32;
	int base_cell_z = (cur_cz >= 0 ? cur_cz : cur_cz - 31) / 32;

	for(int dcell_x = -1; dcell_x <= 1; dcell_x++) {
		for(int dcell_z = -1; dcell_z <= 1; dcell_z++) {
			int cell_x = base_cell_x + dcell_x;
			int cell_z = base_cell_z + dcell_z;

			/* Deterministischer Zell-RNG: worldSeed ^ cellHash */
			uint64_t cell_rng = (uint64_t)((int64_t)cell_x * 341873128712LL
			                  + (int64_t)cell_z * 132897987541LL
			                  + (int64_t)(int32_t)seed ^ 10387312LL)
			                  & 0xFFFFFFFFFFFFULL;
			cell_rng = (cell_rng ^ 25214903917LL) & 0xFFFFFFFFFFFFULL;

			/* Dorf-Zentrum: Zufalls-Offset 0..23 innerhalb der 32×32-Zelle */
			int vcx = cell_x * 32 + gen_mc_feat_int(&cell_rng, 24);
			int vcz = cell_z * 32 + gen_mc_feat_int(&cell_rng, 24);

			/* Zu weit vom aktuellen Chunk? (Dorf-Einfluss max. ~80 Blöcke = 5 Chunks) */
			if(abs(cur_cx - vcx) > 6 || abs(cur_cz - vcz) > 6) continue;

			/* Dorf-Zentrum in Weltkoordinaten */
			int v_world_x = vcx * CHUNK_SIZE + 8;
			int v_world_z = vcz * CHUNK_SIZE + 8;

			/* Terrain-Höhe am Dorf-Zentrum direkt aus Noise */
			int cy = wgen_gen_world_height(wg, v_world_x, v_world_z);
			if(cy <= sea_level + 2) continue;

			int biome_id = gen_biome_at_safe(biome_gen, v_world_x, v_world_z);
			bool is_desert = (biome_id == desert || biome_id == desert_hills);
			bool is_taiga  = (biome_id == taiga  || biome_id == taiga_hills);
			bool is_snowy_village = isSnowy(biome_id);

			uint8_t wall = is_desert ? BLOCK_SANDSTONE : BLOCK_PLANKS;
			uint8_t path_block = is_desert ? BLOCK_SANDSTONE : BLOCK_GRAVEL;
			uint8_t cobble_base = is_desert ? BLOCK_SANDSTONE : BLOCK_COBBLESTONE;

			/* Konvertiere Weltkoordinaten -> lokale Chunk-Koordinaten
			 * (koennen negativ oder > 15 sein - gen_set_block clippt automatisch) */
			int ox = v_world_x - world_x0;
			int oz = v_world_z - world_z0;

			/* Organisches Dorf-Layout:
			 * Straßen wachsen in 2-4 Richtungen vom Brunnen, zufällige Länge,
			 * Gebäude hängen an Straßenseiten mit zufälligen Abständen. */

			/* Layout-RNG aus Dorf-Position und Welt-Seed */
			uint64_t vrng = (uint64_t)((int64_t)vcx * 341873128712LL
			              + (int64_t)vcz * 132897987541LL + (int64_t)(int32_t)seed)
			              & 0xFFFFFFFFFFFFULL;
			vrng = (vrng ^ 25214903917LL) & 0xFFFFFFFFFFFFULL;

			/* Brunnen exakt am Dorf-Zentrum */
			int well_x = ox;
			int well_z = oz;
			if(well_x >= 0 && well_x < CHUNK_SIZE && well_z >= 0 && well_z < CHUNK_SIZE) {
				/* Minimum-Hoehe unter dem Brunnen-Footprint (9×9) → kein Schweben */
				int well_min = WORLD_HEIGHT;
				for(int dx2 = 0; dx2 < 9; dx2++)
					for(int dz2 = 0; dz2 < 9; dz2++) {
						int flx = well_x+dx2, flz = well_z+dz2;
						if(flx<0||flx>=CHUNK_SIZE||flz<0||flz>=CHUNK_SIZE) continue;
						int h = gen_chunk_actual_surface_height(sc, flx, flz);
						if(h > sea_level && h < well_min) well_min = h;
				}
				int well_cy = (well_min < WORLD_HEIGHT) ? well_min : 0;
				if(well_cy >= 2 && well_cy > sea_level + 2) {
					if(is_desert)
						gen_place_desert_well(sc, well_x, well_cy, well_z);
					else
						gen_place_plains_fountain(sc, well_x, well_cy - 1, well_z);
					gen_village_area_mark(occupied, well_x, well_z, 9, 9, 1);
				}
			}

			/* Straßen-Richtungen: 4 Himmelsrichtungen (N/S/E/W).
			 * dx/dz pro Richtung: 0=Nord, 1=Süd, 2=Ost, 3=West */
			static const int road_dx[4] = { 0,  0,  1, -1};
			static const int road_dz[4] = {-1,  1,  0,  0};

			/* 4 Straßen-Arme */
			for(int arm = 0; arm < 4; arm++) {
				/* Strassen-Laenge: 7 * (3..5) = 21, 28 oder 35 Bloecke */
				int arm_len = 7 * (3 + gen_mc_feat_int(&vrng, 3));
				int adx = road_dx[arm];
				int adz = road_dz[arm];

				/* Strasse entlang des Arms platzieren (3 Bloecke breit) */
				for(int step = 1; step <= arm_len; step++) {
					int wx = v_world_x + adx * step;
					int wz = v_world_z + adz * step;
					for(int rw = -1; rw <= 1; rw++) {
						int lx = wx - world_x0 + (adz != 0 ? rw : 0);
						int lz = wz - world_z0 + (adx != 0 ? rw : 0);
						if(lx < 0||lx>=CHUNK_SIZE||lz<0||lz>=CHUNK_SIZE) continue;
						int surf = gen_chunk_actual_surface_height(sc, lx, lz);
						if(surf < 1) continue;
						if(is_snowy_village && surf > 1
						   && gen_get_block(sc, lx, surf, lz) == BLOCK_SNOW) {
							gen_set_block(sc, lx, surf, lz, BLOCK_AIR);
							gen_set_block(sc, lx, surf - 1, lz, path_block);
							surf--;
						} else {
							gen_set_block(sc, lx, surf, lz, path_block);
						}
						for(int d = 1; d <= 2; d++) {
							if(surf - d < 1) break;
							uint8_t below = gen_get_block(sc, lx, surf-d, lz);
							if(below==BLOCK_DIRT||below==BLOCK_GRASS||below==BLOCK_GRAVEL)
								gen_set_block(sc, lx, surf-d, lz, path_block);
							else break;
						}
					}
				}

				/* Gebaeude entlang des Arms.
				 * Haustypen: 0=(5×5), 1=(4×5), 2=(10×7), 3=(9×12), 4=(5×9) */
				int house_sx_t[] = {5, 4, 10, 9, 5};
				int house_sz_t[] = {5, 5,  7,12, 9};
				int house_h_t[]  = {6, 6,  6, 7,12}; /* Bauhoehe (uz=Turm=12) */
				int house_door_x_t[] = {2, 1, 7, 2, 2};

				for(int pos = gen_mc_feat_int(&vrng, 5) + 4;
				    pos <= arm_len - 4;
				    pos += 2 + gen_mc_feat_int(&vrng, 5)) {

					/* Gebaeude auf linker oder rechter Strassenseite */
					for(int side = -1; side <= 1; side += 2) {
						if(adz != 0)
							continue; /* Haus-Templates zeigen nur nach Norden. */
						if(side < 0)
							continue; /* Nur suedlich der Ost/West-Strasse passt die Front. */
						if(gen_mc_feat_int(&vrng, 4) == 0) continue; /* 75% Chance */

						/* Gebaeude-Typ zufaellig (0=uy,1=wi,2=ko,3=tf,4=uz) */
						int htype = gen_mc_feat_int(&vrng, 5);

						/* Gebaeude so setzen, dass der echte Eingang nicht auf der
						 * Strasse steht und der Seitenweg direkt zur Tuer fuehrt.
						 * Alle MC10-Haeuser haben ihre Front an z=0. */
						int sx = house_sx_t[htype], sz = house_sz_t[htype];
						int door_x = house_door_x_t[htype];
						int road_wx = v_world_x + adx * pos;
						int road_wz = v_world_z + adz * pos;
						int h_wx = road_wx;
						int h_wz = road_wz;
						h_wx = road_wx - door_x;
						h_wz = road_wz + 3;

						int hx = h_wx - world_x0;
						int hz = h_wz - world_z0;

						/* Nur platzieren wenn GESAMTER Footprint im aktuellen Chunk liegt.
						 * Verhindert halbe Haeuser an Chunk-Grenzen. */
						if(hx < 0 || hx + sx > CHUNK_SIZE
						   || hz < 0 || hz + sz > CHUNK_SIZE) continue;
						if(!gen_village_area_available(occupied, hx, hz, sx, sz, 0))
							continue;

						/* Durchschnittliche Terrain-Höhe unter dem Haus
						 * (Summe / Anzahl).
						 * Kein Kopfstein-Fundament — Haus sitzt auf dem Terrain. */
						int sum_h = 0, cnt_h = 0;
						int min_h = WORLD_HEIGHT, max_h = 0;
						for(int dx2 = 0; dx2 < sx; dx2++)
							for(int dz2 = 0; dz2 < sz; dz2++) {
								int flx = hx+dx2, flz = hz+dz2;
								if(flx<0||flx>=CHUNK_SIZE||flz<0||flz>=CHUNK_SIZE) continue;
								int h = gen_chunk_actual_surface_height(sc, flx, flz);
								if(h > sea_level) {
									sum_h += h; cnt_h++;
									if(h < min_h) min_h = h;
									if(h > max_h) max_h = h;
								}
							}
						if(cnt_h == 0) continue;
						/* MINIMUM statt Durchschnitt: Haus beginnt immer oberhalb
						 * des tiefsten Terrain-Punkts → keine versunkenen Wände. */
						int house_cy = min_h;
						if(house_cy <= sea_level + 2) continue;
						/* Schlucht: Höhenvariation >14 Blöcke → überspringen */
						if(max_h - min_h > 14) continue;

						/* Seitenweg zur Tuer auf Tuerschwellen-Hoehe. */
						int door_wx = h_wx + door_x;
						int door_wz = h_wz - 1;
						int road_y = gen_chunk_actual_surface_height(
							sc, door_wx - world_x0, road_wz - world_z0);
						bool entrance_needs_stair = (road_y > 0 && road_y < house_cy);
						gen_village_set_path_wave_at(sc, door_wx - world_x0,
													 door_wz - world_z0,
													 house_cy, path_block,
													 is_snowy_village);

						/* Gesamten Bauraum freischaufeln: Footprint (+1 Rand) ueber die
						 * volle Haushoehe leeren, damit nichts im Hang steckt (kein
						 * Turm-unter-der-Erde mehr). Rand vorne (oz-1) fuer den Eingang. */
						int clear_h = house_h_t[htype];
						for(int clx = hx-1; clx <= hx+sx; clx++) {
							if(clx<0||clx>=CHUNK_SIZE) continue;
							for(int clz = hz-1; clz <= hz+sz; clz++) {
								if(clz<0||clz>=CHUNK_SIZE) continue;
								for(int cly = house_cy+1; cly <= house_cy+clear_h && cly < WORLD_HEIGHT; cly++) {
									uint8_t b = gen_get_block(sc, clx, cly, clz);
									if(b != BLOCK_AIR && b != BLOCK_WATER_STILL
									   && b != BLOCK_WATER_FLOW)
										gen_set_block(sc, clx, cly, clz, BLOCK_AIR);
								}
							}
						}

						/* Haus platzieren */
						if(!is_desert) {
							switch(htype) {
							case 0: gen_mc10_house_uy(sc, hx, house_cy, hz); break;
							case 1: gen_mc10_house_wi(sc, hx, house_cy, hz); break;
							case 2: gen_mc10_house_ko(sc, hx, house_cy, hz); break;
							case 3: gen_mc10_house_tf(sc, hx, house_cy, hz); break;
							case 4: gen_mc10_house_uz(sc, hx, house_cy, hz); break;
							}
							if(entrance_needs_stair)
								gen_set_block_with_meta(sc, door_wx - world_x0, house_cy,
														door_wz - world_z0,
														BLOCK_STONE_STAIRS, 2);
						} else {
							gen_place_village_house(sc, hx, house_cy, hz, wall,
							                        cobble_base, (htype >= 3));
						}
						gen_village_area_mark(occupied, hx, hz, sx, sz, 1);
					}
				}

				/* Laternen nach den Haeusern platzieren: wenn ein Haus dort steht,
				 * verschwindet die Laterne komplett statt halb abgeschnitten zu sein. */
				for(int step = 8; step <= arm_len; step += 8) {
					int wx = v_world_x + adx * step;
					int wz = v_world_z + adz * step;
					for(int side = -1; side <= 1; side += 2) {
						int lx = wx - world_x0 + (adz != 0 ? side * 2 : 0);
						int lz = wz - world_z0 + (adx != 0 ? side * 2 : 0);
						if(!gen_village_area_available(occupied, lx - 1, lz - 1, 3, 3, 0))
							continue;
						int surf = gen_chunk_actual_surface_height(sc, lx, lz);
						if(surf >= 1) {
							int lamp_y = is_snowy_village ? surf : surf + 1;
							gen_place_plains_lamp(sc, lx, lamp_y, lz);
							gen_village_area_mark(occupied, lx - 1, lz - 1, 3, 3, 0);
						}
					}
				}

				/* Farm am Ende des laengsten Arms */
				if(arm == 0) {
					int farm_wx = v_world_x + adx * (arm_len + 8);
					int farm_wz = v_world_z + adz * (arm_len + 8);
					int farm_x = farm_wx - world_x0 - 6; /* -6 zentriert */
					int farm_z = farm_wz - world_z0 - 6;
					if(farm_x >= 0 && farm_x < CHUNK_SIZE
					   && farm_z >= 0 && farm_z < CHUNK_SIZE) {
						if(!gen_village_area_available(occupied, farm_x, farm_z, 13, 9, 0))
							continue;
						/* Durchschnittshöhe unter der Farm */
					int farm_sum = 0, farm_cnt = 0;
					for(int dx2 = 0; dx2 < 13; dx2++)
						for(int dz2 = 0; dz2 < 9; dz2++) {
							int flx = farm_x+dx2, flz = farm_z+dz2;
							if(flx<0||flx>=CHUNK_SIZE||flz<0||flz>=CHUNK_SIZE) continue;
							int h = gen_chunk_actual_surface_height(sc, flx, flz);
							if(h > sea_level) { farm_sum += h; farm_cnt++; }
						}
					if(farm_cnt > 0) {
						int farm_cy = farm_sum / farm_cnt;
						if(farm_cy >= 3 && farm_cy > sea_level + 2) {
							/* Bauraum ueber der Farm freischaufeln (13×9 × 4 hoch) */
							for(int dx2 = -1; dx2 <= 13; dx2++) {
								int flx = farm_x+dx2;
								if(flx<0||flx>=CHUNK_SIZE) continue;
								for(int dz2 = -1; dz2 <= 9; dz2++) {
									int flz = farm_z+dz2;
									if(flz<0||flz>=CHUNK_SIZE) continue;
									for(int cly = farm_cy+1; cly <= farm_cy+4 && cly < WORLD_HEIGHT; cly++) {
										uint8_t b = gen_get_block(sc, flx, cly, flz);
										if(b != BLOCK_AIR && b != BLOCK_WATER_STILL
										   && b != BLOCK_WATER_FLOW)
											gen_set_block(sc, flx, cly, flz, BLOCK_AIR);
									}
								}
							}
							for(int dx2 = 0; dx2 < 13; dx2++) {
								int flx = farm_x + dx2;
								if(flx < 0 || flx >= CHUNK_SIZE) continue;
								for(int dz2 = 0; dz2 < 9; dz2++) {
									int flz = farm_z + dz2;
									if(flz < 0 || flz >= CHUNK_SIZE) continue;
									int h = gen_chunk_actual_surface_height(sc, flx, flz);
									if(h > farm_cy) {
										for(int y = h; y > farm_cy; y--)
											gen_set_block(sc, flx, y, flz, BLOCK_AIR);
									} else if(h < farm_cy) {
										for(int y = h + 1; y < farm_cy; y++)
											gen_set_block(sc, flx, y, flz, BLOCK_DIRT);
									}
								}
							}
							gen_place_plains_large_farm(sc, farm_x, farm_cy, farm_z);
							gen_village_area_mark(occupied, farm_x, farm_z, 13, 9, 1);
						}
					}
					}
				}
			}
		}
	}
}

static void gen_place_ice_surfaces(struct server_chunk* sc, uint32_t seed,
								   int world_x0, int world_z0, int sea_level,
								   Generator* biome_gen) {
	(void)seed;
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			int wx = world_x0 + lx;
			int wz = world_z0 + lz;
			int biome_id = gen_biome_at_safe(biome_gen, wx, wz);
			if(!isSnowy(biome_id))
				continue;
			for(int y = sea_level + 1; y >= sea_level - 2; y--) {
				if(!gen_inside_chunk(lx, y, lz))
					continue;
				if(gen_get_block(sc, lx, y, lz) == BLOCK_WATER_STILL
				   && gen_get_block(sc, lx, y + 1, lz) == BLOCK_AIR) {
					gen_set_block(sc, lx, y, lz, BLOCK_ICE);
					break;
				}
			}
		}
	}
}

static void gen_place_flower_clumps(struct server_chunk* sc, uint32_t seed,
									int world_x0, int world_z0,
									Generator* biome_gen) {
	int clumps = (int)(gen_hash3i(world_x0, 1701, world_z0, seed ^ 0xF1043001U) % 3U);
	for(int c = 0; c < clumps; c++) {
		int cx = 2 + (int)(gen_hash3i(world_x0, 1709 + c * 7, world_z0,
									  seed ^ 0xF1043002U) % 12U);
		int cz = 2 + (int)(gen_hash3i(world_x0, 1717 + c * 11, world_z0,
									  seed ^ 0xF1043003U) % 12U);
		int center_y = gen_chunk_actual_surface_height(sc, cx, cz);
		if(center_y < 1)
			continue;
		int biome_id = gen_biome_at_safe(biome_gen, world_x0 + cx, world_z0 + cz);
		if(!(biome_id == plains || biome_id == flower_forest || biome_id == forest
			 || biome_id == birch_forest))
			continue;

		for(int dx = -3; dx <= 3; dx++) {
			for(int dz = -3; dz <= 3; dz++) {
				if(dx * dx + dz * dz > 9)
					continue;
				int x = cx + dx;
				int z = cz + dz;
				if(x < 1 || x >= CHUNK_SIZE - 1 || z < 1 || z >= CHUNK_SIZE - 1)
					continue;
				int y = gen_chunk_actual_surface_height(sc, x, z);
				if(y < 1)
					continue;
				if(gen_get_block(sc, x, y, z) != BLOCK_GRASS
				   || gen_get_block(sc, x, y + 1, z) != BLOCK_AIR)
					continue;
				uint32_t h = gen_hash3i(world_x0 + x, y, world_z0 + z,
										seed ^ 0xF1043004U);
				if((h % 100U) >= 35U)
					continue;
				gen_set_block(sc, x, y + 1, z, ((h >> 8) & 1U) ? BLOCK_FLOWER : BLOCK_ROSE);
			}
		}
	}
}

static void gen_place_water_lake(struct server_chunk* sc, uint32_t seed,
								 int world_x0, int world_z0, int sea_level,
								 int probability) {
	if(probability <= 0)
		return;
	if((gen_hash3i(world_x0, 37, world_z0, seed ^ 0x7A7E1234U)
		% (uint32_t)probability) != 0U)
		return;

	int cx = 6 + (int)(gen_hash3i(world_x0, 41, world_z0, seed ^ 0x6A4E1001U) % 4U);
	int cz = 6 + (int)(gen_hash3i(world_x0, 43, world_z0, seed ^ 0x6A4E1002U) % 4U);
	int surface = gen_chunk_actual_surface_height(sc, cx, cz);
	if(surface <= sea_level + 2)
		return;

	/* Teich höher: Wasseroberfläche nur 1 Block unter dem Terrain (statt 3-4),
	 * damit der Teich nicht tief in einer Grube versinkt. */
	int water_y = surface - 1;
	if(water_y < sea_level + 1)
		water_y = sea_level + 1;
	if(water_y >= WORLD_HEIGHT - 5)
		return;

	int rx = 4 + (int)(gen_hash3i(world_x0, 53, world_z0, seed ^ 0x6A4E1004U) % 2U);
	int rz = 4 + (int)(gen_hash3i(world_x0, 59, world_z0, seed ^ 0x6A4E1005U) % 2U);
	int max_r = (rx > rz) ? rx : rz;

	for(int x = cx - max_r - 1; x <= cx + max_r + 1; x++) {
		for(int z = cz - max_r - 1; z <= cz + max_r + 1; z++) {
			if(!gen_inside_chunk(x, water_y, z))
				return;
		}
	}

	for(int x = cx - max_r - 1; x <= cx + max_r + 1; x++) {
		for(int z = cz - max_r - 1; z <= cz + max_r + 1; z++) {
			if(!gen_inside_chunk(x, water_y, z))
				continue;

			float dx = (float)(x - cx) / (float)rx;
			float dz = (float)(z - cz) / (float)rz;
			float dist = dx * dx + dz * dz;
			if(dist > 2.25f)
				continue;

			int top = gen_chunk_actual_surface_height(sc, x, z);
			if(top < 1)
				continue;

			/* Wände flacher absenken: der Rand steigt nur sanft an, damit keine
			 * hohen Steilwände um den Teich stehen bleiben. */
			int target_top;
			if(dist <= 1.0f) {
				target_top = water_y;
			} else if(dist <= 1.55f) {
				target_top = water_y + (int)((dist - 1.0f) * 2.0f);
			} else {
				target_top = water_y + 1 + (int)((dist - 1.55f) * 2.0f);
			}
			if(target_top > top)
				target_top = top;

			for(int y = top; y > target_top && y < WORLD_HEIGHT; y--)
				gen_set_block(sc, x, y, z, BLOCK_AIR);

			if(dist <= 1.0f) {
				int depth = 2 + (int)((1.0f - dist) * 3.0f);
				int bottom = water_y - depth;
				if(bottom < 1)
					bottom = 1;
				for(int y = bottom; y <= water_y; y++)
					gen_set_block(sc, x, y, z, BLOCK_WATER_STILL);
				if(bottom > 1)
					gen_set_block(sc, x, bottom - 1, z, BLOCK_DIRT);
			} else {
				uint8_t top_block = gen_get_block(sc, x, target_top, z);
				if(top_block == BLOCK_STONE || gen_is_ore_block(top_block))
					gen_set_block(sc, x, target_top, z, BLOCK_GRASS);
				for(int y = target_top - 1; y >= target_top - 3 && y > 0; y--) {
					uint8_t b = gen_get_block(sc, x, y, z);
					if(b == BLOCK_STONE || gen_is_ore_block(b))
						gen_set_block(sc, x, y, z, BLOCK_DIRT);
				}
			}
		}
	}

	/* Erdwall direkt um die Wasserfläche: wenn der See höher liegt als ein
	 * Randblock, darf Wasser nicht seitlich über die Kante laufen. */
	for(int x = cx - max_r - 1; x <= cx + max_r + 1; x++) {
		for(int z = cz - max_r - 1; z <= cz + max_r + 1; z++) {
			if(!gen_inside_chunk(x, water_y + 1, z))
				continue;
			float dx = (float)(x - cx) / (float)rx;
			float dz = (float)(z - cz) / (float)rz;
			float dist = dx * dx + dz * dz;
			if(dist <= 1.0f || dist > 1.42f)
				continue;

			int bank_y = water_y + 1;
			int top = gen_chunk_actual_surface_height(sc, x, z);
			if(top > bank_y) {
				for(int y = top; y > bank_y && y < WORLD_HEIGHT; y--)
					gen_set_block(sc, x, y, z, BLOCK_AIR);
			} else {
				for(int y = top + 1; y < bank_y; y++)
					gen_set_block(sc, x, y, z, BLOCK_DIRT);
			}
			gen_set_block(sc, x, bank_y, z, BLOCK_GRASS);
			if(bank_y > 0)
				gen_set_block(sc, x, bank_y - 1, z, BLOCK_DIRT);
			for(int y = bank_y + 1; y <= bank_y + 2 && y < WORLD_HEIGHT; y++)
				gen_set_block(sc, x, y, z, BLOCK_AIR);
		}
	}
}

static void gen_place_water_springs(struct server_chunk* sc, uint32_t seed,
									int world_x0, int world_z0, int sea_level) {
	for(int i = 0; i < 4; i++) {
		int x = 2 + (int)(gen_hash3i(world_x0, 401 + i, world_z0, seed ^ 0x57121001U) % 12U);
		int z = 2 + (int)(gen_hash3i(world_x0, 501 + i, world_z0, seed ^ 0x57121002U) % 12U);
		int surface = gen_chunk_actual_surface_height(sc, x, z);
		if(surface < 18)
			continue;

		int max_y = surface - 4;
		if(max_y > sea_level - 2)
			max_y = sea_level - 2;
		if(max_y < 8)
			continue;

		int y = 8 + (int)(gen_hash3i(world_x0, 601 + i, world_z0, seed ^ 0x57121003U)
			% (uint32_t)(max_y - 7));
		if(!gen_inside_chunk(x, y, z))
			continue;
		if(gen_get_block(sc, x, y, z) != BLOCK_AIR)
			continue;

		int solid = 0;
		int open = 0;
		if(gen_is_opaque(gen_get_block(sc, x + 1, y, z))) solid++; else open++;
		if(gen_is_opaque(gen_get_block(sc, x - 1, y, z))) solid++; else open++;
		if(gen_is_opaque(gen_get_block(sc, x, y, z + 1))) solid++; else open++;
		if(gen_is_opaque(gen_get_block(sc, x, y, z - 1))) solid++; else open++;
		if(gen_is_opaque(gen_get_block(sc, x, y - 1, z))) solid++;
		if(gen_is_opaque(gen_get_block(sc, x, y + 1, z))) solid++;

		if(solid >= 5 && open == 1)
			gen_set_block(sc, x, y, z, BLOCK_WATER_STILL);
	}
}

static void gen_place_lava_lake(struct server_chunk* sc, uint32_t seed,
								int world_x0, int world_z0, int probability) {
	if(probability <= 0)
		return;
	if((gen_hash3i(world_x0, 73, world_z0, seed ^ 0x1A7A1A7AU)
		% (uint32_t)probability) != 0U)
		return;

	int cx = 4 + (int)(gen_hash3i(world_x0, 79, world_z0, seed ^ 0x1A7A2001U) % 8U);
	int cz = 4 + (int)(gen_hash3i(world_x0, 83, world_z0, seed ^ 0x1A7A2002U) % 8U);
	int surface = gen_chunk_actual_surface_height(sc, cx, cz);
	if(surface < 18)
		return;

	int max_center_y = surface - 8;
	if(max_center_y > 28)
		max_center_y = 28;
	if(max_center_y < 8)
		return;

	int cy = 8 + (int)(gen_hash3i(world_x0, 89, world_z0, seed ^ 0x1A7A2003U)
		% (uint32_t)(max_center_y - 7));
	int rx = 2 + (int)(gen_hash3i(world_x0, 97, world_z0, seed ^ 0x1A7A2004U) % 2U);
	int rz = 2 + (int)(gen_hash3i(world_x0, 101, world_z0, seed ^ 0x1A7A2005U) % 2U);
	int ry = 2 + (int)(gen_hash3i(world_x0, 103, world_z0, seed ^ 0x1A7A2006U) % 2U);

	for(int x = cx - rx - 1; x <= cx + rx + 1; x++) {
		for(int z = cz - rz - 1; z <= cz + rz + 1; z++) {
			if(!gen_inside_chunk(x, cy, z))
				return;
			int top = gen_chunk_actual_surface_height(sc, x, z);
			if(top < cy + ry + 3)
				return;
		}
	}

	for(int x = cx - rx; x <= cx + rx; x++) {
		for(int z = cz - rz; z <= cz + rz; z++) {
			for(int y = cy - ry; y <= cy + ry; y++) {
				float dx = (float)(x - cx) / (float)rx;
				float dy = (float)(y - cy) / (float)ry;
				float dz = (float)(z - cz) / (float)rz;
				float dist = dx * dx + dy * dy + dz * dz;
				if(dist > 1.0f)
					continue;

				uint8_t here = gen_get_block(sc, x, y, z);
				if(!gen_is_opaque(here) && here != BLOCK_AIR)
					continue;

				if(y <= cy) {
					gen_set_block(sc, x, y, z, BLOCK_LAVA_STILL);
				} else {
					gen_set_block(sc, x, y, z, BLOCK_AIR);
				}
			}
		}
	}
}

static bool gen_is_local_fluid(uint8_t block) {
	return block == BLOCK_WATER_STILL || block == BLOCK_WATER_FLOW
		|| block == BLOCK_LAVA_STILL || block == BLOCK_LAVA_FLOW;
}

/* Wasser-Fluss wird nach dem Laden über das echte onWorldTick-System
 * angestossen (server_local weckt die Wasserzellen des Chunks), nicht beim
 * Generieren simuliert. Siehe server_local_settle_chunk_water(). */
static void gen_presimulate_fluids_local(struct server_chunk* sc,
										 bool enable_water, bool enable_lava) {
	(void)sc; (void)enable_water; (void)enable_lava;
}

static void gen_place_lava_sources(struct server_chunk* sc, uint32_t seed,
								   int world_x0, int world_z0) {
	for(int i = 0; i < 3; i++) {
		int x = 1 + (int)(gen_rand01_from_hash(
							  gen_hash3i(world_x0, 100 + i, world_z0,
										 seed ^ 0x1A7A0000U))
						  * 14.0f);
		int z = 1 + (int)(gen_rand01_from_hash(
							  gen_hash3i(world_x0, 200 + i, world_z0,
										 seed ^ 0x1A7A0001U))
						  * 14.0f);
		int y = 6 + (int)(gen_rand01_from_hash(
							  gen_hash3i(world_x0, 300 + i, world_z0,
										 seed ^ 0x1A7A0002U))
						  * 18.0f);

		if(!gen_inside_chunk(x, y, z))
			continue;
		if(gen_get_block(sc, x, y, z) != BLOCK_AIR)
			continue;

		int solid = 0;
		if(gen_is_opaque(gen_get_block(sc, x + 1, y, z)))
			solid++;
		if(gen_is_opaque(gen_get_block(sc, x - 1, y, z)))
			solid++;
		if(gen_is_opaque(gen_get_block(sc, x, y, z + 1)))
			solid++;
		if(gen_is_opaque(gen_get_block(sc, x, y, z - 1)))
			solid++;
		if(gen_is_opaque(gen_get_block(sc, x, y - 1, z)))
			solid++;
		if(solid >= 4)
			gen_set_block(sc, x, y, z, BLOCK_LAVA_STILL);
	}
}

/* BFS-Lichtausbreitung innerhalb eines frisch generierten Chunks.
 *
 * Zwei Probleme mit gen_recompute_height_and_skylight allein:
 *   1. Fackelllicht (Lava, Glowstone, Fackeln) wird überhaupt nicht gesetzt.
 *   2. Himmelslicht breitet sich nicht seitlich in Höhlenöffnungen aus.
 *
 * Diese Funktion arbeitet direkt auf den Roh-Nibble-Arrays des Chunk-Buffers
 * (vor dict_server_chunks_set_at) und benötigt keine World-API.
 * Repeated-Sweep-BFS: maximal 15 Durchläufe, bricht früh ab sobald sich
 * nichts mehr ändert (in der Praxis 1–3 Durchläufe). */
static void gen_propagate_lighting(struct server_chunk* sc, bool is_nether) {
	static const int8_t nbx[6] = {1,-1,0,0,0,0};
	static const int8_t nby[6] = {0,0,1,-1,0,0};
	static const int8_t nbz[6] = {0,0,0,0,1,-1};

	/* ---- Fackelllicht: Seed ---- */
	for(int lx = 0; lx < CHUNK_SIZE; lx++)
		for(int lz = 0; lz < CHUNK_SIZE; lz++)
			for(int y = 0; y < WORLD_HEIGHT; y++) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				uint8_t type = sc->ids[idx];
				uint8_t lum  = (blocks[type] && blocks[type]->luminance)
				               ? blocks[type]->luminance : 0;
				if(lum)
					gen_set_nibble(sc->lighting_torch, idx, lum);
			}

	/* ---- Fackelllicht: Ausbreitung (Repeated Sweep) ---- */
	for(int pass = 0; pass < 15; pass++) {
		bool changed = false;
		for(int lx = 0; lx < CHUNK_SIZE; lx++)
			for(int lz = 0; lz < CHUNK_SIZE; lz++)
				for(int y = 0; y < WORLD_HEIGHT; y++) {
					size_t idx   = S_CHUNK_IDX(lx, y, lz);
					uint8_t type = sc->ids[idx];
					/* Undurchsichtige Blöcke empfangen/senden kein Licht */
					if(blocks[type] && !blocks[type]->can_see_through) continue;

					uint8_t opc  = (blocks[type] && blocks[type]->opacity > 1)
					               ? (uint8_t)blocks[type]->opacity : 1;
					uint8_t best = gen_get_nibble(sc->lighting_torch, idx);

					for(int d = 0; d < 6; d++) {
						int nx = lx + nbx[d], ny = y + nby[d], nz = lz + nbz[d];
						if(!gen_inside_chunk(nx, ny, nz)) continue;
						uint8_t nt = gen_get_nibble(
						    sc->lighting_torch, S_CHUNK_IDX(nx, ny, nz));
						if(nt > opc && nt - opc > best) best = nt - opc;
					}
					if(best != gen_get_nibble(sc->lighting_torch, idx)) {
						gen_set_nibble(sc->lighting_torch, idx, best);
						changed = true;
					}
				}
		if(!changed) break;
	}

	if(is_nether) return; /* Nether: kein Himmelslicht */

	/* ---- Himmelslicht: seitliche Ausbreitung ---- */
	for(int pass = 0; pass < 15; pass++) {
		bool changed = false;
		for(int lx = 0; lx < CHUNK_SIZE; lx++)
			for(int lz = 0; lz < CHUNK_SIZE; lz++)
				for(int y = 0; y < WORLD_HEIGHT; y++) {
					size_t idx   = S_CHUNK_IDX(lx, y, lz);
					uint8_t type = sc->ids[idx];
					if(blocks[type] && !blocks[type]->can_see_through) continue;

					uint8_t opc  = (blocks[type] && blocks[type]->opacity > 1)
					               ? (uint8_t)blocks[type]->opacity : 1;
					uint8_t best = gen_get_nibble(sc->lighting_sky, idx);

					for(int d = 0; d < 6; d++) {
						int nx = lx + nbx[d], ny = y + nby[d], nz = lz + nbz[d];
						if(!gen_inside_chunk(nx, ny, nz)) continue;
						uint8_t ns = gen_get_nibble(
						    sc->lighting_sky, S_CHUNK_IDX(nx, ny, nz));
						if(ns > opc && ns - opc > best) best = ns - opc;
					}
					if(best != gen_get_nibble(sc->lighting_sky, idx)) {
						gen_set_nibble(sc->lighting_sky, idx, best);
						changed = true;
					}
				}
		if(!changed) break;
	}
}

static void gen_recompute_height_and_skylight(struct server_chunk* sc) {
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			uint8_t column_height = 0;
			uint8_t light = 15;
			for(int y = WORLD_HEIGHT - 1; y >= 0; y--) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				uint8_t block = sc->ids[idx];
				if(column_height == 0 && block != BLOCK_AIR
				   && block != BLOCK_WATER_FLOW
				   && block != BLOCK_WATER_STILL)
					column_height = (uint8_t)(y + 1);
				gen_set_nibble(sc->lighting_sky, idx, light);
				if(light == 0)
					continue;
				if(gen_is_opaque(block)) {
					light = 0;
				} else if(blocks[block] && blocks[block]->opacity > 0) {
					uint8_t opacity = blocks[block]->opacity;
					light = (opacity >= light) ? 0 : (uint8_t)(light - opacity);
				}
			}
			sc->heightmap[lx + lz * CHUNK_SIZE] = column_height;
		}
	}
}

static bool gen_is_ore_block(uint8_t block) {
	return (block == BLOCK_COAL_ORE)
		|| (block == BLOCK_IRON_ORE)
		|| (block == BLOCK_GOLD_ORE)
		|| (block == BLOCK_DIAMOND_ORE)
		|| (block == BLOCK_LAPIS_ORE)
		|| (block == BLOCK_EMERALD_ORE);
}

static int gen_mc_feat_int(uint64_t* s, int n); /* forward decl */

/* Platziert eine Erz-Ader (Vein): eine rotierte Linie mit kugelförmigen
 * Klumpen entlang ihr — so entstehen zusammenhängende Erz-Massen (viele
 * Blöcke auf einmal) statt einzeln verstreuter Erze, wie in Minecraft. */
static void gen_place_ore_vein(struct server_chunk* sc, float cx, float cy, float cz,
                               int count, uint8_t ore, uint64_t* rng) {
	if(count < 1) count = 1;
	float angle = (float)gen_mc_feat_int(rng, 628) / 100.0f;
	float len = (float)count / 8.0f;
	float x1 = cx + sinf(angle) * len, x2 = cx - sinf(angle) * len;
	float z1 = cz + cosf(angle) * len, z2 = cz - cosf(angle) * len;
	float y1 = cy + (float)(gen_mc_feat_int(rng, 3) - 1);
	float y2 = cy + (float)(gen_mc_feat_int(rng, 3) - 1);

	for(int i = 0; i < count; i++) {
		float t = (count > 1) ? (float)i / (float)(count - 1) : 0.0f;
		float px = x1 + (x2 - x1) * t;
		float py = y1 + (y2 - y1) * t;
		float pz = z1 + (z2 - z1) * t;
		/* Radius pulsiert entlang der Ader → dickere Mitte, dünnere Enden */
		float rad = sinf(t * 3.14159265f) * ((float)count / 20.0f) + 0.6f;
		int ir = (int)rad + 1;
		for(int dx = -ir; dx <= ir; dx++)
			for(int dy = -ir; dy <= ir; dy++)
				for(int dz = -ir; dz <= ir; dz++) {
					if((float)(dx*dx + dy*dy + dz*dz) > rad * rad + 0.3f) continue;
					int bx = (int)(px + 0.5f) + dx;
					int by = (int)(py + 0.5f) + dy;
					int bz = (int)(pz + 0.5f) + dz;
					if(bx < 0 || bx >= CHUNK_SIZE || bz < 0 || bz >= CHUNK_SIZE) continue;
					if(by < 1 || by >= WORLD_HEIGHT) continue;
					if(sc->ids[S_CHUNK_IDX(bx, by, bz)] == BLOCK_STONE)
						sc->ids[S_CHUNK_IDX(bx, by, bz)] = ore;
				}
	}
}

/* Platziert alle Erz-Adern eines Chunks (einmal pro Chunk aufgerufen). */
static void gen_place_ore_veins(struct server_chunk* sc, int64_t world_seed,
                                int world_x0, int world_z0, Generator* biome_gen) {
	int cx = world_x0 / CHUNK_SIZE;
	int cz = world_z0 / CHUNK_SIZE;
	uint64_t rng = (uint64_t)((int64_t)cx * 341873128712LL
	             + (int64_t)cz * 132897987541LL + world_seed + 0x0E5A1234LL)
	             & 0xFFFFFFFFFFFFULL;
	rng = (rng ^ 25214903917LL) & 0xFFFFFFFFFFFFULL;

	/* Erz-Typ, Adern pro Chunk, Ader-Größe, y-Bereich */
	struct { uint8_t ore; int veins; int size; int ymin; int ymax; } T[] = {
		{ BLOCK_COAL_ORE,    14, 17, 5,  120 },
		{ BLOCK_IRON_ORE,    10,  9, 5,  64  },
		{ BLOCK_GOLD_ORE,     2,  9, 5,  32  },
		{ BLOCK_LAPIS_ORE,    1,  7, 11, 32  },
		{ BLOCK_DIAMOND_ORE,  1,  8, 5,  16  },
	};
	for(size_t t = 0; t < sizeof(T) / sizeof(T[0]); t++) {
		for(int v = 0; v < T[t].veins; v++) {
			float px = (float)gen_mc_feat_int(&rng, CHUNK_SIZE);
			float pz = (float)gen_mc_feat_int(&rng, CHUNK_SIZE);
			int span = T[t].ymax - T[t].ymin;
			float py = (float)(T[t].ymin + gen_mc_feat_int(&rng, span > 0 ? span : 1));
			gen_place_ore_vein(sc, px, py, pz, T[t].size, T[t].ore, &rng);
		}
	}

	/* Smaragd: einzelne Blöcke, nur in Bergen (wie in Minecraft) */
	int cb = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);
	if(cb == mountains || cb == wooded_mountains || cb == gravelly_mountains
	   || cb == mountain_edge || cb == modified_gravelly_mountains) {
		int n_emerald = 3 + gen_mc_feat_int(&rng, 6); /* 3..8 */
		for(int e = 0; e < n_emerald; e++) {
			int bx = gen_mc_feat_int(&rng, CHUNK_SIZE);
			int bz = gen_mc_feat_int(&rng, CHUNK_SIZE);
			int by = 16 + gen_mc_feat_int(&rng, 90);
			if(by < 1 || by >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(bx, by, bz)] == BLOCK_STONE)
				sc->ids[S_CHUNK_IDX(bx, by, bz)] = BLOCK_EMERALD_ORE;
		}
	}
}

static void gen_cleanup_floating_ores(struct server_chunk* sc) {
	if(sc == NULL)
		return;
	for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
		for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
			for(int y = 2; y < WORLD_HEIGHT - 2; y++) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				uint8_t block = sc->ids[idx];
				if(!gen_is_ore_block(block))
					continue;

				int open_neighbors = 0;
				if(gen_is_air(sc, lx + 1, y, lz) || gen_is_water(sc, lx + 1, y, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx - 1, y, lz) || gen_is_water(sc, lx - 1, y, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx, y + 1, lz) || gen_is_water(sc, lx, y + 1, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx, y - 1, lz) || gen_is_water(sc, lx, y - 1, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx, y, lz + 1) || gen_is_water(sc, lx, y, lz + 1))
					open_neighbors++;
				if(gen_is_air(sc, lx, y, lz - 1) || gen_is_water(sc, lx, y, lz - 1))
					open_neighbors++;

				bool unsupported = gen_is_air(sc, lx, y - 1, lz) || gen_is_water(sc, lx, y - 1, lz);
				if(open_neighbors >= 5 || (unsupported && open_neighbors >= 4))
					sc->ids[idx] = BLOCK_STONE;
			}
		}
	}
}

static void gen_cleanup_floating_cave_blocks(struct server_chunk* sc) {
	if(sc == NULL)
		return;
	for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
		for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
			for(int y = 2; y < WORLD_HEIGHT - 2; y++) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				uint8_t block = sc->ids[idx];
				if(block != BLOCK_STONE && block != BLOCK_DIRT && block != BLOCK_GRASS
				   && block != BLOCK_SAND && block != BLOCK_SANDSTONE && block != BLOCK_GRAVEL)
					continue;

				int open_neighbors = 0;
				int horizontal_open = 0;
				if(gen_is_air(sc, lx + 1, y, lz) || gen_is_water(sc, lx + 1, y, lz)) {
					open_neighbors++;
					horizontal_open++;
				}
				if(gen_is_air(sc, lx - 1, y, lz) || gen_is_water(sc, lx - 1, y, lz)) {
					open_neighbors++;
					horizontal_open++;
				}
				if(gen_is_air(sc, lx, y + 1, lz) || gen_is_water(sc, lx, y + 1, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx, y - 1, lz) || gen_is_water(sc, lx, y - 1, lz))
					open_neighbors++;
				if(gen_is_air(sc, lx, y, lz + 1) || gen_is_water(sc, lx, y, lz + 1)) {
					open_neighbors++;
					horizontal_open++;
				}
				if(gen_is_air(sc, lx, y, lz - 1) || gen_is_water(sc, lx, y, lz - 1)) {
					open_neighbors++;
					horizontal_open++;
				}

				bool unsupported = gen_is_air(sc, lx, y - 1, lz) || gen_is_water(sc, lx, y - 1, lz);
				bool top_open = gen_is_air(sc, lx, y + 1, lz) || gen_is_water(sc, lx, y + 1, lz);
				if(open_neighbors >= 4
				   || (unsupported && horizontal_open >= 2)
				   || (unsupported && top_open && open_neighbors >= 3))
					sc->ids[idx] = BLOCK_AIR;
			}
		}
	}
}

static bool gen_is_falling_block(uint8_t block) {
	return (block == BLOCK_SAND) || (block == BLOCK_GRAVEL);
}

static int gen_chunk_surface_height(const struct server_chunk* sc, int lx, int lz);
static int gen_chunk_actual_surface_height(const struct server_chunk* sc, int lx, int lz);

static void gen_settle_falling_blocks_local(struct server_chunk* sc) {
	if(sc == NULL)
		return;
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			for(int y = 2; y < WORLD_HEIGHT; y++) {
				uint8_t block = gen_get_block(sc, lx, y, lz);
				if(!gen_is_falling_block(block))
					continue;
				int ny = y;
				while(ny > 1) {
					uint8_t below = gen_get_block(sc, lx, ny - 1, lz);
					if(below != BLOCK_AIR && below != BLOCK_WATER_STILL && below != BLOCK_WATER_FLOW)
						break;
					ny--;
				}
				if(ny != y) {
					gen_set_block(sc, lx, ny, lz, block);
					gen_set_block(sc, lx, y, lz, BLOCK_AIR);
				}
			}
		}
	}
}

static int gen_chunk_surface_height(const struct server_chunk* sc, int lx, int lz) {
	if(!sc || !sc->heightmap)
		return -1;
	uint8_t h = sc->heightmap[lx + lz * CHUNK_SIZE];
	if(h == 0)
		return -1;
	return (int)h - 1;
}

static int gen_chunk_actual_surface_height(const struct server_chunk* sc, int lx, int lz) {
	if(!sc)
		return -1;
	if(lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE)
		return -1;
	for(int y = WORLD_HEIGHT - 1; y >= 0; y--) {
		uint8_t block = sc->ids[S_CHUNK_IDX(lx, y, lz)];
		if(block != BLOCK_AIR && block != BLOCK_WATER_FLOW
		   && block != BLOCK_WATER_STILL) {
			return y;
		}
	}
	return -1;
}

static bool gen_sample_neighbor_edge_height(struct server_world* w, w_coord_t cx,
											w_coord_t cz, int dir, int index,
											int* out_height) {
	w_coord_t nx = cx;
	w_coord_t nz = cz;
	if(dir == 0)
		nx--;
	else if(dir == 1)
		nx++;
	else if(dir == 2)
		nz--;
	else
		nz++;

	struct server_chunk* nsc = dict_server_chunks_get(w->chunks, S_CHUNK_ID(nx, nz));
	if(!nsc)
		return false;

	int lx = 0;
	int lz = 0;
	if(dir == 0) {
		lx = CHUNK_SIZE - 1;
		lz = index;
	} else if(dir == 1) {
		lx = 0;
		lz = index;
	} else if(dir == 2) {
		lx = index;
		lz = CHUNK_SIZE - 1;
	} else {
		lx = index;
		lz = 0;
	}

	int h = gen_chunk_surface_height(nsc, lx, lz);
	if(h < 0)
		return false;

	*out_height = h;
	return true;
}

static int gen_smooth_surface_against_local_neighbors(
	const int* surface_map, int lx, int lz, int surface,
	const struct gen_biome_profile* profile, int sea_level) {
	if(surface_map == NULL || profile == NULL || profile->riverine)
		return surface;

	int sum = 0;
	int count = 0;
	if(lz > 0) {
		sum += surface_map[lx + (lz - 1) * CHUNK_SIZE];
		count++;
	}
	if(lx > 0) {
		sum += surface_map[(lx - 1) + lz * CHUNK_SIZE];
		count++;
	}
	if(count <= 0)
		return surface;

	int avg = sum / count;
	int max_step = 3;
	if(surface <= sea_level + 5 || profile->top_block == BLOCK_SAND)
		max_step = 2;
	if(profile->top_block == BLOCK_STONE)
		max_step = 6;
	if(profile->amplitude > 12.0f && profile->top_block != BLOCK_SAND)
		max_step = 5;

	if(surface > avg + max_step)
		surface = avg + max_step;
	else if(surface < avg - max_step)
		surface = avg - max_step;
	return surface;
}

static int gen_sample_raw_surface(Generator* biome_gen,
								  const struct gen_cuberite_runtime_args* gen_args,
								  uint32_t seed, int choice_octaves,
								  int32_t wx, int32_t wz) {
	int biome_id = plains;
	struct gen_biome_profile profile = gen_blended_profile(biome_gen, wx, wz, &biome_id);
	int surface = gen_compute_surface_height_base(
		&profile, biome_id, gen_args, seed, choice_octaves, wx, wz);
	return gen_apply_river_channel(&profile, gen_args, seed, wx, wz, surface);
}

static int gen_smooth_surface_against_terrain_shape_area(
	Generator* biome_gen, const struct gen_cuberite_runtime_args* gen_args,
	uint32_t seed, int choice_octaves, int32_t wx, int32_t wz,
	const struct gen_biome_profile* profile, int surface) {
	if(profile == NULL || profile->oceanic || profile->riverine)
		return surface;

	static const int offs[4][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4}};
	int sum = surface * 4;
	int count = 4;
	for(int i = 0; i < 4; i++) {
		sum += gen_sample_raw_surface(biome_gen, gen_args, seed, choice_octaves,
									  wx + offs[i][0], wz + offs[i][1]);
		count++;
	}
	int avg = sum / count;
	int max_delta = (profile->amplitude > 18.0f) ? 8 : 4;
	if(surface > avg + max_delta)
		surface = avg + max_delta;
	else if(surface < avg - max_delta)
		surface = avg - max_delta;
	return surface;
}

static int gen_wgen_height_at(wgen_gen_t* wg, const uint8_t* cache,
							 int cache_origin_x, int cache_origin_z,
							 int wx, int wz) {
	if(cache && wx >= cache_origin_x
	   && wx < cache_origin_x + SERVER_WORLD_WGEN_HEIGHT_WIDTH
	   && wz >= cache_origin_z
	   && wz < cache_origin_z + SERVER_WORLD_WGEN_HEIGHT_WIDTH)
		return cache[(wx - cache_origin_x)
				 + (wz - cache_origin_z) * SERVER_WORLD_WGEN_HEIGHT_WIDTH];
	return wgen_gen_world_height(wg, wx, wz);
}

static int gen_apply_sea_shore_lowering(wgen_gen_t* wg,
										const struct gen_cuberite_runtime_args* gen_args,
										const uint8_t* height_cache,
										int cache_origin_x, int cache_origin_z,
										int32_t wx, int32_t wz, int surface) {
	if(!GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL || wg == NULL || gen_args == NULL)
		return surface;
	if(surface <= gen_args->sea_level + 3)
		return surface;

	int lowered = surface;
	const int radius = 22;
	const int radius_sq = radius * radius;
	for(int dz = -radius; dz <= radius; dz += 2) {
		for(int dx = -radius; dx <= radius; dx += 2) {
			if(dx == 0 && dz == 0)
				continue;
			int dist_sq = dx * dx + dz * dz;
			if(dist_sq > radius_sq)
				continue;

			int neighbor = gen_wgen_height_at(wg, height_cache, cache_origin_x,
											cache_origin_z, wx + dx, wz + dz);
			if(neighbor > gen_args->sea_level + 2)
				continue;
			float dist = sqrtf((float)(dx * dx + dz * dz));
			float slope = (dist <= 8.0f) ? 0.85f : 1.45f;
			int shore_cap = gen_args->sea_level + 1 + (int)(dist * slope);
			if(shore_cap < lowered)
				lowered = shore_cap;
		}
	}
	return lowered;
}

static int gen_apply_large_water_depth(wgen_gen_t* wg,
									   const struct gen_cuberite_runtime_args* gen_args,
									   const uint8_t* height_cache,
									   int cache_origin_x, int cache_origin_z,
									   int32_t wx, int32_t wz, int surface) {
	if(!GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL || wg == NULL || gen_args == NULL)
		return surface;
	if(surface >= gen_args->sea_level)
		return surface;

	const int radius = 26;
	static const int dirs[8][2] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {-1, 1}, {1, -1}, {-1, -1},
	};
	float land_dist = (float)radius;
	bool found_land = false;
	for(int r = 2; r <= radius && !found_land; r += 2) {
		for(int i = 0; i < 8; i++) {
			int dx = dirs[i][0] * r;
			int dz = dirs[i][1] * r;
			int neighbor = gen_wgen_height_at(wg, height_cache, cache_origin_x,
											cache_origin_z, wx + dx, wz + dz);
			if(neighbor > gen_args->sea_level) {
				land_dist = sqrtf((float)(dx * dx + dz * dz));
				found_land = true;
				break;
			}
		}
	}
	int desired_depth = 4 + (int)(land_dist * 1.55f);
	if(desired_depth > 40)
		desired_depth = 40;
	if(desired_depth < 4)
		desired_depth = 4;

	int deep_floor = gen_args->sea_level - desired_depth;
	if(deep_floor < 4)
		deep_floor = 4;
	if(surface > deep_floor)
		surface = deep_floor;
	return surface;
}

/* Region-Ozeane brauchen eine eigene Bathymetrie. Die Noise-Höhe kann an
 * derselben Stelle deutlich über dem Meeresspiegel liegen; dann darf der
 * Region-Ozean nicht als steile Wand stehen bleiben. */
static int gen_apply_region_ocean_depth(Generator* biome_gen,
										const struct gen_cuberite_runtime_args* gen_args,
										int32_t wx, int32_t wz, int surface) {
	if(!biome_gen || !gen_args)
		return surface;

	static const int dirs[8][2] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {-1, 1}, {1, -1}, {-1, -1},
	};
	float land_dist = 10.0f;
	bool found_land = false;
	/* Der Biome-Cache reicht acht Blöcke über den Chunkrand. */
	for(int r = 2; r <= GEN_BIOME_CACHE_MARGIN && !found_land; r += 2) {
		for(int i = 0; i < 8; i++) {
			int sample = gen_biome_at_safe(biome_gen,
				wx + dirs[i][0] * r, wz + dirs[i][1] * r);
			if(!isOceanic(sample)) {
				land_dist = sqrtf((float)(dirs[i][0] * dirs[i][0]
					+ dirs[i][1] * dirs[i][1]) * (float)(r * r));
				found_land = true;
				break;
			}
		}
	}

	int desired_depth = 4 + (int)(land_dist * 1.55f);
	if(desired_depth > 40)
		desired_depth = 40;
	int floor = gen_args->sea_level - desired_depth;
	if(floor < 4)
		floor = 4;
	if(surface > floor)
		surface = floor;
	return surface;
}

static int gen_apply_region_shore_lowering(Generator* biome_gen,
										const struct gen_cuberite_runtime_args* gen_args,
										int32_t wx, int32_t wz, int surface) {
	if(!biome_gen || !gen_args)
		return surface;

	static const int dirs[8][2] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {-1, 1}, {1, -1}, {-1, -1},
	};
	float water_dist = 10.0f;
	bool found_water = false;
	for(int r = 2; r <= GEN_BIOME_CACHE_MARGIN && !found_water; r += 2) {
		for(int i = 0; i < 8; i++) {
			int sample = gen_biome_at_safe(biome_gen,
				wx + dirs[i][0] * r, wz + dirs[i][1] * r);
			if(isOceanic(sample)) {
				water_dist = sqrtf((float)(dirs[i][0] * dirs[i][0]
					+ dirs[i][1] * dirs[i][1]) * (float)(r * r));
				found_water = true;
				break;
			}
		}
	}
	if(!found_water)
		return surface;

	/* Küstenland steigt langsam an; niemals eine senkrechte Noise-Wand. */
	int shore_cap = gen_args->sea_level + 1
		+ (int)(water_dist * 0.85f);
	if(surface > shore_cap)
		surface = shore_cap;
	return surface;
}

static float gen_terrain_shape_density(const struct gen_biome_profile* profile,
								   const struct gen_cuberite_runtime_args* gen_args,
								   uint32_t seed, int32_t wx, int y, int32_t wz,
								   int surface) {
	float vertical = ((float)surface - (float)y) / 10.5f;
	float low = gen_fbm3d((float)wx * gen_args->land_frequency_x * 0.42f,
						  (float)y * 0.010f,
						  (float)wz * gen_args->land_frequency_z * 0.42f,
						  seed ^ 0xD31517E1U, 3, 2.0f, 0.5f);
	float detail = gen_fbm3d((float)wx * gen_args->detail_frequency_x * 0.65f,
							 (float)y * 0.026f,
							 (float)wz * gen_args->detail_frequency_z * 0.65f,
							 seed ^ 0xD31517E2U, 2, 2.0f, 0.5f);
	float rugged = gen_fbm2d((float)wx * gen_args->mountain_frequency_x,
							(float)wz * gen_args->mountain_frequency_z,
							seed ^ 0xD31517E3U, 2, 2.0f, 0.5f);
	float cave_bias = (profile != NULL && profile->amplitude > 18.0f) ? 0.05f : -0.08f;
	float density = vertical + low * 0.42f + detail * 0.18f + fmaxf(0.0f, rugged) * 0.20f
		+ cave_bias;
	if(y < 5)
		density += (5.0f - (float)y) * 0.80f;
	if(profile != NULL && profile->oceanic && y > surface - 4)
		density -= 0.25f;
	return density;
}

static void gen_apply_surface_column(struct server_chunk* sc, int lx, int lz,
									 int sea_level, int preliminary_surface,
									 uint8_t top_block, uint8_t filler_block,
									 int dirt_depth, int* out_surface) {
	/* This is called immediately after a fresh column was filled with stone
	 * and water.  The top solid block is therefore already known; scanning all
	 * 128 heights here only repeated work that the terrain pass had done. */
	int surface = preliminary_surface;
	if(surface >= 1 && surface < WORLD_HEIGHT) {
		size_t idx = S_CHUNK_IDX(lx, surface, lz);
		sc->ids[idx] = (surface <= sea_level - 1 && top_block == BLOCK_GRASS)
			? filler_block : top_block;
		for(int depth = 1; depth <= dirt_depth && surface - depth >= 1; depth++)
			sc->ids[S_CHUNK_IDX(lx, surface - depth, lz)] = filler_block;
	}
	if(out_surface)
		*out_surface = surface;
}

/* ---- Server-Chunk-Speicherpool -----------------------------------------
 * Kernproblem auf der Wii: pro Chunk-Lade/Entlade wurden 5 große Puffer
 * (32K+16K+16K+16K+256B) einzeln ge-malloc'd/free'd. Beim Erkunden zerstückelt
 * dieser Churn MEM1 so, dass ein 32-KB-calloc scheitert obwohl in Summe MB frei
 * sind ("load OOM" bei nur 18 Chunks). Der Pool alloziert EINMAL einen festen
 * Block und vergibt daraus Slots -> null Churn, null Fragmentierung,
 * deterministische Obergrenze. */
#define SC_IDS_BYTES  (CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT)
#define SC_HALF_BYTES (SC_IDS_BYTES / 2)
#define SC_HMAP_BYTES (CHUNK_SIZE * CHUNK_SIZE)
#define SC_SET_BYTES  (SC_IDS_BYTES + 3 * SC_HALF_BYTES + SC_HMAP_BYTES)

#ifndef SERVER_CHUNK_POOL_MAX
#define SERVER_CHUNK_POOL_MAX 96 /* Obergrenze; wächst nur so weit wie MEM1 reicht */
#endif

/* Slots werden EINZELN und LAZY alloziert (je ~80KB — viel leichter im
 * fragmentierten MEM1 zu platzieren als ein 5-MB-Block am Stück) und danach
 * NIE freigegeben, sondern über eine Free-List wiederverwendet. Dadurch gibt es
 * nach der Aufwärmphase keinen malloc/free-Churn mehr -> keine Fragmentierung.
 * g_sc_cap sinkt automatisch auf die real erreichbare Slot-Zahl, sobald ein
 * malloc scheitert (MEM1 erschöpft). */
static uint8_t* g_sc_slot[SERVER_CHUNK_POOL_MAX];
static int g_sc_count = 0;                       /* bisher allozierte Slots      */
static int g_sc_cap = SERVER_CHUNK_POOL_MAX;     /* effektive Obergrenze          */
static int16_t g_sc_freelist[SERVER_CHUNK_POOL_MAX];
static int g_sc_freelist_n = 0;                  /* freie, wiederverwendbare Slots */

int server_world_chunk_pool_total(void) { return g_sc_cap; }
int server_world_chunk_pool_free(void) {
	/* wiederverwendbare + noch nicht angelegte (aber innerhalb cap) Slots */
	return g_sc_freelist_n + (g_sc_cap - g_sc_count);
}

static void sc_pool_carve(struct server_chunk* sc, int slot) {
	uint8_t* p = g_sc_slot[slot];
	memset(p, 0, SC_SET_BYTES);
	sc->ids = p;            p += SC_IDS_BYTES;
	sc->metadata = p;       p += SC_HALF_BYTES;
	sc->lighting_sky = p;   p += SC_HALF_BYTES;
	sc->lighting_torch = p; p += SC_HALF_BYTES;
	sc->heightmap = p;
	sc->from_pool = true;
	sc->pool_slot = (int16_t)slot;
}

static bool sc_pool_take(struct server_chunk* sc) {
	int slot = -1;
	if(g_sc_freelist_n > 0) {
		slot = g_sc_freelist[--g_sc_freelist_n];
	} else if(g_sc_count < g_sc_cap) {
		uint8_t* p = malloc(SC_SET_BYTES);
		if(p) {
			g_sc_slot[g_sc_count] = p;
			slot = g_sc_count++;
		} else {
			/* MEM1 erschöpft -> nicht weiter wachsen */
			g_sc_cap = g_sc_count;
		}
	}
	if(slot < 0)
		return false;
	sc_pool_carve(sc, slot);
	return true;
}

static void sc_pool_return(struct server_chunk* sc) {
	int i = sc->pool_slot;
	if(sc->from_pool && i >= 0 && i < g_sc_count
	   && g_sc_freelist_n < SERVER_CHUNK_POOL_MAX)
		g_sc_freelist[g_sc_freelist_n++] = (int16_t)i;
}

static bool gen_alloc_chunk_buffers(struct server_chunk* sc) {
	*sc = (struct server_chunk) {0};

	/* Bevorzugt aus dem Pool (fragmentierungsfrei). */
	if(sc_pool_take(sc)) {
		sc->modified = true;
		return true;
	}

	/* Fallback: einzeln allozieren (Pool voll oder nicht verfügbar). */
	size_t total = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
	sc->ids = calloc(total, 1);
	sc->metadata = calloc(total / 2, 1);
	sc->lighting_sky = calloc(total / 2, 1);
	sc->lighting_torch = calloc(total / 2, 1);
	sc->heightmap = calloc(CHUNK_SIZE * CHUNK_SIZE, 1);
	sc->modified = true;

	if(!sc->ids || !sc->metadata || !sc->lighting_sky || !sc->lighting_torch
	   || !sc->heightmap) {
		free(sc->ids);
		free(sc->metadata);
		free(sc->lighting_sky);
		free(sc->lighting_torch);
		free(sc->heightmap);
		*sc = (struct server_chunk) {0};
		return false;
	}
	return true;
}

static wgen_gen_t* server_world_wgen_gen(struct server_world* w); /* forward decl */

static void gen_generate_terrain_columns(struct server_world* w,
										 struct server_chunk* sc, int* surface_map,
										 const uint8_t (*cached_wgen_heights)[CHUNK_SIZE],
										 const uint8_t* cached_height_cache,
										 int cache_origin_x, int cache_origin_z,
										 Generator* biome_gen,
										 const struct gen_cuberite_runtime_args* gen_args,
										 uint32_t seed, int choice_octaves,
										 int32_t world_x0, int32_t world_z0,
										 w_coord_t chunk_x, w_coord_t chunk_z,
										 int start_col, int num_cols) {
	int end_col = start_col + num_cols;
	if(end_col > CHUNK_SIZE * CHUNK_SIZE)
		end_col = CHUNK_SIZE * CHUNK_SIZE;

	/* MC-26.3-Höhen einmal pro Chunk berechnen.  Pending generation calls
	 * this function many times in small column batches, so recomputing the
	 * complete 16x16 map in every batch was needlessly expensive. */
	wgen_gen_t* wg = server_world_wgen_gen(w);
	uint8_t local_wgen_heights[CHUNK_SIZE][CHUNK_SIZE];
	uint8_t local_height_cache[SERVER_WORLD_WGEN_HEIGHT_WIDTH]
		[SERVER_WORLD_WGEN_HEIGHT_WIDTH];
	const uint8_t (*wgen_heights)[CHUNK_SIZE] = cached_wgen_heights;
	const uint8_t* height_cache = cached_height_cache;
	if(!wgen_heights) {
		wgen_gen_surface_heights(wg, (int)chunk_x, (int)chunk_z,
		                         local_wgen_heights);
		wgen_heights = local_wgen_heights;
	}
	if(!height_cache) {
		cache_origin_x = world_x0 - SERVER_WORLD_WGEN_HEIGHT_MARGIN;
		cache_origin_z = world_z0 - SERVER_WORLD_WGEN_HEIGHT_MARGIN;
		for(int cz = 0; cz < SERVER_WORLD_WGEN_HEIGHT_WIDTH; cz++)
			for(int cx = 0; cx < SERVER_WORLD_WGEN_HEIGHT_WIDTH; cx++)
				local_height_cache[cz][cx]
					= (uint8_t)wgen_gen_world_height(
						wg, cache_origin_x + cx, cache_origin_z + cz);
		height_cache = &local_height_cache[0][0];
	}

	for(int col = start_col; col < end_col; col++) {
		int lx = col / CHUNK_SIZE;
		int lz = col % CHUNK_SIZE;
		int32_t wx = world_x0 + lx;
		int32_t wz = world_z0 + lz;
		int biome_id = plains;
		struct gen_biome_profile profile = gen_blended_profile(biome_gen, wx, wz, &biome_id);
		/* Die Biome-Variation darf ein echtes Region-Ozean nicht in Land
		 * umwandeln, bevor die Küstenform berechnet wurde. */
		bool region_ocean = isOceanic(gen_biome_at_safe(biome_gen, wx, wz));
		int surface = (int)wgen_heights[lx][lz];
		/* Flüsse und Region-Ozeane werden getrennt vom Noise-Terrain behandelt.
		 * Region-Ozeane erhalten weiter unten eine eigene, sanfte Bathymetrie. */
		if(profile.riverine && surface > gen_args->sea_level - 1
		   && surface <= gen_args->sea_level + 5)
			surface = gen_args->sea_level - 1;
		if(region_ocean && surface > gen_args->sea_level - 3
		   && surface <= gen_args->sea_level + 10)
			surface = gen_args->sea_level - 3;
		surface = gen_apply_large_water_depth(wg, gen_args, height_cache,
																		 cache_origin_x, cache_origin_z,
																		 wx, wz, surface);
		surface = gen_apply_sea_shore_lowering(wg, gen_args, height_cache,
																		 cache_origin_x, cache_origin_z,
																		 wx, wz, surface);
		if(region_ocean || profile.oceanic) {
			surface = gen_apply_region_ocean_depth(biome_gen, gen_args,
																		 wx, wz, surface);
		} else {
			surface = gen_apply_region_shore_lowering(biome_gen, gen_args,
																		 wx, wz, surface);
	}

		int dirt_depth = 0;
		uint8_t top_block = BLOCK_GRASS;
		uint8_t filler_block = BLOCK_DIRT;
		gen_choose_surface_materials(&profile, biome_id, gen_args, seed, wx, wz,
									 surface, &top_block, &filler_block,
									 &dirt_depth);
		if(surface <= gen_args->sea_level + 3 && !profile.riverine
		   && top_block == BLOCK_GRASS) {
			top_block = BLOCK_GRAVEL;
			filler_block = BLOCK_DIRT;
			if(dirt_depth < 3)
				dirt_depth = 3;
		}

		for(int y = 0; y < WORLD_HEIGHT; y++) {
			size_t idx = S_CHUNK_IDX(lx, y, lz);
			uint8_t block = BLOCK_AIR;
			if(y == 0) {
				block = BLOCK_BEDROCK; /* unterste Schicht immer Bedrock */
			} else if(y <= 3
			          && gen_rand01_from_hash(
			                 gen_hash3i(wx, y, wz, seed ^ 0xBED70C01U))
			                 < (float)(4 - y) / 4.0f) {
				/* bruchartige Bedrock-Schicht: y=1 ~75%, y=2 ~50%, y=3 ~25%
				 * → Gesamtdicke variiert unregelmäßig zwischen 1 und 4 */
				block = BLOCK_BEDROCK;
			} else {
				if(y <= surface) {
					block = BLOCK_STONE;
				} else if(GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL
						  && y <= gen_args->sea_level) {
					block = BLOCK_WATER_STILL;
				}
			}
			sc->ids[idx] = block;
		}
		int actual_surface = surface;
		gen_apply_surface_column(sc, lx, lz, gen_args->sea_level, surface,
								 top_block, filler_block, dirt_depth,
								 &actual_surface);
		if(actual_surface >= 0)
			surface = actual_surface;
		surface_map[lx + lz * CHUNK_SIZE] = surface;

		/* Unterwasser: Erde/Gras unter Meeresspiegel → Kies */
		if(actual_surface > 0 && actual_surface <= gen_args->sea_level - 1) {
			for(int uy = actual_surface; uy >= 1 && uy >= actual_surface - 3; uy--) {
				size_t uidx = S_CHUNK_IDX(lx, uy, lz);
				if(sc->ids[uidx] == BLOCK_DIRT || sc->ids[uidx] == BLOCK_GRASS)
					sc->ids[uidx] = BLOCK_GRAVEL;
			}
		}

		if(GEN_ENABLE_SNOW_SURFACE_LAYER && w->generator.finisher_snow
		   && surface > gen_args->sea_level + 1 && isSnowy(biome_id)
		   && surface < WORLD_HEIGHT - 1) { /* surface+1 darf nicht WORLD_HEIGHT sein */
			size_t top_idx = S_CHUNK_IDX(lx, surface + 1, lz);
			if(sc->ids[top_idx] == BLOCK_AIR)
				sc->ids[top_idx] = BLOCK_SNOW;
		}

		/* Erze werden als Adern (Veins) platziert, nicht per Block —
		 * siehe gen_place_ore_veins (einmal pro Chunk in der Finalize-Phase). */
	}
}

/* Forward declarations for RNG helpers used below */
static int64_t gen_mc_legacy_next_long(uint64_t* s);
static int gen_mc_feat_int(uint64_t* s, int n);

static void gen_place_dungeons(struct server_chunk* sc, int64_t world_seed,
                                int world_x0, int world_z0) {
	/* MC: 8 Dungeons per chunk attempt */
	int chunk_x = world_x0 / CHUNK_SIZE;
	int chunk_z = world_z0 / CHUNK_SIZE;

	/* Large-Feature-Seed: */
	uint64_t ls = (uint64_t)(world_seed ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL;
	int64_t S2 = gen_mc_legacy_next_long(&ls);
	int64_t T2 = gen_mc_legacy_next_long(&ls);
	uint64_t dungeon_seed = (uint64_t)((int64_t)chunk_x * S2 ^ (int64_t)chunk_z * T2 ^ world_seed);
	dungeon_seed = (dungeon_seed ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL;

	/* 8 Versuche */
	for(int i = 0; i < 8; i++) {
		int lx = gen_mc_feat_int(&dungeon_seed, 16);
		int lz = gen_mc_feat_int(&dungeon_seed, 16);
		int ly = gen_mc_feat_int(&dungeon_seed, WORLD_HEIGHT);

		/* Nur weit unter der Oberfläche — nie weniger als 15 Blöcke tief */
		if(ly > WORLD_HEIGHT - 30) continue; /* Absoluter y-Cap */
		/* Prüfe Oberfläche dieser Spalte */
		bool too_close_to_surface = false;
		for(int check_y = ly + 1; check_y < WORLD_HEIGHT - 1 && check_y <= ly + 15; check_y++) {
			uint8_t above = gen_get_block(sc, lx, check_y, lz);
			if(above == BLOCK_AIR || above == BLOCK_WATER_STILL) {
				too_close_to_surface = true;
				break;
			}
		}
		if(too_close_to_surface) continue;

		if(sc->ids[S_CHUNK_IDX(lx, ly, lz)] != BLOCK_AIR) continue;

		/* Dungeon: 5x5 oder 7x7, aus Moos-Kopfstein */
		int w2 = 2 + gen_mc_feat_int(&dungeon_seed, 2); /* 2 oder 3 */
		int d2 = 2 + gen_mc_feat_int(&dungeon_seed, 2);

		if(lx < w2 || lx >= CHUNK_SIZE - w2 || lz < d2 || lz >= CHUNK_SIZE - d2) continue;
		if(ly < 3 || ly >= WORLD_HEIGHT - 3) continue;
		if(!gen_dungeon_has_earth_cover(sc, lx - w2, lx + w2, ly, ly + 2,
										lz - d2, lz + d2))
			continue;
		if(!gen_dungeon_connected_to_cave(sc, lx - w2, lx + w2, ly, ly + 2,
										  lz - d2, lz + d2))
			continue;

		/* Wände prüfen: genug Stein drum herum? */
		int solid = 0;
		for(int dx = -w2 - 1; dx <= w2 + 1; dx++)
			for(int dz = -d2 - 1; dz <= d2 + 1; dz++) {
				int nx = lx + dx, nz = lz + dz;
				if(nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
				if(sc->ids[S_CHUNK_IDX(nx, ly, nz)] == BLOCK_STONE) solid++;
			}
		if(solid < 12) continue;

		/* Dungeon aushöhlen */
		for(int dx = -w2; dx <= w2; dx++)
			for(int dz = -d2; dz <= d2; dz++)
				for(int dy = 0; dy <= 2; dy++) {
					int nx = lx + dx, nz = lz + dz, ny = ly + dy;
					if(nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
					if(!gen_inside_chunk(nx, ny, nz)) continue;
					uint8_t b = (dy == 0) ?
						((gen_mc_feat_int(&dungeon_seed, 4) < 2) ? BLOCK_COBBLESTONE : BLOCK_MOSSY_COBBLE) :
						BLOCK_AIR;
					gen_set_block(sc, nx, ny, nz, b);
				}

		/* Spawner */
		gen_set_block(sc, lx, ly + 1, lz, BLOCK_SPAWNER);

		/* Truhen an den Ecken */
		if(lx - w2 + 1 >= 0 && lz - d2 + 1 >= 0
		   && lx - w2 + 1 < CHUNK_SIZE && lz - d2 + 1 < CHUNK_SIZE)
			gen_set_block(sc, lx - w2 + 1, ly + 1, lz - d2 + 1, BLOCK_CHEST);
		if(lx + w2 - 1 < CHUNK_SIZE && lz + d2 - 1 < CHUNK_SIZE)
			gen_set_block(sc, lx + w2 - 1, ly + 1, lz + d2 - 1, BLOCK_CHEST);
	}
}

static void gen_place_desert_structures(struct server_chunk* sc, int64_t world_seed,
                                         int world_x0, int world_z0, Generator* biome_gen) {
	int center_biome = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);
	if(center_biome != desert && center_biome != desert_hills
	   && center_biome != desert_lakes)
		return;

	/* ZUSÄTZLICH: Prüfe ob wirklich Sand an der Oberfläche */
	{
		int cx_check = 8, cz_check = 8;
		int sy_check = gen_chunk_actual_surface_height(sc, cx_check, cz_check);
		if(sy_check <= 0 || sy_check >= WORLD_HEIGHT - 1) return;
		uint8_t top_check = gen_get_block(sc, cx_check, sy_check, cz_check);
		if(top_check != BLOCK_SAND && top_check != BLOCK_SANDSTONE) return;
	}

	/* Wüstenbrunnen: 1 per ca. 500 Chunks */
	int chunk_x = world_x0 / CHUNK_SIZE;
	int chunk_z = world_z0 / CHUNK_SIZE;
	uint64_t well_rng = (uint64_t)((int64_t)chunk_x * 341873128712LL
	                    + (int64_t)chunk_z * 132897987541LL + world_seed + 14357617LL)
	                   & 0xFFFFFFFFFFFFULL;
	well_rng = (well_rng ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL;
	if(gen_mc_feat_int(&well_rng, 1000) < 2) { /* ~0.2% Chance */
		int lx = 2 + gen_mc_feat_int(&well_rng, 12);
		int lz = 2 + gen_mc_feat_int(&well_rng, 12);
		int sy = gen_chunk_actual_surface_height(sc, lx, lz);
		if(sy >= 2 && sy < WORLD_HEIGHT - 5) {
			/* Wüstenbrunnen: 3x3 Sandstein-Plattform mit Wasserloch */
			for(int dx = -1; dx <= 1; dx++)
				for(int dz = -1; dz <= 1; dz++) {
					int nx = lx + dx, nz = lz + dz;
					if(nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
					gen_set_block(sc, nx, sy, nz,
					              (dx == 0 && dz == 0) ? BLOCK_AIR : BLOCK_SANDSTONE);
				}
			/* Wasser in der Mitte */
			if(sy - 1 >= 0) gen_set_block(sc, lx, sy - 1, lz, BLOCK_WATER_STILL);
			/* Wände */
			for(int dy = 1; dy <= 2; dy++) {
				if(!gen_inside_chunk(lx, sy + dy, lz)) break;
				gen_set_block(sc, lx - 1, sy + dy, lz, BLOCK_SANDSTONE);
				gen_set_block(sc, lx + 1, sy + dy, lz, BLOCK_SANDSTONE);
				gen_set_block(sc, lx, sy + dy, lz - 1, BLOCK_SANDSTONE);
				gen_set_block(sc, lx, sy + dy, lz + 1, BLOCK_SANDSTONE);
			}
		}
	}
}

static bool gen_cave_is_carvable(uint8_t b) {
	return b == BLOCK_STONE || b == BLOCK_DIRT || b == BLOCK_GRASS
	    || b == BLOCK_COAL_ORE || b == BLOCK_IRON_ORE || b == BLOCK_GOLD_ORE
	    || b == BLOCK_DIAMOND_ORE
	    || b == BLOCK_LAPIS_ORE || b == BLOCK_EMERALD_ORE;
}

static void gen_carve_worm_caves(struct server_chunk* sc, int64_t world_seed,
                                  int world_x0, int world_z0, int sea_level,
                                  const int* surface_map) {
	int cx0 = world_x0 / CHUNK_SIZE;
	int cz0 = world_z0 / CHUNK_SIZE;

	/* Nachbar-Radius 5 Chunks (Tunnel reichen selten weiter) — begrenzt Dichte */
	for(int dcx = -5; dcx <= 5; dcx++) {
		for(int dcz = -5; dcz <= 5; dcz++) {
			int cx = cx0 + dcx;
			int cz = cz0 + dcz;

			uint64_t rng = (uint64_t)((int64_t)cx * 341873128712LL
			             + (int64_t)cz * 132897987541LL + world_seed)
			             & 0xFFFFFFFFFFFFULL;
			rng = (rng ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL;

			/* Wenige Höhlen pro Chunk (0..1) → nicht überfüllt */
			int n_caves = gen_mc_feat_int(&rng, 2);

			for(int c = 0; c < n_caves; c++) {
				float sx = (float)(cx * CHUNK_SIZE + gen_mc_feat_int(&rng, CHUNK_SIZE));
				/* Höhlen tief unten starten: y=8..45 */
				float sy = (float)(8 + gen_mc_feat_int(&rng, 37));
				float sz = (float)(cz * CHUNK_SIZE + gen_mc_feat_int(&rng, CHUNK_SIZE));

				/* Radius 2.5..5.0 → runde Höhlen. Grosse Kammern (Radius bis 7)
				 * nur selten (~1/8) und nur tief unten (y<30), damit sie nicht
				 * zur Oberfläche durchbrechen. */
				float radius = 2.5f + (float)gen_mc_feat_int(&rng, 250) / 100.0f;
				if(gen_mc_feat_int(&rng, 8) == 0 && sy < 30.0f) radius += 2.0f;

				float yaw    = (float)gen_mc_feat_int(&rng, 314) / 50.0f - 3.14f;
				/* Flacherer Pitch (±0.12) → weniger vertikale Überlappung */
				float pitch  = ((float)gen_mc_feat_int(&rng, 100) / 100.0f - 0.5f) * 0.12f;

				int max_steps = 100 + gen_mc_feat_int(&rng, 150);
				for(int step = 0; step < max_steps; step++) {
					float dx = cosf(yaw) * cosf(pitch);
					float dy = sinf(pitch);
					float dz = sinf(yaw) * cosf(pitch);

					sx += dx; sy += dy; sz += dz;

					yaw    += ((float)gen_mc_feat_int(&rng, 100) - 50) / 350.0f;
					pitch  *= 0.92f;
					pitch  += ((float)gen_mc_feat_int(&rng, 100) - 50) / 900.0f;
					/* Pitch hart begrenzen → Tunnel bleiben flach, keine
					 * übereinanderliegenden Parallel-Tunnel */
					if(pitch >  0.22f) pitch =  0.22f;
					if(pitch < -0.22f) pitch = -0.22f;

					/* KEIN Schritt-Überspringen mehr → glatte, lückenlose Tunnel
					 * (verhindert schwebende Blöcke) */

								/* leichte Radius-Variation pro Schritt für natürliche Form */
								float r = radius + sinf((float)step * 0.35f) * 0.35f;
								float rr = r * r;
								int yr = (int)(r * 0.55f) + 1;
								int y0 = (int)sy - yr;
								if(y0 < 1) y0 = 1;

					/* Nur die Spalten im tatsächlichen Radius prüfen.  Der frühere
					 * Vollscan über 16x16 Spalten kostete bei jedem Tunnel-Schritt
					 * Zeit, obwohl die meisten Spalten sicher außerhalb lagen. */
					int lx0 = (int)ceilf(sx - r - (float)world_x0);
					int lx1 = (int)floorf(sx + r - (float)world_x0);
					int lz0 = (int)ceilf(sz - r - (float)world_z0);
					int lz1 = (int)floorf(sz + r - (float)world_z0);
					if(lx0 < 0) lx0 = 0;
					if(lx1 >= CHUNK_SIZE) lx1 = CHUNK_SIZE - 1;
					if(lz0 < 0) lz0 = 0;
					if(lz1 >= CHUNK_SIZE) lz1 = CHUNK_SIZE - 1;

					for(int lx = lx0; lx <= lx1; lx++) {
						for(int lz2 = lz0; lz2 <= lz1; lz2++) {
							float wx = (float)(world_x0 + lx);
							float wz = (float)(world_z0 + lz2);
							float dist2 = (wx-sx)*(wx-sx) + (wz-sz)*(wz-sz);
							if(dist2 > rr) continue;

							int surf = surface_map
							    ? surface_map[lx + lz2 * CHUNK_SIZE]
							    : 60;
							/* Mindestabstand 5 zur Oberfläche. Unter/nahe Wasser:
							 * Höhle klar unter dem Meeresgrund halten, sonst sieht
							 * man das Höhlenmuster durch den Ozeanboden. */
							int max_y = surf - 5;
							if(surf <= sea_level + 2) {
								int under_sea = sea_level - 6;
								if(max_y > under_sea) max_y = under_sea;
							}
							if(max_y < 5) max_y = 5;

							/* vertikal gestaucht (Faktor 4) → flache, breite Höhle:
							 * halbe Höhe bei voller Breite, rundes Ellipsoid */
							int y1 = (int)sy + yr; if(y1 > max_y) y1 = max_y;
							for(int ly = y0; ly <= y1; ly++) {
								float fy = (float)ly;
								float dyv = (fy - sy);
								float d3 = dist2 + dyv * dyv * 4.0f; /* flaches Ellipsoid */
								if(d3 > rr) continue;
								uint8_t b = gen_get_block(sc, lx, ly, lz2);
								if(gen_cave_is_carvable(b))
									gen_set_block(sc, lx, ly, lz2, BLOCK_AIR);
							}
						}
					}

					if(sy < 2.0f || sy > 54.0f) break;
				}
			}
		}
	}
}

static void gen_apply_feature_step(struct server_world* w, struct server_chunk* sc,
								   int* surface_map, Generator* biome_gen,
								   const struct gen_cuberite_runtime_args* gen_args,
								   uint32_t seed, int32_t world_x0, int32_t world_z0,
								   w_coord_t chunk_x, w_coord_t chunk_z, int step) {
	switch(step) {
	case 0:
		/* Erz-Adern VOR den Höhlen → Höhlen legen die Adern frei */
		if(w->generator.finisher_ore_nests)
			gen_place_ore_veins(sc, w->world_seed, world_x0, world_z0, biome_gen);
		if(w->generator.finisher_worm_nest_caves) {
			(void)chunk_x;
			(void)chunk_z;
			(void)biome_gen;
			gen_carve_worm_caves(sc, w->world_seed, world_x0, world_z0,
								 gen_args->sea_level, surface_map);
		}
		return;
	case 1:
		if(w->generator.finisher_worm_nest_caves) {
			/* Schluchten selten halten: sie sind auffällig und sollen nicht
			 * jeden zweiten See/Meer-Einschnitt dominieren. */
			uint32_t ravine_check = gen_hash3i(world_x0 / CHUNK_SIZE, 0,
			                                   world_z0 / CHUNK_SIZE,
			                                   (uint32_t)w->world_seed ^ 0xABBA1234U);
			if((ravine_check % 6) == 0)
				gen_carve_density_canyon(sc, seed, world_x0, world_z0, surface_map,
										 gen_args->sea_level);
		}
		return;
	case 2:
		if(w->generator.finisher_water_lakes)
			gen_place_water_lake(sc, seed, world_x0, world_z0, gen_args->sea_level,
								 w->generator.water_lakes_probability);
		return;
	case 3:
		if(w->generator.finisher_water_springs)
			gen_place_water_springs(sc, seed, world_x0, world_z0,
									gen_args->sea_level);
		return;
	case 4:
		if(w->generator.finisher_lava_lakes)
			gen_place_lava_lake(sc, seed, world_x0, world_z0,
								w->generator.lava_lakes_probability);
		return;
	case 5:
		if(0 && w->generator.finisher_mineshafts)
			gen_place_mineshaft(sc, seed, world_x0, world_z0);
		return;
	case 6:
		if(w->generator.finisher_pre_simulator
		   && (w->generator.pre_simulator_water || w->generator.pre_simulator_lava)) {
			gen_presimulate_fluids_local(sc, w->generator.pre_simulator_water,
										 w->generator.pre_simulator_lava);
		}
		if(w->generator.finisher_worm_nest_caves)
			gen_drain_density_canyon(sc, seed, world_x0, world_z0, surface_map,
									 gen_args->sea_level);
		return;
	case 7:
		if(w->generator.finisher_pre_simulator
		   && w->generator.pre_simulator_falling_blocks) {
			gen_settle_falling_blocks_local(sc);
		} else {
			gen_settle_falling_blocks_local(sc);
		}
		return;
	case 8:
		(void)surface_map;
		(void)gen_args;
		gen_cleanup_floating_ores(sc);
		return;
	case 9:
		if(w->generator.finisher_worm_nest_caves)
			gen_carve_cave_entrance_to_worm(sc, seed, chunk_x, chunk_z,
											surface_map, gen_args->sea_level);
		return;
	case 10:
		if(w->generator.finisher_villages)
			gen_place_village(sc, w, seed, world_x0, world_z0, gen_args->sea_level,
							  biome_gen);
		return;
	case 11:
		if(w->generator.finisher_single_piece_structures)
			gen_place_single_piece_structure(sc, seed, world_x0, world_z0, biome_gen);
		return;
	case 12:
		if(w->generator.finisher_ice)
			gen_place_ice_surfaces(sc, seed, world_x0, world_z0,
								   gen_args->sea_level, biome_gen);
		return;
	case 13:
		if(w->generator.finisher_overworld_clump_flowers)
			gen_place_flower_clumps(sc, seed, world_x0, world_z0, biome_gen);
		return;
	case 14:
		gen_place_dungeons(sc, w->world_seed, world_x0, world_z0);
		return;
	case 15:
		gen_place_desert_structures(sc, w->world_seed, world_x0, world_z0, biome_gen);
		return;
	default:
		return;
	}
}

static void gen_apply_feature_pass(struct server_world* w, struct server_chunk* sc,
								   int* surface_map, Generator* biome_gen,
								   const struct gen_cuberite_runtime_args* gen_args,
								   uint32_t seed, int32_t world_x0, int32_t world_z0,
								   w_coord_t chunk_x, w_coord_t chunk_z) {
	for(int step = 0; step < GEN_FEATURE_STEP_COUNT; step++) {
		gen_apply_feature_step(w, sc, surface_map, biome_gen, gen_args, seed,
							   world_x0, world_z0, chunk_x, chunk_z, step);
	}
}

/* ── RNG-Hilfsfunktionen ── */

static int64_t gen_mc_legacy_next_long(uint64_t* s) {
	*s = (*s * 25214903917ULL + 11ULL) & 0xFFFFFFFFFFFFULL;
	int32_t hi = (int32_t)(*s >> 16);
	*s = (*s * 25214903917ULL + 11ULL) & 0xFFFFFFFFFFFFULL;
	int32_t lo = (int32_t)(*s >> 16);
	return ((int64_t)hi << 32) + (int64_t)(int32_t)lo;
}

static int gen_mc_feat_int(uint64_t* s, int n) {
	*s = (*s * 25214903917ULL + 11ULL) & 0xFFFFFFFFFFFFULL;
	return (int)((uint32_t)(*s >> 17) % (unsigned)n);
}

static void gen_generate_deco_columns(struct server_world* w, struct server_chunk* sc,
									  const uint8_t (*cached_wgen_heights)[CHUNK_SIZE],
									  Generator* biome_gen,
									  const struct gen_cuberite_runtime_args* gen_args,
									  uint32_t seed, int32_t world_x0, int32_t world_z0,
									  int start_col, int num_cols) {
	(void)num_cols;
	/* This phase is advanced in small column-sized steps by the pending
	 * generator, but the decoration pass itself operates on the whole chunk.
	 * Running it for every step repeated all trees, grass and flowers ~20x. */
	if(start_col != 0)
		return;
	(void)gen_args; (void)seed;

	int chunk_x = (int)(world_x0 / CHUNK_SIZE);
	int chunk_z = (int)(world_z0 / CHUNK_SIZE);

	/* ── Decoration-Seed ── */
	int64_t ws = w->world_seed;
	uint64_t lcg = (uint64_t)(ws ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL;
	int64_t S = gen_mc_legacy_next_long(&lcg) | 1;
	int64_t T = gen_mc_legacy_next_long(&lcg) | 1;
	int64_t dec_seed = (int64_t)(chunk_x * 16) * S + (int64_t)(chunk_z * 16) * T ^ ws;

	/* Makro: Feature-RNG initialisieren und nextInt(n) aufrufen */
#define FEAT_RNG_INIT(step, fi) \
	uint64_t feat_rng = (uint64_t)(((dec_seed) + (int64_t)(step) + (int64_t)10000*(fi)) \
	                   ^ (int64_t)25214903917LL) & 0xFFFFFFFFFFFFULL
#define FEAT_INT(n) gen_mc_feat_int(&feat_rng, (n))

	uint8_t hh[CHUNK_SIZE][CHUNK_SIZE];
	const uint8_t (*wgen_heights)[CHUNK_SIZE] = cached_wgen_heights;
	if(!wgen_heights) {
		wgen_gen_t* wg = server_world_wgen_gen(w);
		wgen_gen_surface_heights(wg, chunk_x, chunk_z, hh);
		wgen_heights = hh;
	}

	/* ── BÄUME (Feature-Step 1) — exakte MC-26.3-Versuchsanzahlen ── */
	if(w->generator.finisher_trees) {
		int cx_b = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);
		/* Gewichtete Listen */
		FEAT_RNG_INIT(1, 0);
		int tree_count = 0;
		if(cx_b == dark_forest) {
			tree_count = 16;
		} else if(cx_b == jungle || cx_b == jungle_hills
		          || cx_b == bamboo_jungle || cx_b == bamboo_jungle_hills) {
			/* weighted: 9x50 + 1x51 → meist 50 */
			tree_count = (FEAT_INT(10) == 0) ? 51 : 50;
		} else if(cx_b == forest || cx_b == wooded_hills) {
			/* weighted: 9x6 + 1x7 */
			tree_count = (FEAT_INT(10) == 0) ? 7 : 6;
		} else if(cx_b == birch_forest || cx_b == birch_forest_hills
		          || cx_b == tall_birch_forest || cx_b == tall_birch_hills) {
			tree_count = (FEAT_INT(10) == 0) ? 11 : 10;
		} else if(cx_b == flower_forest) {
			tree_count = (FEAT_INT(10) == 0) ? 7 : 6;
		} else if(cx_b == taiga || cx_b == taiga_hills) {
			tree_count = (FEAT_INT(10) == 0) ? 11 : 10;
		} else if(cx_b == giant_tree_taiga || cx_b == giant_tree_taiga_hills
		          || cx_b == giant_spruce_taiga || cx_b == giant_spruce_taiga_hills) {
			tree_count = (FEAT_INT(10) == 0) ? 11 : 10;
		} else if(cx_b == swamp || cx_b == swamp_hills) {
			tree_count = (FEAT_INT(10) == 0) ? 3 : 2;
		} else if(cx_b == snowy_tundra || cx_b == snowy_mountains || cx_b == ice_spikes) {
			tree_count = (FEAT_INT(10) == 0) ? 1 : 0;
		} else if(cx_b == plains || cx_b == sunflower_plains) {
			/* 19x0 + 1x1 → seltene Bäume */
			tree_count = (FEAT_INT(20) == 0) ? 1 : 0;
		} else if(cx_b == savanna || cx_b == savanna_plateau
		          || cx_b == shattered_savanna || cx_b == shattered_savanna_plateau) {
			tree_count = (FEAT_INT(10) == 0) ? 2 : 1;
		} else if(cx_b == mountains || cx_b == wooded_mountains
		          || cx_b == gravelly_mountains || cx_b == modified_gravelly_mountains) {
			tree_count = (FEAT_INT(10) == 0) ? 2 : 1;
		} else {
			tree_count = 0;
		}

		/* 8× Vanilla-Versuche: doppelt so viele Baeume wie vorher. */
		tree_count *= 8;

		for(int t = 0; t < tree_count; t++) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			/* Blätter-Radius 2: Baum muss mindestens 2 Blöcke vom Rand entfernt sein
			 * damit Blätter vollständig im Chunk bleiben */
			if(lx < 2 || lx > 13 || lz < 2 || lz > 13) continue;
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 2 || sy > 120) continue;
			uint8_t top = sc->ids[S_CHUNK_IDX(lx, sy, lz)];
			if(top != BLOCK_GRASS && top != BLOCK_DIRT && top != BLOCK_MYCELIUM) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			gen_try_place_tree(sc, lx, lz, sy + 1, (uint32_t)dec_seed,
			                   world_x0 + lx, world_z0 + lz, cx_b);
		}
		/* ── GEFALLENE BÄUME (Feature-Step 1, nach Bäumen) ── */
		/* forest/taiga haben gelegentlich liegende Baumstämme */
		{
			int fallen_biome = cx_b;
			bool has_fallen = (fallen_biome == forest || fallen_biome == wooded_hills
			    || fallen_biome == taiga || fallen_biome == taiga_hills
			    || fallen_biome == giant_tree_taiga || fallen_biome == giant_tree_taiga_hills
			    || fallen_biome == birch_forest || fallen_biome == birch_forest_hills);
			if(has_fallen && FEAT_INT(16) == 0) { /* halb so oft wie vorher */
				int lx = 3 + FEAT_INT(10);
				int lz = 3 + FEAT_INT(10);
				int sy = (int)wgen_heights[lx][lz];
				if(sy >= 2 && sy < WORLD_HEIGHT - 2
				   && sc->ids[S_CHUNK_IDX(lx, sy, lz)] == BLOCK_GRASS
				   && sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] == BLOCK_AIR) {
					/* Liegender Stamm: 2-4 Blöcke lang in zufälliger Richtung */
					int len = 2 + FEAT_INT(3);
					int dx = 0, dz2 = 0;
					int dir = FEAT_INT(4);
					if(dir == 0) dx = 1;
					else if(dir == 1) dx = -1;
					else if(dir == 2) dz2 = 1;
					else dz2 = -1;
					/* CavEX-Log: kein Rotations-Support, nur Holztyp (0=Eiche,1=Fichte,2=Birke) */
					uint8_t log_type = (fallen_biome == birch_forest
					    || fallen_biome == birch_forest_hills) ? 2 :
					    (fallen_biome == taiga || fallen_biome == taiga_hills
					    || fallen_biome == giant_tree_taiga || fallen_biome == giant_tree_taiga_hills)
					    ? 1 : 0;
					for(int i = 0; i < len; i++) {
						int nx = lx + dx * i, nz = lz + dz2 * i;
						if(nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) break;
						uint8_t base = sc->ids[S_CHUNK_IDX(nx, sy, nz)];
						if(base != BLOCK_GRASS && base != BLOCK_DIRT && base != BLOCK_STONE) break;
						if(sc->ids[S_CHUNK_IDX(nx, sy + 1, nz)] != BLOCK_AIR) break;
						gen_set_block_with_meta(sc, nx, sy + 1, nz, BLOCK_LOG, log_type);
					}
				}
			}
		}
	}

	/* ── GRAS / FARN / BLUMEN (Feature-Step 2, 3) — exaktere MC-Anzahlen ── */
	if(w->generator.finisher_tall_grass) {
		int cx_g = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);

		/* Gras: MC hat "count"-Placements mit Noise */
		int grass_count = 0;
		if(cx_g == plains || cx_g == sunflower_plains) grass_count = 12;
		else if(cx_g == forest || cx_g == wooded_hills) grass_count = 8;
		else if(cx_g == flower_forest) grass_count = 4; /* flower forest hat stattdessen Blumen */
		else if(cx_g == jungle || cx_g == jungle_hills
		        || cx_g == bamboo_jungle || cx_g == bamboo_jungle_hills) grass_count = 25; /* Dschungel hat viel */
		else if(cx_g == taiga || cx_g == taiga_hills) grass_count = 7;
		else if(cx_g == swamp || cx_g == swamp_hills) grass_count = 12;
		else if(cx_g == birch_forest || cx_g == birch_forest_hills) grass_count = 8;
		else if(cx_g == dark_forest) grass_count = 8;
		else if(cx_g == savanna || cx_g == savanna_plateau) grass_count = 10;
		else grass_count = 3; /* Default für andere Biome */
		grass_count *= 2;

		FEAT_RNG_INIT(2, 0);
		for(int t = 0; t < grass_count; t++) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 1 >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_GRASS) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			int bm = gen_biome_at_safe(biome_gen, world_x0 + lx, world_z0 + lz);
			/* Taiga: Farn, sonst Gras */
			if(bm == taiga || bm == taiga_hills || bm == giant_tree_taiga
			   || bm == giant_tree_taiga_hills || bm == giant_spruce_taiga
			   || bm == giant_spruce_taiga_hills)
				gen_set_block_with_meta(sc, lx, sy + 1, lz, BLOCK_TALL_GRASS, 2); /* fern */
			else
				gen_set_block_with_meta(sc, lx, sy + 1, lz, BLOCK_TALL_GRASS, 1); /* grass */
		}

		/* Blumen (Feature-Step 3) */
		{
			int flower_count = 0;
			if(cx_g == flower_forest) flower_count = 10; /* war 100 */
			else if(cx_g == plains || cx_g == sunflower_plains) flower_count = 3; /* war 4 */
			else flower_count = 1; /* war 2 */

			FEAT_RNG_INIT(3, 0);
			flower_count = flower_count / 2
				+ (((flower_count & 1) && FEAT_INT(2) == 0) ? 1 : 0);
			for(int t = 0; t < flower_count; t++) {
				int lx = FEAT_INT(16);
				int lz = FEAT_INT(16);
				int sy = (int)wgen_heights[lx][lz];
				if(sy < 1 || sy + 1 >= WORLD_HEIGHT) continue;
				if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_GRASS) continue;
				if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
				int r = FEAT_INT(2);
				sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] = r ? BLOCK_FLOWER : BLOCK_ROSE;
			}
		}
	}

	/* ── KAKTEEN (Feature-Step 5) ── */
	{
		int center_biome3 = gen_biome_at_safe(biome_gen,
		                                      world_x0 + CHUNK_SIZE / 2,
		                                      world_z0 + CHUNK_SIZE / 2);
		int cact_tries = (center_biome3 == desert || center_biome3 == desert_hills
		                  || center_biome3 == desert_lakes) ? 3 : 0;
		FEAT_RNG_INIT(5, 0);
		for(int t = 0; t < cact_tries; t++) {
			if(FEAT_INT(6) != 0)
				continue;
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 2 >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_SAND) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			/* Kaktus nur wenn keine direkten Nachbarn */
			if(lx > 0  && sc->ids[S_CHUNK_IDX(lx - 1, sy + 1, lz)] != BLOCK_AIR) continue;
			if(lx < 15 && sc->ids[S_CHUNK_IDX(lx + 1, sy + 1, lz)] != BLOCK_AIR) continue;
			if(lz > 0  && sc->ids[S_CHUNK_IDX(lx, sy + 1, lz - 1)] != BLOCK_AIR) continue;
			if(lz < 15 && sc->ids[S_CHUNK_IDX(lx, sy + 1, lz + 1)] != BLOCK_AIR) continue;
			int h = FEAT_INT(3) + 1; /* 1..3 hoch */
			for(int i = 0; i < h && sy + 1 + i < WORLD_HEIGHT - 1; i++)
				sc->ids[S_CHUNK_IDX(lx, sy + 1 + i, lz)] = BLOCK_CACTUS;
		}
	}

	/* ── SCHILF (Feature-Step 6) ── */
	{
		int center_biome4 = gen_biome_at_safe(biome_gen,
		                                      world_x0 + CHUNK_SIZE / 2,
		                                      world_z0 + CHUNK_SIZE / 2);
		int reed_tries = (center_biome4 == swamp || center_biome4 == river
		                  || center_biome4 == beach
		                  || center_biome4 == frozen_river) ? 20 : 0;
		FEAT_RNG_INIT(6, 0);
		for(int t = 0; t < reed_tries; t++) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 2 >= WORLD_HEIGHT) continue;
			uint8_t top = sc->ids[S_CHUNK_IDX(lx, sy, lz)];
			if(top != BLOCK_GRASS && top != BLOCK_DIRT && top != BLOCK_SAND) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			bool near_water =
				(lx > 0  && gen_is_water(sc, lx - 1, sy, lz))
				|| (lx < 15 && gen_is_water(sc, lx + 1, sy, lz))
				|| (lz > 0  && gen_is_water(sc, lx, sy, lz - 1))
				|| (lz < 15 && gen_is_water(sc, lx, sy, lz + 1));
			if(!near_water) continue;
			int h = FEAT_INT(2) + 2; /* 2..3 hoch */
			for(int i = 0; i < h && sy + 1 + i < WORLD_HEIGHT - 1; i++)
				sc->ids[S_CHUNK_IDX(lx, sy + 1 + i, lz)] = BLOCK_REED;
		}
	}

	/* ── TOTER BUSCH (Feature-Step 7, Wüste/Badlands) ── */
	{
		int center_biome6 = gen_biome_at_safe(biome_gen,
		                                      world_x0 + CHUNK_SIZE / 2,
		                                      world_z0 + CHUNK_SIZE / 2);
		bool is_desert = (center_biome6 == desert || center_biome6 == desert_hills
		                  || center_biome6 == badlands || center_biome6 == badlands_plateau
		                  || center_biome6 == wooded_badlands_plateau);
		int bush_tries = is_desert ? 4 : 0;
		FEAT_RNG_INIT(7, 0);
		for(int t = 0; t < bush_tries; t++) {
			if(FEAT_INT(10) != 0)
				continue;
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 1 >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_SAND) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] = 32; /* dead bush */
		}
	}

	/* ── SEEROSEN (Feature-Step 8, Sumpf) ── */
	if(w->generator.finisher_lilypads) {
		int center_biome7 = gen_biome_at_safe(biome_gen,
		                                      world_x0 + CHUNK_SIZE / 2,
		                                      world_z0 + CHUNK_SIZE / 2);
		int lily_tries = (center_biome7 == swamp || center_biome7 == mangrove_swamp)
		                 ? 8 : 0;
		FEAT_RNG_INIT(8, 0);
		for(int t = 0; t < lily_tries; t++) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 1 >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_WATER_STILL) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] = BLOCK_WATERLILY;
		}
	}

	/* ── PILZE auf Myzel (Feature-Step 9) ── */
	{
		int center_biome8 = gen_biome_at_safe(biome_gen,
		                                      world_x0 + CHUNK_SIZE / 2,
		                                      world_z0 + CHUNK_SIZE / 2);
		int mush_tries = (center_biome8 == mushroom_fields
		                  || center_biome8 == mushroom_field_shore) ? 8 : 0;
		FEAT_RNG_INIT(9, 0);
		for(int t = 0; t < mush_tries; t++) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy < 1 || sy + 1 >= WORLD_HEIGHT) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy, lz)] != BLOCK_MYCELIUM) continue;
			if(sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] != BLOCK_AIR) continue;
			sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] = (FEAT_INT(2) == 0)
				? BLOCK_RED_MUSHROOM : BLOCK_BROWM_MUSHROOM;
		}
	}

	/* ── KÜRBISSE (Feature-Step 10) ── */
	{
		FEAT_RNG_INIT(10, 0);
		int cx_p = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);
		int pumpkin_tries = (cx_p == plains || cx_p == forest
		                     || cx_p == sunflower_plains) ? 1 : 0;
		if(FEAT_INT(32) == 0 && pumpkin_tries > 0) {
			int lx = FEAT_INT(16);
			int lz = FEAT_INT(16);
			int sy = (int)wgen_heights[lx][lz];
			if(sy >= 1 && sy + 1 < WORLD_HEIGHT
			   && sc->ids[S_CHUNK_IDX(lx, sy, lz)] == BLOCK_GRASS
			   && sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] == BLOCK_AIR)
				gen_set_block(sc, lx, sy + 1, lz, BLOCK_PUMPKIN);
		}
	}

	/* ── MELONEN (Feature-Step 11) ── */
	{
		FEAT_RNG_INIT(11, 0);
		int cx_m = gen_biome_at_safe(biome_gen, world_x0 + 8, world_z0 + 8);
		if(cx_m == jungle || cx_m == jungle_hills || cx_m == jungle_edge
		   || cx_m == bamboo_jungle || cx_m == bamboo_jungle_hills) {
			if(FEAT_INT(4) == 0) {
				int lx = FEAT_INT(16);
				int lz = FEAT_INT(16);
				int sy = (int)wgen_heights[lx][lz];
				if(sy >= 1 && sy + 1 < WORLD_HEIGHT
				   && sc->ids[S_CHUNK_IDX(lx, sy, lz)] == BLOCK_GRASS
				   && sc->ids[S_CHUNK_IDX(lx, sy + 1, lz)] == BLOCK_AIR)
					gen_set_block(sc, lx, sy + 1, lz, BLOCK_MELON);
			}
		}
	}

#undef FEAT_RNG_INIT
#undef FEAT_INT
}

static bool gen_finalize_chunk_step(struct server_world* w, struct server_chunk* sc,
									Generator* biome_gen,
									const struct gen_cuberite_runtime_args* gen_args,
									uint32_t seed, int32_t world_x0, int32_t world_z0,
									w_coord_t x, w_coord_t z, struct server_chunk** out,
									int step) {
	switch(step) {
	case 0:
		gen_try_place_dungeon(sc, seed, world_x0, world_z0);
		if(w->generator.finisher_lava_springs || w->generator.finisher_bottom_lava)
			gen_place_lava_sources(sc, seed, world_x0, world_z0);
		if(w->generator.finisher_natural_patches) {
			for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
				for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
					int wx = world_x0 + lx;
					int wz = world_z0 + lz;
					int biome_id = gen_biome_at_safe(biome_gen, wx, wz);
					if(!(isOceanic(biome_id) || biome_id == river || biome_id == swamp))
						continue;
					for(int y = gen_args->sea_level - 12; y <= gen_args->sea_level - 1;
						y++) {
						if(y < 2 || y >= WORLD_HEIGHT - 1)
							continue;
						if(!gen_is_water(sc, lx, y + 1, lz))
							continue;
						uint8_t here = gen_get_block(sc, lx, y, lz);
						if(here != BLOCK_SAND && here != BLOCK_DIRT
						   && here != BLOCK_GRAVEL)
							continue;
						float p = gen_rand01_from_hash(
							gen_hash3i(wx, y, wz, seed ^ 0x7733AA11U));
						if(p > gen_args->clay_threshold)
							gen_set_block(sc, lx, y, lz, BLOCK_CLAY);
						else if(p > gen_args->gravel_patch_threshold)
							gen_set_block(sc, lx, y, lz, BLOCK_GRAVEL);
					}
				}
			}
		}
		return false;
	case 1:
		return false;
	case 2:
		gen_recompute_height_and_skylight(sc);
		gen_propagate_lighting(sc, w->dimension == WORLD_DIM_NETHER);
		dict_server_chunks_set_at(w->chunks, S_CHUNK_ID(x, z), *sc);
		*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
		if(*out != NULL) {
			/* Jeder frisch generierte Chunk wird auf Disk gespeichert, damit er
			 * beim Wiederbetreten geladen statt (teuer) neu generiert wird. */
			(*out)->needs_save = true;
		}
		return *out != NULL;
	default:
		return false;
	}
}

static bool gen_finalize_chunk(struct server_world* w, struct server_chunk* sc,
							   Generator* biome_gen,
							   const struct gen_cuberite_runtime_args* gen_args,
							   uint32_t seed, int32_t world_x0, int32_t world_z0,
							   w_coord_t x, w_coord_t z, struct server_chunk** out) {
	for(int step = 0; step < GEN_FINALIZE_STEP_COUNT; step++) {
		if(gen_finalize_chunk_step(w, sc, biome_gen, gen_args, seed, world_x0,
								   world_z0, x, z, out, step))
			return true;
	}
	return false;
}

/* ─────────── Nether-Chunk-Generierung (B1.7.3-inspiriert) ─────────── */

static void gen_generate_nether_chunk(struct server_world* w,
                                       struct server_chunk* sc,
                                       w_coord_t cx, w_coord_t cz) {
	uint32_t seed = (uint32_t)w->world_seed;
	int32_t wx0   = cx * CHUNK_SIZE;
	int32_t wz0   = cz * CHUNK_SIZE;

	/*
	 * Zwei unabhängige großskalige Noises:
	 *   pn1 — sehr große Hohlräume (Periode ~500 Blöcke horizontal)
	 *   pn2 — mittlere Säulen/Plattformen (Periode ~80 Blöcke)
	 *   pn3 — Feindetail für Oberflächen (Periode ~25 Blöcke)
	 *
	 * Solid-Schwellenwert 0.22: nur die obersten ~38% der Dichteverteilung
	 * sind solid → mind. 62% der mittleren Zone ist Hohlraum.
	 * Boden (y<22) und Decke (y>105) werden per Bias hart auf solid gezogen.
	 */
	PerlinNoise pn1, pn2, pn3;
	uint64_t s1 = (uint64_t)seed * 0x9C3FAB17ULL ^ 0x5A2D8E4CULL;
	uint64_t s2 = (uint64_t)seed * 0x7BE3C491ULL ^ 0xD8F2A063ULL;
	uint64_t s3 = (uint64_t)seed * 0x3DA5F921ULL ^ 0xF0E1D2C3ULL;
	perlinInit(&pn1, &s1);
	perlinInit(&pn2, &s2);
	perlinInit(&pn3, &s3);

	/* Pass 1: Terrain */
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			double wx = wx0 + lx, wz = wz0 + lz;
			for(int y = 0; y < WORLD_HEIGHT; y++) {
				/* Bedrock-Boden und -Decke */
				if(y == 0 || y == WORLD_HEIGHT - 1) {
					gen_set_block(sc, lx, y, lz, BLOCK_BEDROCK);
					continue;
				}
				/* Probabilistisches Bedrock (Boden Y=1-4, Decke Y=123-126) */
				if(y <= 4 || y >= WORLD_HEIGHT - 5) {
					float t = (y <= 4) ? (4.0f - y) / 4.0f
					                   : (y - (WORLD_HEIGHT - 6)) / 5.0f;
					float p = gen_rand01_from_hash(
					    gen_hash3i((int)wx, y, (int)wz, seed ^ 0xFACE0000U));
					if(p < t) {
						gen_set_block(sc, lx, y, lz, BLOCK_BEDROCK);
						continue;
					}
				}

				/* Großer Hohlraum-Noise (dominiert den Gesamtcharakter) */
				double n1 = samplePerlin(&pn1, wx / 500.0, (double)y / 120.0,
				                         wz / 500.0, 0.0, 0.0);
				/* Säulen-/Plattformen-Noise */
				double n2 = samplePerlin(&pn2, wx / 80.0,  (double)y / 40.0,
				                         wz / 80.0,  0.0, 0.0);
				/* Feindetail für rauere Oberflächen */
				double n3 = samplePerlin(&pn3, wx / 25.0,  (double)y / 12.0,
				                         wz / 25.0,  0.0, 0.0);

				/* Dichte: Großraum dominiert, Säulen addieren Struktur */
				double density = n1 * 0.55 + n2 * 0.32 + n3 * 0.13;

				/* Harter Bias für Boden- und Deckenzone */
				if(y < 22)               density += (22.0 - y) / 22.0 * 2.5;
				if(y > WORLD_HEIGHT - 23) density += (y - (WORLD_HEIGHT - 23.0)) / 22.0 * 2.5;

				/* Schwellenwert: solid nur wo density > 0.22 */
				if(density > 0.22) {
					gen_set_block(sc, lx, y, lz, BLOCK_NETHERRACK);
				} else if(y < 32) {
					gen_set_block(sc, lx, y, lz, BLOCK_LAVA_STILL);
				}
				/* sonst: BLOCK_AIR */
			}
		}
	}

	/* Pass 2: Oberflächenfeatures */
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			int wx = wx0 + lx, wz = wz0 + lz;
			for(int y = 2; y < WORLD_HEIGHT - 5; y++) {
				if(gen_get_block(sc, lx, y, lz) != BLOCK_NETHERRACK) continue;
				uint8_t above = gen_get_block(sc, lx, y + 1, lz);
				uint8_t below = gen_get_block(sc, lx, y - 1, lz);
				/* Boden-Oberfläche → Soul Sand oder Kies */
				if(above == BLOCK_AIR || above == BLOCK_LAVA_STILL) {
					uint32_t h = gen_hash3i(wx, y, wz, seed ^ 0x1B2C3D4EU);
					uint8_t r  = h & 0xFF;
					if(r < 48)      gen_set_block(sc, lx, y, lz, BLOCK_SOULSAND);
					else if(r < 62) gen_set_block(sc, lx, y, lz, BLOCK_GRAVEL);
				}
				/* Decken-Oberfläche → Glowstone */
				else if(below == BLOCK_AIR) {
					uint32_t h = gen_hash3i(wx, y, wz, seed ^ 0xE5F6A7B8U);
					if((h & 0xFF) < 24)
						gen_set_block(sc, lx, y, lz, BLOCK_GLOWSTONE);
				}
			}
		}
	}
}

/* ─────────────────────────────────────────────────────────────────────── */

/* Build the biome generator once per seed and reuse it. setupGenerator() and
 * applySeed() are expensive; doing them on every generation step was the real
 * bottleneck (raising the per-tick budget just burned CPU on redundant setup).
 *
 * Kept as a file-static (BSS), NOT inside struct server_world: that struct lives
 * in the stack-allocated server_local, and the cubiomes Generator is large --
 * embedding it overflowed the Wii stack (ISI crash). The local server runs on a
 * single thread, so one shared static is safe. */
static Generator g_biome_gen;
static bool g_biome_gen_ready = false;
static int64_t g_biome_gen_seed = 0;

static wgen_gen_t* g_wgen_gen      = NULL;
static int64_t     g_wgen_gen_seed = 0;

static wgen_gen_t* server_world_wgen_gen(struct server_world* w) {
	if(!g_wgen_gen || g_wgen_gen_seed != w->world_seed) {
		if(g_wgen_gen) wgen_gen_destroy(g_wgen_gen);
		g_wgen_gen = wgen_gen_create(w->world_seed);
		g_wgen_gen_seed = w->world_seed;
	}
	return g_wgen_gen;
}

static Generator* server_world_biome_gen(struct server_world* w) {
	if(!g_biome_gen_ready || g_biome_gen_seed != w->world_seed) {
		setupGenerator(&g_biome_gen, MC_1_21, 0);
		applySeed(&g_biome_gen, DIM_OVERWORLD, (uint64_t)w->world_seed);
		g_biome_gen_seed = w->world_seed;
		g_biome_gen_ready = true;
	}
	return &g_biome_gen;
}

static bool server_world_generate_chunk(struct server_world* w, w_coord_t x,
										w_coord_t z, struct server_chunk** out) {
	assert(w && out);

	if(server_world_is_chunk_loaded(w, x, z)) {
		*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
		return *out != NULL;
	}
	struct server_chunk sc;
	if(!gen_alloc_chunk_buffers(&sc))
		return false;

	/* Nether: eigener Generator, nur Lighting/Dict-Finalisierung */
	if(w->dimension == WORLD_DIM_NETHER) {
		gen_generate_nether_chunk(w, &sc, x, z);
		return gen_finalize_chunk_step(w, &sc, NULL, NULL, 0,
		                               x * CHUNK_SIZE, z * CHUNK_SIZE,
		                               x, z, out, 2);
	}

	struct gen_cuberite_runtime_args gen_args = gen_runtime_args(w);
	int choice_octaves = (w->generator.biomal_noise3d_num_choice_octaves > 0)
		? w->generator.biomal_noise3d_num_choice_octaves : 4;
	uint32_t seed = (uint32_t)w->world_seed;
	Generator* biome_gen = server_world_biome_gen(w);
	int32_t world_x0 = x * CHUNK_SIZE;
	int32_t world_z0 = z * CHUNK_SIZE;
	int surface_map[CHUNK_SIZE * CHUNK_SIZE];
	bool biome_cache_valid = false;
	int biome_cache_origin_x = 0;
	int biome_cache_origin_z = 0;
	int biome_cache_ids[GEN_BIOME_CACHE_WIDTH * GEN_BIOME_CACHE_WIDTH];
	uint8_t wgen_heights[CHUNK_SIZE][CHUNK_SIZE];
	wgen_gen_surface_heights(server_world_wgen_gen(w), (int)x, (int)z,
	                         wgen_heights);
	gen_biome_cache_fill(biome_gen, world_x0, world_z0, &biome_cache_valid,
						 &biome_cache_origin_x, &biome_cache_origin_z,
						 biome_cache_ids);
	gen_biome_cache_activate(biome_cache_valid, biome_cache_origin_x,
							 biome_cache_origin_z, biome_cache_ids);
	gen_generate_terrain_columns(w, &sc, surface_map, wgen_heights, NULL, 0, 0,
								 biome_gen, &gen_args, seed,
								 choice_octaves, world_x0, world_z0, x, z, 0,
								 CHUNK_SIZE * CHUNK_SIZE);
	gen_apply_feature_pass(w, &sc, surface_map, biome_gen, &gen_args, seed,
						   world_x0, world_z0, x, z);
	gen_generate_deco_columns(w, &sc, wgen_heights, biome_gen, &gen_args, seed, world_x0,
							  world_z0, 0,
							  (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2));
	bool ok = gen_finalize_chunk(w, &sc, biome_gen, &gen_args, seed, world_x0,
								 world_z0, x, z, out);
	gen_biome_cache_clear_active();
	return ok;
}

static void server_world_pending_clear(struct server_world* w) {
	if(!w->pending_chunk.active)
		return;
	server_world_chunk_destroy(&w->pending_chunk.chunk);
	memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
}

static bool server_world_pending_start(struct server_world* w, w_coord_t x,
									   w_coord_t z) {
	server_world_pending_clear(w);
	if(!gen_alloc_chunk_buffers(&w->pending_chunk.chunk))
		return false;
	w->pending_chunk.active = true;
	w->pending_chunk.x = x;
	w->pending_chunk.z = z;
	w->pending_chunk.phase = SERVER_WORLD_PENDING_TERRAIN;
	w->pending_chunk.next_column = 0;
	memset(w->pending_chunk.surface_map, 0, sizeof(w->pending_chunk.surface_map));
	gen_pending_biome_cache = (struct gen_biome_cache_storage) {0};
	if(w->dimension == WORLD_DIM_OVERWORLD) {
		ptime_t height_cache_start = time_get();
		Generator* biome_gen = server_world_biome_gen(w);
		wgen_gen_t* wg = server_world_wgen_gen(w);
		int cache_origin_x = x * CHUNK_SIZE - SERVER_WORLD_WGEN_HEIGHT_MARGIN;
		int cache_origin_z = z * CHUNK_SIZE - SERVER_WORLD_WGEN_HEIGHT_MARGIN;
		for(int cz = 0; cz < SERVER_WORLD_WGEN_HEIGHT_WIDTH; cz++)
			for(int cx = 0; cx < SERVER_WORLD_WGEN_HEIGHT_WIDTH; cx++)
				w->pending_chunk.wgen_height_cache[cz][cx]
					= (uint8_t)wgen_gen_world_height(
						wg, cache_origin_x + cx,
						cache_origin_z + cz);
		wgen_gen_surface_heights_from_raw(
			w->pending_chunk.wgen_height_cache[0],
			SERVER_WORLD_WGEN_HEIGHT_WIDTH,
			SERVER_WORLD_WGEN_HEIGHT_MARGIN - 2,
			w->pending_chunk.wgen_heights);
		gen_profile_phase_done(GEN_PROFILE_TERRAIN, height_cache_start);
		gen_biome_cache_fill(biome_gen, x * CHUNK_SIZE, z * CHUNK_SIZE,
							 &gen_pending_biome_cache.valid,
							 &gen_pending_biome_cache.origin_x,
							 &gen_pending_biome_cache.origin_z,
							 gen_pending_biome_cache.ids);
	}
	return true;
}

bool server_world_pending_chunk(struct server_world* w, w_coord_t* x,
								w_coord_t* z) {
	assert(w);
	if(!w->pending_chunk.active)
		return false;
	if(x)
		*x = w->pending_chunk.x;
	if(z)
		*z = w->pending_chunk.z;
	return true;
}

/* progress 0..100 of the chunk currently being generated, or -1 if none */
int server_world_pending_progress(struct server_world* w) {
	assert(w);
	if(!w->pending_chunk.active)
		return -1;
	int nc = w->pending_chunk.next_column;
	switch(w->pending_chunk.phase) {
		case SERVER_WORLD_PENDING_TERRAIN:
			return (int)(45.0 * nc / (CHUNK_SIZE * CHUNK_SIZE));
		case SERVER_WORLD_PENDING_FEATURES:
			return 45 + (int)(10.0 * nc / GEN_FEATURE_STEP_COUNT);
		case SERVER_WORLD_PENDING_DECO:
			return 55
				+ (int)(40.0 * nc / ((CHUNK_SIZE - 2) * (CHUNK_SIZE - 2)));
		case SERVER_WORLD_PENDING_FINALIZE:
			return 95 + (int)(5.0 * nc / GEN_FINALIZE_STEP_COUNT);
		default: return 0;
	}
}

static bool server_world_advance_pending(struct server_world* w,
										 struct server_chunk** out) {
	assert(w && out);
	if(!w->pending_chunk.active)
		return false;

	/* Nether: gesamten Chunk in TERRAIN-Phase generieren, direkt finalisieren */
	if(w->dimension == WORLD_DIM_NETHER) {
		switch(w->pending_chunk.phase) {
		case SERVER_WORLD_PENDING_TERRAIN: {
			ptime_t phase_start = time_get();
			gen_generate_nether_chunk(w, &w->pending_chunk.chunk,
			                          w->pending_chunk.x, w->pending_chunk.z);
			gen_profile_phase_done(GEN_PROFILE_TERRAIN, phase_start);
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FINALIZE;
			w->pending_chunk.next_column = 0;
			return false;
		}
		case SERVER_WORLD_PENDING_FINALIZE: {
			int32_t wx0 = w->pending_chunk.x * CHUNK_SIZE;
			int32_t wz0 = w->pending_chunk.z * CHUNK_SIZE;
			ptime_t phase_start = time_get();
			bool ok = gen_finalize_chunk_step(w, &w->pending_chunk.chunk,
			                                  NULL, NULL, 0, wx0, wz0,
			                                  w->pending_chunk.x,
			                                  w->pending_chunk.z, out, 2);
			gen_profile_phase_done(GEN_PROFILE_FINALIZE, phase_start);
			if(ok) {
				g_gen_profile.chunks++;
				memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
			}
			return ok;
		}
		default:
			return false;
		}
	}

	struct gen_cuberite_runtime_args gen_args = gen_runtime_args(w);
	int choice_octaves = (w->generator.biomal_noise3d_num_choice_octaves > 0)
		? w->generator.biomal_noise3d_num_choice_octaves : 4;
	uint32_t seed = (uint32_t)w->world_seed;
	Generator* biome_gen = server_world_biome_gen(w);
	int32_t world_x0 = w->pending_chunk.x * CHUNK_SIZE;
	int32_t world_z0 = w->pending_chunk.z * CHUNK_SIZE;
	gen_biome_cache_activate(gen_pending_biome_cache.valid,
							 gen_pending_biome_cache.origin_x,
							 gen_pending_biome_cache.origin_z,
							 gen_pending_biome_cache.ids);

	switch(w->pending_chunk.phase) {
	case SERVER_WORLD_PENDING_TERRAIN: {
		ptime_t phase_start = time_get();
		gen_generate_terrain_columns(w, &w->pending_chunk.chunk,
									 w->pending_chunk.surface_map,
									 w->pending_chunk.wgen_heights,
									 w->pending_chunk.wgen_height_cache[0],
									 world_x0 - SERVER_WORLD_WGEN_HEIGHT_MARGIN,
									 world_z0 - SERVER_WORLD_WGEN_HEIGHT_MARGIN,
									 biome_gen,
									 &gen_args, seed, choice_octaves, world_x0,
									 world_z0, w->pending_chunk.x,
									 w->pending_chunk.z,
									 w->pending_chunk.next_column,
									 GEN_CHUNK_COLUMNS_PER_STEP);
		gen_profile_phase_done(GEN_PROFILE_TERRAIN, phase_start);
		w->pending_chunk.next_column += GEN_CHUNK_COLUMNS_PER_STEP;
		if(w->pending_chunk.next_column >= CHUNK_SIZE * CHUNK_SIZE) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FEATURES;
			w->pending_chunk.next_column = 0;
		}
		return false;
	}

	case SERVER_WORLD_PENDING_FEATURES: {
		ptime_t phase_start = time_get();
		gen_apply_feature_step(w, &w->pending_chunk.chunk,
							   w->pending_chunk.surface_map, biome_gen,
							   &gen_args, seed, world_x0, world_z0,
								 w->pending_chunk.x, w->pending_chunk.z,
								 w->pending_chunk.next_column);
		gen_profile_phase_done(GEN_PROFILE_FEATURES, phase_start);
		w->pending_chunk.next_column++;
		if(w->pending_chunk.next_column >= GEN_FEATURE_STEP_COUNT) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_DECO;
			w->pending_chunk.next_column = 0;
		}
		return false;
	}

	case SERVER_WORLD_PENDING_DECO: {
		ptime_t phase_start = time_get();
		gen_generate_deco_columns(w, &w->pending_chunk.chunk,
								  w->pending_chunk.wgen_heights, biome_gen,
								  &gen_args, seed, world_x0, world_z0,
								  w->pending_chunk.next_column,
								  GEN_CHUNK_COLUMNS_PER_STEP);
		gen_profile_phase_done(GEN_PROFILE_DECO, phase_start);
		w->pending_chunk.next_column += GEN_CHUNK_COLUMNS_PER_STEP;
		if(w->pending_chunk.next_column >= (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2)) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FINALIZE;
			w->pending_chunk.next_column = 0;
		}
		return false;
	}

	case SERVER_WORLD_PENDING_FINALIZE: {
		ptime_t phase_start = time_get();
		bool ok = gen_finalize_chunk_step(
			w, &w->pending_chunk.chunk, biome_gen, &gen_args, seed, world_x0, world_z0,
			w->pending_chunk.x, w->pending_chunk.z, out,
			w->pending_chunk.next_column);
		gen_profile_phase_done(GEN_PROFILE_FINALIZE, phase_start);
		w->pending_chunk.next_column++;
		if(ok || w->pending_chunk.next_column >= GEN_FINALIZE_STEP_COUNT) {
			if(ok)
				g_gen_profile.chunks++;
			memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
			gen_biome_cache_clear_active();
		}
		return ok;
	}

	case SERVER_WORLD_PENDING_NONE:
	default:
		return false;
	}
}
static void random_unit_vector(vec3 out) {
    float z = 2.0f * ((rand()/(float)RAND_MAX) - 0.5f);
    float t = 2.0f * M_PI * (rand()/(float)RAND_MAX);
    float r = sqrtf(1.0f - z*z);
    out[0] = r * cosf(t);
    out[1] = r * sinf(t);
    out[2] = z;
}



void server_world_chunk_destroy(struct server_chunk* sc) {
	assert(sc);

	if(sc->from_pool) {
		/* Slot in den Pool zurückgeben — kein free (die Puffer gehören dem
		 * einmalig allozierten Pool-Block). */
		sc_pool_return(sc);
	} else {
		free(sc->ids);
		free(sc->metadata);
		free(sc->lighting_sky);
		free(sc->lighting_torch);
		free(sc->heightmap);
	}
	sc->ids = NULL;
	sc->metadata = NULL;
	sc->lighting_sky = NULL;
	sc->lighting_torch = NULL;
	sc->heightmap = NULL;
	sc->from_pool = false;
	sc->pool_slot = -1;
}

void server_world_set_cuberite_defaults(struct server_world* w) {
	assert(w);
	w->generator = (struct server_world_cuberite_config) {
		.biome_gen = SERVER_WORLD_CUBERITE_BIOME_GEN_GROWN,
		.shape_gen = SERVER_WORLD_CUBERITE_SHAPE_GEN_BIOMAL_NOISE_3D,
		.composition_gen = SERVER_WORLD_CUBERITE_COMPOSITION_GEN_BIOMAL,
		.biome_gen_cache_size = 16,
		.biome_gen_multi_cache_length = 128,
		.sea_level = 62,
		.biomal_noise3d_frequency_x = 40.0f,
		.biomal_noise3d_frequency_y = 40.0f,
		.biomal_noise3d_frequency_z = 40.0f,
		.biomal_noise3d_base_frequency_x = 40.0f,
		.biomal_noise3d_base_frequency_z = 40.0f,
		.biomal_noise3d_choice_frequency_x = 40.0f,
		.biomal_noise3d_choice_frequency_y = 80.0f,
		.biomal_noise3d_choice_frequency_z = 40.0f,
		.biomal_noise3d_air_threshold = 0.0f,
		.biomal_noise3d_num_choice_octaves = 4,
		.biomal_noise3d_num_density_octaves = 6,
		.biomal_noise3d_num_base_octaves = 6,
		.biomal_noise3d_base_amplitude = 1.0f,
		.composition_gen_cache_size = 64,
		.rough_ravines_grid_size = 256,
		.rough_ravines_max_offset = 128,
		.rough_ravines_max_size = 128,
		.rough_ravines_min_size = 64,
		.rough_ravines_max_center_width = 8.0f,
		.rough_ravines_min_center_width = 2.0f,
		.rough_ravines_max_roughness = 0.2f,
		.rough_ravines_min_roughness = 0.05f,
		.rough_ravines_max_floor_height_edge = 8.0f,
		.rough_ravines_min_floor_height_edge = 30.0f,
		.rough_ravines_max_floor_height_center = 20.0f,
		.rough_ravines_min_floor_height_center = 6.0f,
		.rough_ravines_max_ceiling_height_edge = 56.0f,
		.rough_ravines_min_ceiling_height_edge = 38.0f,
		.rough_ravines_max_ceiling_height_center = 58.0f,
		.rough_ravines_min_ceiling_height_center = 36.0f,
		.worm_nest_caves_size = 40,
		.worm_nest_caves_grid = 128,
		.worm_nest_max_offset = 16,
		.water_lakes_probability = 25,
		.lava_lakes_probability = 10,
		.mineshafts_grid_size = 512,
		.mineshafts_max_offset = 256,
		.mineshafts_max_system_size = 160,
		.mineshafts_chance_corridor = 600,
		.mineshafts_chance_crossing = 200,
		.mineshafts_chance_staircase = 30,
		.bottom_lava_level = 10,
#ifdef ALL_FALSE_FINISHER
		.finisher_rough_ravines = 			false,
		.finisher_worm_nest_caves = 		false,
		.finisher_water_lakes = 			false,
		.finisher_water_springs = 			false,
		.finisher_lava_lakes = 				false,
		.finisher_lava_springs = 			false,
		.finisher_ore_nests = 				false,
		.finisher_mineshafts = 				false,
		.finisher_trees = 					false,
		.finisher_villages = 				false,
		.finisher_single_piece_structures = false,
		.finisher_tall_grass = 				false,
		.finisher_sprinkle_foliage = 		false,
		.finisher_ice = 					false,
		.finisher_snow = 					false,
		.finisher_lilypads = 				false,
		.finisher_bottom_lava = 			false,
		.finisher_dead_bushes = 			false,
		.finisher_natural_patches = 		false,
		.finisher_pre_simulator = 			false,
		.pre_simulator_falling_blocks = 	false,
		.pre_simulator_water = 				false,
		.pre_simulator_lava = 				false,
		.finisher_animals = 				false,
		.finisher_overworld_clump_flowers = false,
#else
		.finisher_rough_ravines = 			false,
		.finisher_worm_nest_caves = 		true,
		.finisher_water_lakes = 			true,
		.finisher_water_springs = 			true,
		.finisher_lava_lakes = 				true,
		.finisher_lava_springs = 			true,
		.finisher_ore_nests = 				true,
		.finisher_mineshafts = 				true,
		.finisher_trees = 					true,
		.finisher_villages = 				true,
		.finisher_single_piece_structures = true,
		.finisher_tall_grass = 				true,
		.finisher_sprinkle_foliage = 		true,
		.finisher_ice = 					true,
		.finisher_snow = 					true,
		.finisher_lilypads = 				true,
		.finisher_bottom_lava = 			true,
		.finisher_dead_bushes = 			true,
		.finisher_natural_patches = 		true,
		.finisher_pre_simulator = 			true,
		.pre_simulator_falling_blocks = 	true,
		.pre_simulator_water = 				true,
		.pre_simulator_lava = 				true,
		.finisher_animals = 				true,
		.finisher_overworld_clump_flowers = true,
#endif
	};
}

void server_world_create(struct server_world* w, string_t level_name,
						 enum world_dim dimension) {
	assert(w && dimension >= -1 && dimension <= 0);

	if(w->initialized)
		server_world_destroy(w);

	dict_server_chunks_init(w->chunks);
	ilist_regions_init(w->loaded_regions_lru);
	string_init_set(w->level_name, level_name);
	w->dimension = dimension;
	w->world_seed = 0;
	memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
	server_world_set_cuberite_defaults(w);
	w->loaded_regions_length = 0;
	w->initialized = true;
}

void server_world_set_seed(struct server_world* w, int64_t seed) {
	assert(w);
	w->world_seed = seed;
}

	void server_world_destroy(struct server_world* w) {
		assert(w);

		if(!w->initialized)
			return;
		// m-dict iterators assert on uninitialized dicts (index == NULL). In
		// practice this can happen if a caller destroys a zeroed server_world
		// without having created it successfully.
		if(w->chunks->index == NULL) {
			// Can't safely iterate/clear an uninitialized dict. Just mark the
			// world as destroyed to avoid iterator asserts.
			server_world_pending_clear(w);
			w->initialized = false;
			return;
		}

		server_world_pending_clear(w);

		dict_server_chunks_it_t it;
		dict_server_chunks_it(it, w->chunks);

	while(!dict_server_chunks_end_p(it)) {
		struct server_chunk* sc = &dict_server_chunks_ref(it)->value;
		int64_t id = dict_server_chunks_ref(it)->key;
		server_world_save_chunk_obj(w, false, S_CHUNK_X(id), S_CHUNK_Z(id), sc);
		server_world_chunk_destroy(sc);

		dict_server_chunks_next(it);
	}

		dict_server_chunks_clear(w->chunks);
		string_clear(w->level_name);
		w->initialized = false;
	}

static bool server_chunk_get_block(void* user, c_coord_t x, w_coord_t y,
								   c_coord_t z, struct block_data* blk) {
	assert(user && blk);
	struct server_chunk* sc = user;

	if(y < 0 || y >= WORLD_HEIGHT)
		return false;

	size_t idx = S_CHUNK_IDX(x, y, z);

	*blk = (struct block_data) {
		.type = sc->ids[idx],
		.metadata = nibble_read(sc->metadata, idx),
		.sky_light = nibble_read(sc->lighting_sky, idx),
		.torch_light = nibble_read(sc->lighting_torch, idx),
	};

	return true;
}

static bool server_world_light_get_block(void* user, w_coord_t x, w_coord_t y,
										 w_coord_t z, struct block_data* blk,
										 uint8_t* height) {
	assert(user);
	struct server_world* w = user;

	if(y < 0 || y >= WORLD_HEIGHT)
		return false;

	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));

	if(!sc)
		return false;

	if(blk)
		server_chunk_get_block(sc, W2C_COORD(x), y, W2C_COORD(z), blk);

	if(height)
		*height = sc->heightmap[W2C_COORD(x) + W2C_COORD(z) * CHUNK_SIZE];

	return true;
}

static void server_world_light_set_light(void* user, w_coord_t x, w_coord_t y,
										 w_coord_t z, uint8_t light) {
	assert(user);
	if(y < 0 || y >= WORLD_HEIGHT)
		return;
	struct server_world* w = user;
	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));
	if(!sc)
		return;

	size_t idx = S_CHUNK_IDX(x, y, z);
	nibble_write(sc->lighting_sky, idx, light & 0xF);
	nibble_write(sc->lighting_torch, idx, light >> 4);
	sc->modified = true;
}

bool server_world_get_block(struct server_world* w, w_coord_t x, w_coord_t y,
							w_coord_t z, struct block_data* blk) {
	assert(w && blk);

	if(y < 0 || y >= WORLD_HEIGHT)
		return false;

	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));

	if(!sc)
		return false;

	size_t idx = S_CHUNK_IDX(W2C_COORD(x), y, W2C_COORD(z));

	*blk = (struct block_data) {
		.type = sc->ids[idx],
		.metadata = nibble_read(sc->metadata, idx),
		.sky_light = nibble_read(sc->lighting_sky, idx),
		.torch_light = nibble_read(sc->lighting_torch, idx),
	};

	return true;
}

int server_world_find_ground_y(struct server_world* w, w_coord_t x,
							   w_coord_t z) {
	assert(w);

	/* same rule as the working animal (pig/sheep) spawn: top-down, ground must
	 * be grass/dirt with two air blocks (feet + head) above it. Returns the
	 * feet Y. */
	struct block_data ground, body, head;
	for(w_coord_t y = WORLD_HEIGHT - 2; y >= 1; y--) {
		if(!server_world_get_block(w, x, y - 1, z, &ground)
		   || !server_world_get_block(w, x, y, z, &body)
		   || !server_world_get_block(w, x, y + 1, z, &head))
			continue; /* chunk not loaded / out of range */

		if((ground.type == BLOCK_GRASS || ground.type == BLOCK_DIRT)
		   && body.type == BLOCK_AIR && head.type == BLOCK_AIR)
			return (int)y; /* feet stand on top of the ground block */
	}

	return -1; /* no grass/dirt surface with air above */
}

bool server_world_find_spawn(struct server_world* w, w_coord_t x0, w_coord_t z0,
							 int max_radius, w_coord_t* out_x, int* out_y,
							 w_coord_t* out_z) {
	assert(w && out_x && out_y && out_z);

	/* 1) prefer real ground (grass/dirt) -> spiral outward from the centre */
	for(int r = 0; r <= max_radius; r++) {
		for(int dz = -r; dz <= r; dz++) {
			for(int dx = -r; dx <= r; dx++) {
				if(r != 0 && abs(dx) != r && abs(dz) != r)
					continue; /* only the outer ring of this radius */

				int gy = server_world_find_ground_y(w, x0 + dx, z0 + dz);
				if(gy >= 0) {
					*out_x = x0 + dx;
					*out_y = gy;
					*out_z = z0 + dz;
					return true;
				}
			}
		}
	}

	/* 2) fallback: no land nearby (e.g. ocean) -> stand on the topmost block of
	 * the centre column so the player never spawns mid-air */
	for(w_coord_t y = WORLD_HEIGHT - 1; y >= 1; y--) {
		struct block_data b;
		if(!server_world_get_block(w, x0, y, z0, &b))
			continue;
		if(b.type != BLOCK_AIR) {
			*out_x = x0;
			*out_y = (int)y + 1;
			*out_z = z0;
			return true;
		}
	}

	return false;
}

bool server_world_set_block(struct server_local* s, w_coord_t x, w_coord_t y, w_coord_t z, struct block_data blk) {
    struct server_world* w = AWORLD(s);
	assert(w);
	if(y < 0 || y >= WORLD_HEIGHT)
		return false;

	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));

	if(sc) {
		size_t idx = S_CHUNK_IDX(x, y, z);
		struct block_data old_blk = {
			.type = sc->ids[idx],
			.metadata = nibble_read(sc->metadata, idx),
			.sky_light = nibble_read(sc->lighting_sky, idx),
			.torch_light = nibble_read(sc->lighting_torch, idx),
		};
		bool redstone_visual_update
			= server_world_is_redstone_visual_block(old_blk.type)
			  && server_world_is_redstone_visual_block(blk.type);

		if(old_blk.type == blk.type
		   && old_blk.metadata == (blk.metadata & 0x0F))
			return true;

		sc->modified = true;
		sc->needs_save = true; /* echte Spieleraenderung -> muss auf Disk */
		if(redstone_visual_update) {
			blk.sky_light = old_blk.sky_light;
			blk.torch_light = old_blk.torch_light;
		}
		sc->ids[idx] = blk.type;
		nibble_write(sc->metadata, idx, blk.metadata);
		sc->tick_valid = false; /* tick_count neu zaehlen beim naechsten Tick */

		if(w->dimension != WORLD_DIM_NETHER)
			lighting_heightmap_update(sc->heightmap, W2C_COORD(x), y,
									  W2C_COORD(z), blk.type,
									  server_chunk_get_block, sc);

		if(!redstone_visual_update)
			lighting_update_at_block(
				(struct world_modification_entry) {
					.x = x,
					.y = y,
					.z = z,
					.blk = blk,
				},
				w->dimension == WORLD_DIM_NETHER, server_world_light_get_block,
				server_world_light_set_light, w);

		struct client_rpc set_block_call = {
			.type = CRPC_SET_BLOCK,
			.payload.set_block.x = x,
			.payload.set_block.y = y,
			.payload.set_block.z = z,
			.payload.set_block.block = blk,
			.payload.set_block.dimension = w->dimension,
			.payload.set_block.placement_id = s->placement_sync_pending
				? s->placement_sync_id : 0,
			.payload.set_block.placement_player_id = s->placement_sync_player_id,
			.payload.set_block.suppress_block_particles = s->suppress_block_particles,
		};
		if(s->placement_sync_pending)
			BLOCK_PLACE_SERVER_PRINTF(
			       "[PLACE_SERVER_TAG] id=%u player=%u pos=(%d,%d,%d)\n",
			       s->placement_sync_id, s->placement_sync_player_id,
			       x, y, z);
		if(s->placement_sync_pending)
			s->placement_sync_pending = false;
		if(redstone_visual_update)
			clin_rpc_try_send(&set_block_call);
		else
			clin_rpc_send(&set_block_call);
	}

    static const int dx[6] = {  1, -1,  0,  0,  0,  0 };
    static const int dy[6] = {  0,  0,  0,  0,  1, -1 };
    static const int dz[6] = {  0,  0,  1, -1,  0,  0 };

    for (int i = 0; i < 6; i++) {
        w_coord_t nx = x + dx[i];
        w_coord_t ny = y + dy[i];
        w_coord_t nz = z + dz[i];

        struct block_data nb;
        if (!server_world_get_block(w, nx, ny, nz, &nb))
            continue;

        // wake adjacent water so it re-evaluates its flow (into a new gap, or
        // away from a block that was just placed)
        if (nb.type == BLOCK_WATER_STILL || nb.type == BLOCK_WATER_FLOW)
            server_local_schedule_fluid(s, nx, ny, nz);

        const struct block* b = blocks[nb.type];
        if (b && b->onNeighbourBlockChange) {
            struct block_info info = {
                .block      = &nb,
                .neighbours = NULL,
                .x          = nx,
                .y          = ny,
                .z          = nz
            };
            b->onNeighbourBlockChange(s, &info);
        }
    }

    // if the block we just placed is water itself, wake it too so it starts
    // flowing (bucket placement, or a freshly-spread flow cell)
    if (blk.type == BLOCK_WATER_STILL || blk.type == BLOCK_WATER_FLOW)
        server_local_schedule_fluid(s, x, y, z);

	return sc;
}

bool server_world_furthest_chunk(struct server_world* w, w_coord_t dist,
								 w_coord_t px, w_coord_t pz, w_coord_t* x,
								 w_coord_t* z) {
	assert(w && x && z);

	dict_server_chunks_it_t it;
	dict_server_chunks_it(it, w->chunks);

	w_coord_t furthest_dist2 = -1;
	bool found = false;
	while(!dict_server_chunks_end_p(it)) {
		int64_t id = dict_server_chunks_ref(it)->key;
		w_coord_t d = CHUNK_DIST2(px, S_CHUNK_X(id), pz, S_CHUNK_Z(id));
		dict_server_chunks_next(it);

		if((abs(px - S_CHUNK_X(id)) > dist || abs(pz - S_CHUNK_Z(id)) > dist)
		   && d > furthest_dist2) {
			*x = S_CHUNK_X(id);
			*z = S_CHUNK_Z(id);
			furthest_dist2 = d;
			found = true;
		}
	}

	return found;
}

bool server_world_is_chunk_loaded(struct server_world* w, w_coord_t x,
								  w_coord_t z) {
	assert(w);
	return dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z)) != NULL;
}

/* Weckt instabiles Wasser eines frisch geladenen Chunks: jede Wasserzelle, die
 * an Luft grenzt (unten oder seitlich), wird in die Fluid-Queue des Servers
 * gelegt. Das echte onWorldTick-Fluss-System (server_local_tick_fluids) laesst
 * sie dann im Hintergrund fliessen — begrenzt pro Tick, also ohne Lag.
 * Stabiles Wasser (Ozean/See, nur von Wasser umgeben) wird NICHT geweckt. */
void server_world_wake_chunk_water(struct server_local* s, struct server_chunk* sc,
								   w_coord_t cx, w_coord_t cz) {
	if(s == NULL || sc == NULL)
		return;
	int base_x = (int)cx * CHUNK_SIZE;
	int base_z = (int)cz * CHUNK_SIZE;
	for(int y = 1; y < WORLD_HEIGHT - 1; y++) {
		for(int lx = 0; lx < CHUNK_SIZE; lx++) {
			for(int lz = 0; lz < CHUNK_SIZE; lz++) {
				uint8_t b = sc->ids[S_CHUNK_IDX(lx, y, lz)];
				if(b != BLOCK_WATER_STILL && b != BLOCK_WATER_FLOW)
					continue;
				bool unstable = (sc->ids[S_CHUNK_IDX(lx, y - 1, lz)] == BLOCK_AIR);
				if(!unstable && lx > 0)
					unstable = (sc->ids[S_CHUNK_IDX(lx - 1, y, lz)] == BLOCK_AIR);
				if(!unstable && lx < CHUNK_SIZE - 1)
					unstable = (sc->ids[S_CHUNK_IDX(lx + 1, y, lz)] == BLOCK_AIR);
				if(!unstable && lz > 0)
					unstable = (sc->ids[S_CHUNK_IDX(lx, y, lz - 1)] == BLOCK_AIR);
				if(!unstable && lz < CHUNK_SIZE - 1)
					unstable = (sc->ids[S_CHUNK_IDX(lx, y, lz + 1)] == BLOCK_AIR);
				if(unstable)
					server_local_schedule_fluid(s, base_x + lx, y, base_z + lz);
			}
		}
	}
}

bool server_world_load_chunk(struct server_world* w, w_coord_t x, w_coord_t z,
							 struct server_chunk** sc) {
	assert(w && sc);
	*sc = NULL;

	if(server_world_is_chunk_loaded(w, x, z))
		return false;

	if(w->pending_chunk.active) {
		return server_world_advance_pending(w, sc);
	}

	// Ensure the corresponding region archive is loaded (LRU is tiny; scanning
	// can evict the region we need before load happens).
	struct region_archive* ra = server_world_chunk_region(w, x, z);
	if(!ra)
		return server_world_pending_start(w, x, z)
			? server_world_advance_pending(w, sc)
			: false;

	bool chunk_exists = false;
	if(!region_archive_contains(ra, x, z, &chunk_exists) || !chunk_exists)
		return server_world_pending_start(w, x, z)
			? server_world_advance_pending(w, sc)
			: false;

	/* Puffer aus dem Chunk-Pool holen (fragmentierungsfrei) und die Blockdaten
	 * hineinkopieren -- kein malloc/free-Churn pro Disk-Load mehr (das war der
	 * Grund, warum mem2arena beim Erkunden generierter Welten auf 0 kroch). */
	struct server_chunk tmp;
	if(!gen_alloc_chunk_buffers(&tmp))
		return false;
	tmp.modified = false; /* frisch von Disk -> unveraendert, kein Save noetig */

	if(!region_archive_get_blocks(ra, x, z, &tmp)) {
		server_world_chunk_destroy(&tmp); /* Pool-Slot zurueckgeben */
		return false;
	}

	dict_server_chunks_set_at(w->chunks, S_CHUNK_ID(x, z), tmp);
	*sc = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
	return true;
}

void server_world_save_chunk(struct server_world* w, bool erase, w_coord_t x,
							 w_coord_t z) {
	assert(w);
	struct server_chunk* c
		= dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
	if(c)
		server_world_save_chunk_obj(w, erase, x, z, c);
}

void server_world_save_chunk_obj(struct server_world* w, bool erase,
								 w_coord_t x, w_coord_t z,
								 struct server_chunk* c) {
	assert(w && c);

	/* Chunks mit needs_save werden beim Entladen auf Disk geschrieben:
	 * - frisch generierte Chunks (needs_save beim Generieren gesetzt)
	 * - vom Spieler geaenderte Chunks
	 * So wird ein Chunk beim Wiederbetreten von Disk geladen statt teuer
	 * neu generiert. Der Save ist best-effort: schlaegt er unter Speicher-
	 * druck fehl, wird der Chunk trotzdem verworfen (siehe erase unten) -
	 * das verhindert den OOM-Deadlock. */
	if(c->needs_save) {
		// load region archive into cache
		struct region_archive tmp;
		struct region_archive* ra = server_world_chunk_region(w, x, z);

		if(!ra) {
			if(!region_archive_create_new(&tmp, w->level_name,
										  CHUNK_REGION_COORD(x),
										  CHUNK_REGION_COORD(z), w->dimension))
				goto save_done;
			ra = &tmp;
		}

		bool saved = region_archive_set_blocks(ra, x, z, c);
		if(saved) {
			c->modified = false;
			c->needs_save = false;
		}
#ifdef CHUNK_DEBUG
		else
			fprintf(stderr, "server_world_save_chunk_obj: failed to save edited chunk %d,%d\n", (int)x, (int)z);
#endif
	}
save_done:

	if(erase) {
		/* IMMER verwerfen — auch wenn das Speichern gerade fehlgeschlagen ist.
		 * Früher wurde ein noch-nicht-gespeicherter (needs_save) Chunk im RAM
		 * behalten, um Datenverlust zu vermeiden. Das erzeugte aber einen fatalen
		 * Deadlock: unter Speicherdruck scheitert das Speichern (braucht malloc,
		 * das es nicht gibt) -> Chunk bleibt -> wird jeden Tick erneut als
		 * "entfernteste" gewählt -> Endlosschleife, die Eviction kommt nie zu den
		 * anderen (freigebbaren) Chunks -> totaler Stillstand.
		 * Ein verlorener Edit unter Extremdruck ist das kleinere Übel gegenüber
		 * einem hängenden Spiel; das Speichern oben ist best-effort. */
		server_world_chunk_destroy(c);
		dict_server_chunks_erase(w->chunks, S_CHUNK_ID(x, z));
	}
}

bool server_world_disk_has_chunk(struct server_world* w, w_coord_t x,
								 w_coord_t z) {
	struct region_archive* ra = server_world_chunk_region(w, x, z);
	bool chunk_exists;
	return ra ?
		(region_archive_contains(ra, x, z, &chunk_exists) && chunk_exists) :
		false;
}

struct region_archive* server_world_chunk_region(struct server_world* w,
												 w_coord_t x, w_coord_t z) {
	assert(w);

	for(size_t k = 0; k < w->loaded_regions_length; k++) {
		bool chunk_exists;
		if(region_archive_contains(w->loaded_regions + k, x, z,
								   &chunk_exists)) {
			ilist_regions_unlink(w->loaded_regions + k);
			ilist_regions_push_back(w->loaded_regions_lru,
									w->loaded_regions + k);
			return w->loaded_regions + k;
		}
	}

	struct region_archive ra;
	if(!region_archive_create(&ra, w->level_name, CHUNK_REGION_COORD(x),
							  CHUNK_REGION_COORD(z), w->dimension))
		return NULL;

	struct region_archive* lru;
	if(ilist_regions_size(w->loaded_regions_lru) < MAX_REGIONS) {
		assert(w->loaded_regions_length < MAX_REGIONS);
		lru = w->loaded_regions + (w->loaded_regions_length++);
	} else {
		lru = ilist_regions_pop_front(w->loaded_regions_lru);
		region_archive_destroy(lru);
	}

	*lru = ra;
	ilist_regions_push_back(w->loaded_regions_lru, lru);

	return lru;
}


void server_world_tick(struct server_world* w, struct server_local* s) {
    dict_server_chunks_it_t it;
    dict_server_chunks_it(it, w->chunks);

    while (!dict_server_chunks_end_p(it)) {
        struct server_chunk* sc   = &dict_server_chunks_ref(it)->value;
        w_coord_t        baseX    = S_CHUNK_X(dict_server_chunks_ref(it)->key) * CHUNK_SIZE;
        w_coord_t        baseZ    = S_CHUNK_Z(dict_server_chunks_ref(it)->key) * CHUNK_SIZE;

        /* Lazy-Count: nach jeder Blockaenderung (tick_valid=false) einmal alle
         * Bloecke zaehlen die onWorldTick haben. Fast jeder Chunk in einer
         * normalen Welt hat 0 solcher Bloecke -> den kompletten 16x16x128-Scan
         * komplett ueberspringen und sofort zum naechsten Chunk. Auf der Wii
         * waren das ~4 Mio. Blockzugriffe/Tick -> 180 ms. */
        if(!sc->tick_valid) {
            uint16_t cnt = 0;
            for(int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT; i++) {
                uint8_t t = sc->ids[i];
                if(t && blocks[t] && blocks[t]->onWorldTick)
                    cnt++;
            }
            sc->tick_count = cnt;
            sc->tick_valid = true;
        }

        if(sc->tick_count == 0) {
            dict_server_chunks_next(it);
            continue;
        }

        for (int cx = 0; cx < CHUNK_SIZE; cx++) {
            for (int cz = 0; cz < CHUNK_SIZE; cz++) {
                for (int y = 0; y < WORLD_HEIGHT; y++) {
                    struct block_data blk;
                    if (!server_chunk_get_block(sc, cx, y, cz, &blk))
                        continue;

                    const struct block* b = blocks[blk.type];
                    if (!b || !b->onWorldTick)
                        continue;

                    bool needNeighbours =
                        (blk.type == BLOCK_REDSTONE_WIRE) ||
                        (blk.type == BLOCK_REDSTONE_TORCH) ||
						(blk.type == BLOCK_TNT) ||
						(blk.type == BLOCK_WOOD_PRESSURE_PLATE) ||
						(blk.type == BLOCK_STONE_PRESSURE_PLATE)||
						(blk.type == BLOCK_DOOR_WOOD)||
						(blk.type == BLOCK_DOOR_IRON) ||
						(blk.type == BLOCK_RAIL) ||
						(blk.type == BLOCK_POWERED_RAIL) ||
						(blk.type == BLOCK_DETECTOR_RAIL)
						;

                    struct block_data neighbour_data[SIDE_MAX];
                    struct block_data* neigh_ptr = NULL;
                    if (needNeighbours) {
						for (int side = 0; side < SIDE_MAX; ++side) {
							int ox, oy, oz;
							blocks_side_offset((enum side)side, &ox, &oy, &oz);

							w_coord_t nx = baseX + cx + ox;
							w_coord_t ny = y        + oy;
							w_coord_t nz = baseZ + cz + oz;

                            if (!server_world_get_block(AWORLD(s),
                                                        nx, ny, nz,
                                                        &neighbour_data[side]))
							{
                                neighbour_data[side].type        = BLOCK_AIR;
                                neighbour_data[side].metadata    = 0;
                                neighbour_data[side].sky_light   = 0;
                                neighbour_data[side].torch_light = 0;
							}
						}
                        neigh_ptr = neighbour_data;
                    }
                    struct block_info info = {
                        .block      = &blk,
                        .neighbours = neigh_ptr,
                        .x          = baseX + cx,
                        .y          = y,
                        .z          = baseZ + cz
                    };

                    b->onWorldTick(s, &info);

					float time = fmodf(daytime_get_time(), 24000.0f);
					if (b->onDay && time >= 0.0f && time < 13000.0f)
						b->onDay(s, &info);
					if (b->onNight && time >= 13000.0f && time < 24000.0f)
						b->onNight(s, &info);
                }
            }
        }

        dict_server_chunks_next(it);
    }
}

void server_world_random_tick(struct server_world* w, struct random_gen* g,
							  struct server_local* s, w_coord_t px,
							  w_coord_t pz, w_coord_t dist) {
	assert(w && g && s);

	dict_server_chunks_it_t it;
	dict_server_chunks_it(it, w->chunks);

	while(!dict_server_chunks_end_p(it)) {
		struct server_chunk* sc = &dict_server_chunks_ref(it)->value;
		int64_t id = dict_server_chunks_ref(it)->key;

		if(abs(S_CHUNK_X(id) - px) <= dist && abs(S_CHUNK_Z(id) - pz) <= dist) {
			// 80 random ticks each chunk
			for(int k = 0; k < 80; k++) {
				c_coord_t cx = rand_gen_range(g, 0, CHUNK_SIZE);
				c_coord_t cz = rand_gen_range(g, 0, CHUNK_SIZE);

				w_coord_t x = S_CHUNK_X(id) * CHUNK_SIZE + cx;
				w_coord_t y = rand_gen_range(g, 0, WORLD_HEIGHT);
				w_coord_t z = S_CHUNK_Z(id) * CHUNK_SIZE + cz;

				struct block_data blk;
				if(server_chunk_get_block(sc, cx, y, cz, &blk)
				   && blocks[blk.type] && blocks[blk.type]->onRandomTick) {
					blocks[blk.type]->onRandomTick(
						s,
						&(struct block_info) {.block = &blk,
											  .neighbours = NULL,
											  .x = x,
											  .y = y,
											  .z = z});
				}
			}
		}

		dict_server_chunks_next(it);
	}
}


void server_world_explode(struct server_local *s, vec3 center, float power) {
    struct broken_coord { int x,y,z; };
    struct broken_coord broken[512];
    int bc = 0;

    for (int i = 0; i < EXPLOSION_MAX_RAYS; i++) {
        vec3 dir;
        random_unit_vector(dir);

        vec3 pos = { center[0], center[1], center[2] };
        float rem = power;

        while (rem > 0.0f) {
            pos[0] += dir[0] * EXPLOSION_STEP;
            pos[1] += dir[1] * EXPLOSION_STEP;
            pos[2] += dir[2] * EXPLOSION_STEP;

            int bx = (int)floorf(pos[0]);
            int by = (int)floorf(pos[1]);
            int bz = (int)floorf(pos[2]);

            struct block_data blk;
            if (!server_world_get_block(AWORLD(s), bx, by, bz, &blk))
                break;

            if (blk.type == 0 || blk.type == BLOCK_BEDROCK) {
                rem -= EXPLOSION_STEP;
                continue;
            }

            float hardness = blocks[blk.type]->digging.hardness;
            if ((rem / power) > (rand()/(float)RAND_MAX)) {
                bool seen = false;
                for (int k = 0; k < bc; k++) {
                    if (broken[k].x == bx
                     && broken[k].y == by
                     && broken[k].z == bz) {
                        seen = true;
                        break;
                    }
                }
                if (!seen && bc < 512) {
                    broken[bc].x = bx;
                    broken[bc].y = by;
                    broken[bc].z = bz;
                    bc++;
                }
            }
            rem -= EXPLOSION_STEP + hardness * HARDNESS_SCALE;
        }
    }

    for (int i = 0; i < bc; i++) {
        int bx = broken[i].x, by = broken[i].y, bz = broken[i].z;
        struct block_data old;
        server_world_get_block(AWORLD(s), bx, by, bz, &old);
        server_world_set_block(s, bx, by, bz, (struct block_data){0});
        if (old.type != BLOCK_TNT
            && rand()/(float)RAND_MAX < 0.33f) {
            server_local_spawn_block_drops(
                s,
                &(struct block_info){ .x=bx,.y=by,.z=bz,.block=&old }
            );
        }
    }
}


bool server_world_find_empty_spot_nearby(const float pos[3], const struct server_world *world, float out_pos[3]){
	const float offs[][3] = {
        { 1.0f, 0.0f,  0.0f }, { -1.0f, 0.0f,  0.0f },
        { 0.0f, 0.0f,  1.0f }, {  0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f,  1.0f }, { -1.0f, 0.0f, -1.0f },
        { 0.0f, 1.0f,  0.0f }, // bovenop als laatste
    };
    int num_offsets = sizeof(offs) / sizeof(offs[0]);
    for (int i = 0; i < num_offsets; ++i) {
        float nx = pos[0] + offs[i][0];
        float ny = pos[1] + offs[i][1];
        float nz = pos[2] + offs[i][2];
        int tx = (int)floorf(nx);
        int ty = (int)floorf(ny);
        int tz = (int)floorf(nz);
        struct block_data bd;
        server_world_get_block(world, tx, ty, tz, &bd);
        if (!blocks[bd.type] || !blocks[bd.type]->getBoundingBox) {
            out_pos[0] = nx; out_pos[1] = ny; out_pos[2] = nz;
            return true;
        }
    }
    return false;
}
