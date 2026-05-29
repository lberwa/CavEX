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

#include "../lighting.h"
#include "../util.h"
#include "client_interface.h"
#include "server_local.h"
#include "server_world.h"
#include "../daytime.h"
#include "../cubiomes/generator.h"
#include "../cubiomes/biomes.h"

#define EXPLOSION_MAX_RAYS 300
#define EXPLOSION_STEP     0.5f
#define HARDNESS_SCALE     0.0005f

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define CHUNK_DIST2(x1, x2, z1, z2)                                            \
	(((x1) - (x2)) * ((x1) - (x2)) + ((z1) - (z2)) * ((z1) - (z2)))

#define S_CHUNK_IDX(x, y, z)                                                   \
	((y) + (W2C_COORD(z) + W2C_COORD(x) * CHUNK_SIZE) * WORLD_HEIGHT)

#define GEN_SEA_LEVEL 62

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
	int beach_band_low;
	int beach_band_high;
	float sea_floor_frequency_x;
	float sea_floor_frequency_z;
	float sea_floor_gravel_threshold;
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

static float gen_inv_nonzero(float value, float fallback) {
	return (fabsf(value) > 0.0001f) ? (1.0f / value) : fallback;
}

static struct gen_cuberite_runtime_args gen_runtime_args(const struct server_world* w) {
	const struct server_world_cuberite_config* cfg = &w->generator;
	struct gen_cuberite_runtime_args args = {
		.land_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_base_frequency_x, 1.0f / 40.0f),
		.land_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_base_frequency_z, 1.0f / 40.0f),
		.land_octaves = (cfg->biomal_noise3d_num_base_octaves > 0) ?
			cfg->biomal_noise3d_num_base_octaves : 6,
		.detail_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_frequency_x, 1.0f / 40.0f),
		.detail_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_frequency_z, 1.0f / 40.0f),
		.detail_octaves = (cfg->biomal_noise3d_num_density_octaves > 0) ?
			cfg->biomal_noise3d_num_density_octaves : 6,
		.mountain_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_x, 1.0f / 40.0f),
		.mountain_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_z, 1.0f / 40.0f),
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
		.beach_band_low = -2,
		.beach_band_high = 2,
		.sea_floor_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_frequency_x, 1.0f / 40.0f) * 2.3f,
		.sea_floor_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_frequency_z, 1.0f / 40.0f) * 2.3f,
		.sea_floor_gravel_threshold = 0.35f,
		.island_mask_frequency_x = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_x, 1.0f / 40.0f) * 0.72f,
		.island_mask_frequency_z = gen_inv_nonzero(cfg->biomal_noise3d_choice_frequency_z, 1.0f / 40.0f) * 0.72f,
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
		.tree_threshold_forest = 0.96f,
		.tree_threshold_dense = 0.975f,
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
		p.top_block = BLOCK_SAND;
		p.filler_block = BLOCK_SAND;
		p.oceanic = false;
		p.riverine = true;
		return p;
	}

	if(isOceanic(biome_id)) {
		p.base_height = 49.0f;
		p.amplitude = 4.0f;
		p.top_block = BLOCK_SAND;
		p.filler_block = BLOCK_SANDSTONE;
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
			p.top_block = BLOCK_STONE;
			p.filler_block = BLOCK_STONE;
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

static int gen_biome_at_safe(Generator* biome_gen, int wx, int wz) {
	int biome_id = getBiomeAt(biome_gen, 4, wx, 64, wz);
	return (biome_id < 0) ? plains : biome_id;
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
	int sand_votes = 0;
	int stone_votes = 0;
	int ocean_votes = 0;
	int snowy_votes = 0;
	int river_votes = 0;

	int first = gen_biome_at_safe(biome_gen, wx, wz);
	if(center_biome)
		*center_biome = first;

	for(int i = 0; i < 5; i++) {
		int biome_id = (i == 0) ? first :
								gen_biome_at_safe(biome_gen, wx + offs[i][0],
												  wz + offs[i][1]);
		struct gen_biome_profile p = gen_profile_for_biome(biome_id);
		float w = weights[i];
		base += p.base_height * w;
		amp += p.amplitude * w;
		wsum += w;
		if(p.top_block == BLOCK_SAND)
			sand_votes++;
		if(p.top_block == BLOCK_STONE)
			stone_votes++;
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
		.top_block = BLOCK_GRASS,
		.filler_block = BLOCK_DIRT,
		.oceanic = ocean_votes >= 3,
		.riverine = false,
	};

	if(stone_votes >= 3) {
		out.top_block = BLOCK_STONE;
		out.filler_block = BLOCK_STONE;
	} else if(sand_votes >= 2) {
		out.top_block = BLOCK_SAND;
		out.filler_block = BLOCK_SAND;
	}

	if(snowy_votes >= 3 && out.top_block == BLOCK_GRASS)
		out.base_height += 1.0f;

	if(river_votes >= 2) {
		out.base_height = 60.0f;
		out.amplitude = 2.5f;
		out.top_block = BLOCK_SAND;
		out.filler_block = BLOCK_SAND;
		out.oceanic = false;
		out.riverine = true;
	}

	return out;
}

static float gen_cave_threshold_for_biome(int biome_id) {
	if(isOceanic(biome_id) || biome_id == river || biome_id == frozen_river)
		return 0.70f;

	switch(biome_id) {
		case mountains:
		case wooded_mountains:
		case mountain_edge:
		case gravelly_mountains:
		case modified_gravelly_mountains: return 0.60f;
		case desert:
		case desert_hills:
		case desert_lakes: return 0.67f;
		default: return 0.64f;
	}
}

static bool gen_is_dry_sandy_biome(int biome_id) {
	return biome_id == desert || biome_id == desert_hills
		   || biome_id == badlands || biome_id == badlands_plateau
		   || biome_id == wooded_badlands_plateau || biome_id == eroded_badlands
		   || biome_id == modified_badlands_plateau
		   || biome_id == modified_wooded_badlands_plateau;
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
	GEN_CHUNK_COLUMNS_PER_STEP = 10,
	GEN_FEATURE_STEP_COUNT = 13,
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

static int gen_chunk_cave_border_count(uint32_t seed, int chunk_x, int chunk_z,
										 enum gen_chunk_cave_side side) {
	uint32_t h = gen_chunk_cave_border_hash(seed, chunk_x, chunk_z, side, 0);
	int roll = (int)(h % 100U);
	if(roll < 55)
		return 1;
	if(roll < 78)
		return 2;
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
	out->y = 18 + (int)(y_h % 34U);
	out->radius = GEN_CAVE_MIN_RADIUS + 2 + (int)(r_h % 3U);
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

static void gen_carve_chunk_linked_caves(struct server_chunk* sc, uint32_t seed,
											 int chunk_x, int chunk_z) {
	struct gen_chunk_cave_connector connectors[8];
	int connector_count = 0;
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int hub_x = base_x + 4 + (int)(gen_hash3i(chunk_x, 211, chunk_z, seed) % 8U);
	int hub_z = base_z + 4 + (int)(gen_hash3i(chunk_x, 307, chunk_z, seed) % 8U);
	int hub_y = 18 + (int)(gen_hash3i(chunk_x, 401, chunk_z, seed) % 36U);

	for(int side = GEN_CHUNK_CAVE_WEST; side <= GEN_CHUNK_CAVE_SOUTH; side++) {
		int count = gen_chunk_cave_border_count(seed, chunk_x, chunk_z, (enum gen_chunk_cave_side)side);
		for(int i = 0; i < count && connector_count < (int)(sizeof(connectors) / sizeof(connectors[0])); i++) {
			if(gen_chunk_cave_make_connector(&connectors[connector_count], seed, chunk_x, chunk_z,
											 (enum gen_chunk_cave_side)side, i)) {
				connector_count++;
			}
		}
	}

	if(connector_count == 0) {
		uint32_t h = gen_hash3i(chunk_x, 509, chunk_z, seed);
		if((h % 100U) >= 22U)
			return;
		hub_y = 18 + (int)(gen_hash3i(chunk_x, 557, chunk_z, seed) % 30U);
		struct gen_cave_tunnel room;
		int room_r = 6 + (int)(h % 4U);
		gen_cave_init_tunnel(&room, hub_x - room_r, hub_y, hub_z, room_r,
							 hub_x + room_r, hub_y, hub_z, room_r, seed ^ 0x4410U);
		gen_cave_process_chunk(&room, sc, chunk_x, chunk_z);
		return;
	}

	int avg_y = 0;
	for(int i = 0; i < connector_count; i++)
		avg_y += connectors[i].y;
	hub_y = gen_clamp_int((hub_y + avg_y / connector_count) / 2, 12, WORLD_HEIGHT - 12);

	int tunnel_budget = 8;
	for(int i = 0; i < connector_count && tunnel_budget > 0; i++) {
		struct gen_cave_tunnel tunnel;
		int end_x = hub_x + (int)(gen_hash3i(chunk_x, 601 + i, chunk_z, connectors[i].seed) % 7U) - 3;
		int end_z = hub_z + (int)(gen_hash3i(chunk_x, 677 + i, chunk_z, connectors[i].seed) % 7U) - 3;
		int end_y = gen_clamp_int(hub_y + (int)(gen_hash3i(chunk_x, 733 + i, chunk_z, connectors[i].seed) % 7U) - 3,
								  10, WORLD_HEIGHT - 10);
		int end_r = connectors[i].radius + 1 + (int)(connectors[i].seed % 2U);
		gen_cave_init_tunnel(&tunnel, connectors[i].x, connectors[i].y, connectors[i].z,
							 connectors[i].radius, end_x, end_y, end_z, end_r,
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

	if(tunnel_budget > 0) {
		struct gen_cave_tunnel room;
		int room_r = 6 + (int)(gen_hash3i(chunk_x, 809, chunk_z, seed) % 4U);
		gen_cave_init_tunnel(&room, hub_x - room_r, hub_y, hub_z, room_r,
							 hub_x + room_r, hub_y, hub_z, room_r,
							 seed ^ 0xA11CEU);
		gen_cave_process_chunk(&room, sc, chunk_x, chunk_z);
	}
}

static void gen_carve_chunk_cave_entrance(struct server_chunk* sc, uint32_t seed,
										  int chunk_x, int chunk_z,
										  const int* surface_map) {
	if((sc == NULL) || (surface_map == NULL))
		return;
	if((gen_hash3i(chunk_x, 941, chunk_z, seed) % 20U) != 0U)
		return;

	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
	int hub_x = base_x + 4 + (int)(gen_hash3i(chunk_x, 211, chunk_z, seed) % 8U);
	int hub_z = base_z + 4 + (int)(gen_hash3i(chunk_x, 307, chunk_z, seed) % 8U);
	int hub_y = 18 + (int)(gen_hash3i(chunk_x, 401, chunk_z, seed) % 36U);
	int entrance_lx = -1;
	int entrance_lz = -1;
	int best_score = -999999;
	for(int attempt = 0; attempt < 12; attempt++) {
		int lx = 2 + (int)(gen_hash3i(chunk_x, 953 + attempt * 17, chunk_z, seed) % (unsigned int)(CHUNK_SIZE - 4));
		int lz = 2 + (int)(gen_hash3i(chunk_x, 967 + attempt * 19, chunk_z, seed) % (unsigned int)(CHUNK_SIZE - 4));
		int surface = surface_map[lx + lz * CHUNK_SIZE];
		if(surface < 68)
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
		if(slope < 3)
			continue;
		int score = surface * 2 + slope * 6 - abs(lx - CHUNK_SIZE / 2) - abs(lz - CHUNK_SIZE / 2);
		if(score > best_score) {
			best_score = score;
			entrance_lx = lx;
			entrance_lz = lz;
		}
	}
	if(entrance_lx < 0 || entrance_lz < 0)
		return;
	int surface = surface_map[entrance_lx + entrance_lz * CHUNK_SIZE];
	if(surface < hub_y + 6)
		surface = hub_y + 6;
	if(surface > WORLD_HEIGHT - 4)
		surface = WORLD_HEIGHT - 4;

	int entrance_x = base_x + entrance_lx;
	int entrance_z = base_z + entrance_lz;
	int mouth_y = surface;
	int step1_x = entrance_x + (hub_x - entrance_x) / 4;
	int step1_z = entrance_z + (hub_z - entrance_z) / 4;
	int step1_y = gen_clamp_int(surface - 2 - (int)(gen_hash3i(chunk_x, 971, chunk_z, seed) % 2U),
								hub_y + 8, surface - 1);
	int step2_x = entrance_x + (hub_x - entrance_x) / 2;
	int step2_z = entrance_z + (hub_z - entrance_z) / 2;
	int step2_y = gen_clamp_int(step1_y - 2 - (int)(gen_hash3i(chunk_x, 983, chunk_z, seed) % 2U),
								hub_y + 4, step1_y - 1);
	int step3_x = entrance_x + (3 * (hub_x - entrance_x)) / 4;
	int step3_z = entrance_z + (3 * (hub_z - entrance_z)) / 4;
	int step3_y = gen_clamp_int(step2_y - 2,
								hub_y + 2, step2_y);

	struct gen_cave_tunnel mouth;
	gen_cave_init_tunnel(&mouth,
						 entrance_x, mouth_y, entrance_z, 3,
						 step1_x, step1_y, step1_z, 3,
						 seed ^ 0xE17A001U);
	gen_cave_process_chunk(&mouth, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel pocket1;
	gen_cave_init_tunnel(&pocket1,
						 step1_x - 2, step1_y, step1_z, 3,
						 step1_x + 2, step1_y, step1_z, 3,
						 seed ^ 0xE17A011U);
	gen_cave_process_chunk(&pocket1, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel link1;
	gen_cave_init_tunnel(&link1,
						 step1_x, step1_y, step1_z, 4,
						 step2_x, step2_y, step2_z, 4,
						 seed ^ 0xE17A021U);
	gen_cave_process_chunk(&link1, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel pocket2;
	gen_cave_init_tunnel(&pocket2,
						 step2_x - 2, step2_y, step2_z, 3,
						 step2_x + 2, step2_y, step2_z, 3,
						 seed ^ 0xE17A031U);
	gen_cave_process_chunk(&pocket2, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel link2;
	gen_cave_init_tunnel(&link2,
						 step2_x, step2_y, step2_z, 4,
						 step3_x, step3_y, step3_z, 5,
						 seed ^ 0xE17A041U);
	gen_cave_process_chunk(&link2, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel pocket3;
	gen_cave_init_tunnel(&pocket3,
						 step3_x - 3, step3_y, step3_z, 4,
						 step3_x + 3, step3_y, step3_z, 4,
						 seed ^ 0xE17A051U);
	gen_cave_process_chunk(&pocket3, sc, chunk_x, chunk_z);

	struct gen_cave_tunnel final_link;
	gen_cave_init_tunnel(&final_link,
						 step3_x, step3_y, step3_z, 5,
						 hub_x, hub_y, hub_z, 6,
						 seed ^ 0xE17A061U);
	gen_cave_process_chunk(&final_link, sc, chunk_x, chunk_z);

	for(int y = surface; y <= mouth_y + 1 && y < WORLD_HEIGHT; y++) {
		for(int dz = -1; dz <= 1; dz++) {
			for(int dx = -1; dx <= 1; dx++) {
				int lx = entrance_lx + dx;
				int lz = entrance_lz + dz;
				if(lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE)
					continue;
				if((dx * dx + dz * dz) > 2)
					continue;
				uint8_t b = gen_get_block(sc, lx, y, lz);
				if(b != BLOCK_BEDROCK)
					gen_set_block(sc, lx, y, lz, BLOCK_AIR);
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
		int sq_rad = p->radius * p->radius;
		for(int z = 0; z < CHUNK_SIZE; z++) {
			for(int x = 0; x < CHUNK_SIZE; x++) {
				for(int y = bottom; y <= top; y++) {
					int sq_dist = (dif_x - x) * (dif_x - x)
						+ (dif_y - y) * (dif_y - y)
						+ (dif_z - z) * (dif_z - z);
					if(sq_dist <= sq_rad) {
						uint8_t b = gen_get_block(sc, x, y, z);
						if(b == BLOCK_STONE || b == BLOCK_DIRT || b == BLOCK_GRASS
						   || b == BLOCK_SAND || b == BLOCK_SANDSTONE
						   || b == BLOCK_GRAVEL) {
							gen_set_block(sc, x, y, z, BLOCK_AIR);
						}
					} else if(sq_dist <= sq_rad * 2) {
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
	return ((h % 100U) < 3U) ? 1 : 0;
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
	if(min_width < 3)
		min_width = 3;
	if(max_width < min_width)
		max_width = min_width;

	out->seed = gen_chunk_ravine_border_hash(seed, chunk_x, chunk_z, side, 131 + index * 23);
	out->width = min_width + (int)(width_h % (unsigned int)(max_width - min_width + 1));
	out->top = 58 + (int)(top_h % 18U);
	out->bottom = 10 + (int)(bottom_h % 18U);
	if(out->top < out->bottom + 12)
		out->top = out->bottom + 12;
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

		float width = (float)width1 + ((float)(width2 - width1) * t) + 0.9f;
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
					float level_width = width * (0.75f + 0.45f * rel);
					if(level_width < 1.0f)
						level_width = 1.0f;
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
	(void)args;
	if(sc == NULL || cfg == NULL)
		return;
	int chunk_x = gen_floor_div(world_x0, CHUNK_SIZE);
	int chunk_z = gen_floor_div(world_z0, CHUNK_SIZE);
	int base_x = chunk_x * CHUNK_SIZE;
	int base_z = chunk_z * CHUNK_SIZE;
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

	int hub_x = base_x + 3 + (int)(gen_hash3i(chunk_x, 1501, chunk_z, seed) % 10U);
	int hub_z = base_z + 3 + (int)(gen_hash3i(chunk_x, 1519, chunk_z, seed) % 10U);
	int hub_top = 60 + (int)(gen_hash3i(chunk_x, 1531, chunk_z, seed) % 14U);
	int hub_bottom = 12 + (int)(gen_hash3i(chunk_x, 1543, chunk_z, seed) % 14U);
	int hub_width = 4 + (int)(gen_hash3i(chunk_x, 1559, chunk_z, seed) % 3U);
	if(hub_top < hub_bottom + 14)
		hub_top = hub_bottom + 14;

	for(int i = 0; i < connector_count; i++) {
		gen_carve_ravine_segment(sc, chunk_x, chunk_z,
								 (float)connectors[i].x, (float)connectors[i].z,
								 (float)hub_x, (float)hub_z,
								 connectors[i].width, hub_width,
								 connectors[i].bottom, hub_bottom,
								 connectors[i].top, hub_top,
								 connectors[i].seed);
	}

	if(connector_count >= 2) {
		for(int i = 0; i + 1 < connector_count; i += 2) {
			gen_carve_ravine_segment(sc, chunk_x, chunk_z,
									 (float)connectors[i].x, (float)connectors[i].z,
									 (float)connectors[i + 1].x, (float)connectors[i + 1].z,
									 connectors[i].width, connectors[i + 1].width,
									 connectors[i].bottom, connectors[i + 1].bottom,
									 connectors[i].top, connectors[i + 1].top,
									 seed ^ (uint32_t)(1601 + i * 37));
		}
	}
}

static void gen_set_nibble(uint8_t* data, size_t idx, uint8_t value) {
	nibble_write(data, idx, value & 0xF);
}

static void gen_set_ore_if_stone(struct server_chunk* sc, int x, int y, int z,
								 uint8_t ore, uint32_t seed, float threshold) {
	size_t idx = S_CHUNK_IDX(x, y, z);
	if(sc->ids[idx] != BLOCK_STONE)
		return;
	float n = gen_rand01_from_hash(gen_hash3i(x, y, z, seed));
	if(n > threshold)
		sc->ids[idx] = ore;
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

static bool gen_tree_space_clear(const struct server_chunk* sc, int x, int y, int z, int radius, int height) {
	for(int yy = y; yy <= y + height; yy++) {
		for(int dz = -radius; dz <= radius; dz++) {
			for(int dx = -radius; dx <= radius; dx++) {
				uint8_t b = gen_get_block(sc, x + dx, yy, z + dz);
				if(b != BLOCK_AIR && b != BLOCK_LEAVES)
					return false;
			}
		}
	}
	return true;
}

static void gen_place_small_apple_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int rnd = gen_noise_int3d(seed ^ 0xACCE5511U, x, y, z) >> 3;
	static const int heights[] = {1, 2, 2, 3};
	int trunk_h = 1 + heights[rnd & 3];
	if(!gen_tree_space_clear(sc, x, y, z, 3, trunk_h + 5))
		return;
	for(int i = 0; i < trunk_h; i++) gen_place_log(sc, x, y + i, z, 0);
	int top = y + trunk_h;
	for(int layer = 0; layer < 2; layer++) {
		gen_place_leaf_pattern(sc, x, top + layer, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 0);
		gen_place_log(sc, x, top + layer, z, 0);
	}
	gen_place_leaf_pattern(sc, x, top + 2, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 0);
	gen_place_log(sc, x, top + 2, z, 0);
	gen_place_leaf_pattern(sc, x, top + 3, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 0);
	gen_set_block_with_meta(sc, x, top + 3, z, BLOCK_LEAVES, 0);
}

static void gen_place_birch_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int height = 5 + (gen_noise_int3d(seed ^ 0xB17C0021U, x, y, z) % 3);
	if(!gen_tree_space_clear(sc, x, y, z, 3, height + 3))
		return;
	for(int i = 0; i < height; i++) gen_place_log(sc, x, y + i, z, 2);
	int h = y + height;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 2);
	gen_set_block_with_meta(sc, x, h, z, BLOCK_LEAVES, 2);
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 2);
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 2);
	h--;
	gen_place_leaf_pattern(sc, x, h, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 2);
}

static void gen_place_small_spruce_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int my_random = gen_noise_int3d(seed ^ 0x5A71CE11U, x, y, z) / 8;
	static const int clear_heights[] = {1, 2, 2, 3};
	int clear_h = clear_heights[my_random & 3];
	if(!gen_tree_space_clear(sc, x, y, z, 3, clear_h + 8))
		return;
	for(int i = 0; i < clear_h; i++) gen_place_log(sc, x, y + i, z, 1);
	int top = y + clear_h;
	if((my_random & 1) == 0) {
		gen_place_leaf_pattern(sc, x, top, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 1);
		gen_place_log(sc, x, top, z, 1);
		top++;
	}
	gen_place_leaf_pattern(sc, x, top, z, GEN_BIG_O2, (int)(sizeof(GEN_BIG_O2) / sizeof(GEN_BIG_O2[0])), 1);
	gen_place_log(sc, x, top, z, 1);
	top++;
	gen_place_leaf_pattern(sc, x, top, z, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 1);
	gen_set_block_with_meta(sc, x, top, z, BLOCK_LEAVES, 1);
	gen_set_block_with_meta(sc, x, top + 1, z, BLOCK_LEAVES, 1);
}

static void gen_place_acacia_tree(struct server_chunk* sc, int x, int y, int z, uint32_t seed) {
	int height = 2 + ((gen_noise_int3d(seed ^ 0xACAC1A11U, x, y, z) / 11) % 3);
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
	gen_place_leaf_pattern(sc, bx, by, bz, GEN_BIG_O3, (int)(sizeof(GEN_BIG_O3) / sizeof(GEN_BIG_O3[0])), 0);
	gen_place_leaf_pattern(sc, bx, by + 1, bz, GEN_BIG_O1, (int)(sizeof(GEN_BIG_O1) / sizeof(GEN_BIG_O1[0])), 0);
	gen_set_block_with_meta(sc, bx, by + 1, bz, BLOCK_LEAVES, 0);
}

static void gen_try_place_tree(struct server_chunk* sc, int lx, int lz, int y,
							   uint32_t seed, int world_x, int world_z,
							   int biome_id) {
	uint8_t ground = gen_get_block(sc, lx, y - 1, lz);
	if(ground != BLOCK_GRASS && ground != BLOCK_DIRT)
		return;
	if(biome_id == birch_forest || biome_id == birch_forest_hills) {
		gen_place_birch_tree(sc, lx, y, lz, seed ^ 0xB17C0001U);
	} else if(biome_id == taiga || biome_id == taiga_hills
			  || biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills
			  || biome_id == snowy_taiga || biome_id == snowy_taiga_hills) {
		gen_place_small_spruce_tree(sc, lx, y, lz, seed ^ 0x5A71CE01U);
	} else if(biome_id == savanna || biome_id == savanna_plateau) {
		gen_place_acacia_tree(sc, lx, y, lz, seed ^ 0xACAC1A01U);
	} else {
		gen_place_small_apple_tree(sc, lx, y, lz, seed ^ 0xA991E001U);
	}
	(void)world_x;
	(void)world_z;
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
				gen_set_block(sc, x, cy + 1, cz - 2, BLOCK_TORCH);
				gen_set_block(sc, x, cy + 1, cz + 2, BLOCK_TORCH);
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
				gen_set_block(sc, cx - 2, cy + 1, z, BLOCK_TORCH);
				gen_set_block(sc, cx + 2, cy + 1, z, BLOCK_TORCH);
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
	gen_set_block(sc, cx, y + 1, cz - 1, BLOCK_TORCH);
	gen_set_block(sc, cx - 2, y + 2, cz, glass);
	gen_set_block(sc, cx + 2, y + 2, cz, glass);
	gen_set_block(sc, cx, y + 2, cz + 2, glass);
	gen_set_block(sc, cx, y + 1, cz, BLOCK_TORCH);
	gen_set_block(sc, cx - 1, y + 1, cz - 1, BLOCK_WORKBENCH);
	gen_set_block(sc, cx + 1, y + 1, cz - 1, BLOCK_CHEST);
}

static void gen_place_village(struct server_chunk* sc, uint32_t seed,
							  int world_x0, int world_z0, int sea_level,
							  Generator* biome_gen) {
	if((gen_hash3i(world_x0, 1601, world_z0, seed ^ 0x7111A63EU) % 100U) >= 20U)
		return;

	int cx = 8;
	int cz = 8;
	int wx = world_x0 + cx;
	int wz = world_z0 + cz;
	int biome_id = gen_biome_at_safe(biome_gen, wx, wz);
	if(!(biome_id == plains || biome_id == desert || biome_id == desert_hills
		 || biome_id == savanna || biome_id == savanna_plateau
		 || biome_id == taiga || biome_id == taiga_hills))
		return;

	int y = 0;
	if(!gen_can_surface_structure(sc, cx, cz, 6, &y))
		return;
	if(y <= sea_level)
		return;

	uint8_t path = (biome_id == desert || biome_id == desert_hills) ? BLOCK_SANDSTONE : BLOCK_GRAVEL;
	uint8_t wall = (biome_id == desert || biome_id == desert_hills) ? BLOCK_SANDSTONE : BLOCK_PLANKS;
	uint8_t roof = (biome_id == desert || biome_id == desert_hills) ? BLOCK_SLAB : BLOCK_WOODEN_STAIRS;
	uint8_t floor = (biome_id == desert || biome_id == desert_hills) ? BLOCK_SANDSTONE : BLOCK_PLANKS;

	gen_flatten_surface_patch(sc, cx, cz, 6, y, path, path);
	for(int x = cx - 5; x <= cx + 5; x++)
		for(int z = cz - 1; z <= cz + 1; z++)
			gen_set_block(sc, x, y, z, path);
	for(int z = cz - 5; z <= cz + 5; z++)
		for(int x = cx - 1; x <= cx + 1; x++)
			gen_set_block(sc, x, y, z, path);

	if(biome_id == desert || biome_id == desert_hills) {
		gen_place_desert_well(sc, cx, y, cz);
	} else {
		gen_fill_box(sc, cx - 1, y, cz - 1, cx + 1, y, cz + 1, BLOCK_COBBLESTONE);
		for(int dx = -1; dx <= 1; dx += 2)
			for(int dz = -1; dz <= 1; dz += 2)
				gen_fill_box(sc, cx + dx, y + 1, cz + dz, cx + dx, y + 3, cz + dz, BLOCK_FENCE);
		gen_fill_box(sc, cx - 2, y + 4, cz - 2, cx + 2, y + 4, cz + 2, BLOCK_PLANKS);
		gen_set_block(sc, cx, y + 1, cz, BLOCK_WATER_STILL);
	}

	gen_place_small_house(sc, cx - 4, y, cz - 4, wall, roof, floor, BLOCK_GLASS_PANE);
	gen_place_small_house(sc, cx + 4, y, cz - 4, wall, roof, floor, BLOCK_GLASS_PANE);
	gen_place_small_house(sc, cx - 4, y, cz + 4, wall, roof, floor, BLOCK_GLASS_PANE);

	if((gen_hash3i(world_x0, 1613, world_z0, seed ^ 0x7111A63FU) & 1U) == 0U)
		gen_place_small_house(sc, cx + 4, y, cz + 4, wall, roof, floor, BLOCK_GLASS_PANE);
}

static void gen_place_ice_surfaces(struct server_chunk* sc, uint32_t seed,
								   int world_x0, int world_z0, int sea_level,
								   Generator* biome_gen) {
	(void)seed;
	for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
		for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
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

	int cx = 4 + (int)(gen_hash3i(world_x0, 41, world_z0, seed ^ 0x6A4E1001U) % 8U);
	int cz = 4 + (int)(gen_hash3i(world_x0, 43, world_z0, seed ^ 0x6A4E1002U) % 8U);
	int surface = gen_chunk_actual_surface_height(sc, cx, cz);
	if(surface <= sea_level + 2)
		return;

	int max_center_y = surface - 6;
	if(max_center_y > sea_level - 4)
		max_center_y = sea_level - 4;
	if(max_center_y < 10)
		return;

	int cy = 10 + (int)(gen_hash3i(world_x0, 47, world_z0, seed ^ 0x6A4E1003U)
		% (uint32_t)(max_center_y - 9));
	int rx = 3 + (int)(gen_hash3i(world_x0, 53, world_z0, seed ^ 0x6A4E1004U) % 2U);
	int rz = 3 + (int)(gen_hash3i(world_x0, 59, world_z0, seed ^ 0x6A4E1005U) % 2U);
	int ry = 2 + (int)(gen_hash3i(world_x0, 61, world_z0, seed ^ 0x6A4E1006U) % 2U);

	for(int x = cx - rx - 1; x <= cx + rx + 1; x++) {
		for(int z = cz - rz - 1; z <= cz + rz + 1; z++) {
			if(!gen_inside_chunk(x, cy, z))
				return;
			int top = gen_chunk_actual_surface_height(sc, x, z);
			if(top < cy + ry + 2)
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
					gen_set_block(sc, x, y, z, BLOCK_WATER_STILL);
				} else {
					gen_set_block(sc, x, y, z, BLOCK_AIR);
				}
			}
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

static void gen_try_spread_fluid_from(struct server_chunk* sc, int x, int y, int z,
									  uint8_t source, uint8_t flowing) {
	if(!gen_inside_chunk(x, y, z))
		return;
	if(gen_get_block(sc, x, y, z) != source && gen_get_block(sc, x, y, z) != flowing)
		return;

	if(gen_inside_chunk(x, y - 1, z) && gen_get_block(sc, x, y - 1, z) == BLOCK_AIR) {
		gen_set_block(sc, x, y - 1, z, flowing);
		return;
	}

	static const int dx[4] = {1, -1, 0, 0};
	static const int dz[4] = {0, 0, 1, -1};
	for(int i = 0; i < 4; i++) {
		int nx = x + dx[i];
		int nz = z + dz[i];
		if(!gen_inside_chunk(nx, y, nz))
			continue;
		if(gen_get_block(sc, nx, y, nz) == BLOCK_AIR
		   && gen_inside_chunk(nx, y - 1, nz)
		   && gen_is_opaque(gen_get_block(sc, nx, y - 1, nz))) {
			gen_set_block(sc, nx, y, nz, flowing);
		}
	}
}

static void gen_presimulate_fluids_local(struct server_chunk* sc,
										 bool enable_water, bool enable_lava) {
	if(sc == NULL || (!enable_water && !enable_lava))
		return;

	for(int pass = 0; pass < 6; pass++) {
		for(int y = 1; y < WORLD_HEIGHT - 1; y++) {
			for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
				for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
					uint8_t block = gen_get_block(sc, lx, y, lz);
					if(enable_water && (block == BLOCK_WATER_STILL || block == BLOCK_WATER_FLOW))
						gen_try_spread_fluid_from(sc, lx, y, lz, BLOCK_WATER_STILL, BLOCK_WATER_FLOW);
					if(enable_lava && (block == BLOCK_LAVA_STILL || block == BLOCK_LAVA_FLOW))
						gen_try_spread_fluid_from(sc, lx, y, lz, BLOCK_LAVA_STILL, BLOCK_LAVA_FLOW);
				}
			}
		}
	}
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

static void gen_recompute_height_and_skylight(struct server_chunk* sc) {
	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			uint8_t column_height = 0;
			for(int y = WORLD_HEIGHT - 1; y >= 0; y--) {
				uint8_t block = sc->ids[S_CHUNK_IDX(lx, y, lz)];
				if(block != BLOCK_AIR && block != BLOCK_WATER_FLOW
				   && block != BLOCK_WATER_STILL) {
					column_height = (uint8_t)(y + 1);
					break;
				}
			}
			sc->heightmap[lx + lz * CHUNK_SIZE] = column_height;

			uint8_t light = 15;
			for(int y = WORLD_HEIGHT - 1; y >= 0; y--) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				gen_set_nibble(sc->lighting_sky, idx, light);
				if(light > 0 && gen_is_opaque(sc->ids[idx]))
					light = 0;
			}
		}
	}
}

static void gen_surface_artifact_cleanup(struct server_chunk* sc) {
	for(int pass = 0; pass < 2; pass++) {
	for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
		for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
			int y = -1;
			for(int yy = WORLD_HEIGHT - 2; yy >= 2; yy--) {
				uint8_t b = sc->ids[S_CHUNK_IDX(lx, yy, lz)];
				if(b != BLOCK_AIR && b != BLOCK_WATER_STILL && b != BLOCK_WATER_FLOW) {
					y = yy;
					break;
				}
			}
			if(y < 3)
				continue;

			uint8_t top = sc->ids[S_CHUNK_IDX(lx, y, lz)];
			if(top != BLOCK_GRASS && top != BLOCK_DIRT && top != BLOCK_SAND
			   && top != BLOCK_STONE && top != BLOCK_SNOW)
				continue;

			int nsolid = 0;
			int nlow = 0;
			const int nx[4] = {1, -1, 0, 0};
			const int nz[4] = {0, 0, 1, -1};
			for(int i = 0; i < 4; i++) {
				int sx = lx + nx[i], sz = lz + nz[i];
				int sy = -1;
				for(int yy = y + 2; yy >= y - 6 && yy >= 1; yy--) {
					uint8_t b = sc->ids[S_CHUNK_IDX(sx, yy, sz)];
					if(b != BLOCK_AIR && b != BLOCK_WATER_STILL && b != BLOCK_WATER_FLOW) {
						sy = yy;
						break;
					}
				}
				if(sy >= 0)
					nsolid++;
				if(sy >= 0 && sy <= y - 2)
					nlow++;
			}

			// Fill narrow trenches/pits near surface.
			if(nlow >= 2 || nsolid <= 1) {
				int fill_y = y - 1;
				if(sc->ids[S_CHUNK_IDX(lx, fill_y, lz)] == BLOCK_AIR) {
					sc->ids[S_CHUNK_IDX(lx, fill_y, lz)]
						= (top == BLOCK_SAND) ? BLOCK_SAND :
						  (top == BLOCK_STONE) ? BLOCK_STONE :
											 BLOCK_DIRT;
				}
			}

			// Close open surface holes: if an air cell has many solid neighbors, fill it.
			for(int yy = y - 1; yy >= y - 8 && yy >= 2; yy--) {
				size_t idx = S_CHUNK_IDX(lx, yy, lz);
				if(sc->ids[idx] != BLOCK_AIR)
					continue;
				int s4 = 0;
				if(gen_is_opaque(sc->ids[S_CHUNK_IDX(lx + 1, yy, lz)]))
					s4++;
				if(gen_is_opaque(sc->ids[S_CHUNK_IDX(lx - 1, yy, lz)]))
					s4++;
				if(gen_is_opaque(sc->ids[S_CHUNK_IDX(lx, yy, lz + 1)]))
					s4++;
				if(gen_is_opaque(sc->ids[S_CHUNK_IDX(lx, yy, lz - 1)]))
					s4++;
				if(s4 >= 3) {
					sc->ids[idx] = (top == BLOCK_SAND) ? BLOCK_SAND :
								   (top == BLOCK_STONE) ? BLOCK_STONE :
														BLOCK_DIRT;
				}
			}
		}
	}
	}
}

static bool gen_is_ore_block(uint8_t block) {
	return (block == BLOCK_COAL_ORE)
		|| (block == BLOCK_IRON_ORE)
		|| (block == BLOCK_GOLD_ORE)
		|| (block == BLOCK_DIAMOND_ORE);
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
				bool top_open = gen_is_air(sc, lx, y + 1, lz) || gen_is_water(sc, lx, y + 1, lz);
				if(open_neighbors >= 5 || (unsupported && top_open && open_neighbors >= 4))
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

static void gen_cleanup_coastal_spires_and_pits(struct server_chunk* sc, int sea_level) {
	if(sc == NULL || sc->heightmap == NULL)
		return;
	for(int pass = 0; pass < 1; pass++) {
		for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
			for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
				int h = gen_chunk_actual_surface_height(sc, lx, lz);
				if(h < 2)
					continue;
				uint8_t top = gen_get_block(sc, lx, h, lz);
				if(top != BLOCK_SAND && top != BLOCK_GRAVEL && top != BLOCK_SANDSTONE)
					continue;
				if(h > sea_level + 3)
					continue;

				int neigh_sum = 0;
				int neigh_min = h;
				int neigh_max = h;
				int lower_neighbors = 0;
				int higher_neighbors = 0;
				int water_neighbors = 0;
				const int nx[4] = {1, -1, 0, 0};
				const int nz[4] = {0, 0, 1, -1};
				for(int i = 0; i < 4; i++) {
					int nh = gen_chunk_actual_surface_height(sc, lx + nx[i], lz + nz[i]);
					neigh_sum += nh;
					if(nh < neigh_min)
						neigh_min = nh;
					if(nh > neigh_max)
						neigh_max = nh;
					if(nh <= h - 2)
						lower_neighbors++;
					if(nh >= h + 2)
						higher_neighbors++;
					if(gen_get_block(sc, lx + nx[i], nh + 1, lz + nz[i]) == BLOCK_WATER_STILL
					   || gen_get_block(sc, lx + nx[i], nh + 1, lz + nz[i]) == BLOCK_WATER_FLOW)
						water_neighbors++;
				}
				int neigh_avg = neigh_sum / 4;

				if(lower_neighbors >= 3 && h >= neigh_avg + 3) {
					int target = neigh_avg + 1;
					for(int y = h; y > target; y--) {
						uint8_t here = gen_get_block(sc, lx, y, lz);
						if(here == BLOCK_SAND || here == BLOCK_GRAVEL || here == BLOCK_SANDSTONE)
							gen_set_block(sc, lx, y, lz, (y <= sea_level) ? BLOCK_WATER_STILL : BLOCK_AIR);
					}
					continue;
				}

				if((higher_neighbors >= 3 || water_neighbors >= 2) && h <= neigh_avg - 3) {
					int fill_to = neigh_avg - 1;
					if(fill_to > sea_level)
						fill_to = sea_level;
					for(int y = h + 1; y <= fill_to; y++) {
						uint8_t below = gen_get_block(sc, lx, y - 1, lz);
						uint8_t fill = (below == BLOCK_SANDSTONE || below == BLOCK_STONE) ? BLOCK_SANDSTONE : BLOCK_SAND;
						uint8_t here = gen_get_block(sc, lx, y, lz);
						if(here == BLOCK_AIR || here == BLOCK_WATER_STILL || here == BLOCK_WATER_FLOW)
							gen_set_block(sc, lx, y, lz, fill);
					}
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

static void gen_cleanup_sandy_water_channels(struct server_chunk* sc, int sea_level) {
	if(sc == NULL)
		return;
	for(int pass = 0; pass < 1; pass++) {
		for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
			for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
				int h = gen_chunk_actual_surface_height(sc, lx, lz);
				if(h < 1 || h > sea_level)
					continue;
				uint8_t top = gen_get_block(sc, lx, h, lz);
				if(top != BLOCK_SAND && top != BLOCK_GRAVEL && top != BLOCK_SANDSTONE)
					continue;

				int neigh[4];
				int higher = 0;
				int sandy = 0;
				int avg = 0;
				const int nx[4] = {1, -1, 0, 0};
				const int nz[4] = {0, 0, 1, -1};
				for(int i = 0; i < 4; i++) {
					neigh[i] = gen_chunk_actual_surface_height(sc, lx + nx[i], lz + nz[i]);
					avg += neigh[i];
					uint8_t ntop = gen_get_block(sc, lx + nx[i], neigh[i], lz + nz[i]);
					if(neigh[i] >= h + 3)
						higher++;
					if(ntop == BLOCK_SAND || ntop == BLOCK_GRAVEL || ntop == BLOCK_SANDSTONE)
						sandy++;
				}
				avg /= 4;
				if(higher < 2 || sandy < 3 || avg <= h + 1)
					continue;

				int fill_to = avg - 1;
				if(fill_to > sea_level + 1)
					fill_to = sea_level + 1;
				for(int y = h + 1; y <= fill_to; y++) {
					uint8_t here = gen_get_block(sc, lx, y, lz);
					if(here != BLOCK_AIR && here != BLOCK_WATER_STILL && here != BLOCK_WATER_FLOW)
						continue;
					uint8_t below = gen_get_block(sc, lx, y - 1, lz);
					uint8_t fill = (below == BLOCK_SANDSTONE || below == BLOCK_STONE) ? BLOCK_SANDSTONE : BLOCK_SAND;
					gen_set_block(sc, lx, y, lz, fill);
				}
			}
		}
	}
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

static bool gen_alloc_chunk_buffers(struct server_chunk* sc) {
	size_t total = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
	*sc = (struct server_chunk) {
		.ids = calloc(total, 1),
		.metadata = calloc(total / 2, 1),
		.lighting_sky = calloc(total / 2, 1),
		.lighting_torch = calloc(total / 2, 1),
		.heightmap = calloc(CHUNK_SIZE * CHUNK_SIZE, 1),
		.modified = true,
	};

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

static void gen_generate_terrain_columns(struct server_world* w,
										 struct server_chunk* sc, int* surface_map,
										 Generator* biome_gen,
										 const struct gen_cuberite_runtime_args* gen_args,
										 uint32_t seed, int choice_octaves,
										 int32_t world_x0, int32_t world_z0,
										 w_coord_t chunk_x, w_coord_t chunk_z,
										 int start_col, int num_cols) {
	int end_col = start_col + num_cols;
	if(end_col > CHUNK_SIZE * CHUNK_SIZE)
		end_col = CHUNK_SIZE * CHUNK_SIZE;

	for(int col = start_col; col < end_col; col++) {
		int lx = col / CHUNK_SIZE;
		int lz = col % CHUNK_SIZE;
		int32_t wx = world_x0 + lx;
		int32_t wz = world_z0 + lz;
		int biome_id = plains;
		struct gen_biome_profile profile = gen_blended_profile(biome_gen, wx, wz, &biome_id);

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
		float micro_factor = 2.0f;
		if(profile.top_block == BLOCK_SAND) {
			mountain_factor = 0.35f;
			micro_factor = 0.8f;
		} else if(profile.top_block == BLOCK_STONE && !profile.oceanic) {
			mountain_factor = 0.95f;
			micro_factor = 1.6f;
		}

		int surface = (int)(profile.base_height
			+ continental * profile.amplitude
			+ mountain_lift * (profile.amplitude * mountain_factor)
			+ micro * micro_factor);

		if(profile.riverine) {
			int river_floor = gen_args->sea_level - 2 + (int)(micro * 1.2f);
			if(river_floor < gen_args->sea_level - 3)
				river_floor = gen_args->sea_level - 3;
			if(river_floor > gen_args->sea_level - 1)
				river_floor = gen_args->sea_level - 1;
			surface = (surface + river_floor * 2) / 3;
			if(surface < gen_args->sea_level - 4)
				surface = gen_args->sea_level - 4;
			if(surface > gen_args->sea_level)
				surface = gen_args->sea_level;
		}

		if(profile.oceanic) {
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
				int target = (int)(gen_args->sea_level - 1 + lift * gen_args->island_max_lift);
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
		if(profile.oceanic && surface > gen_args->sea_level - 2)
			surface = gen_args->sea_level - 2;
		if(surface < gen_args->sea_level - 20)
			surface = gen_args->sea_level - 20;

		if(gen_is_dry_sandy_biome(biome_id) && !profile.oceanic
		   && biome_id != beach && biome_id != river) {
			if(surface < gen_args->sea_level + 1)
				surface = gen_args->sea_level + 1;
			if(surface <= gen_args->sea_level + 4)
				surface = gen_args->sea_level + 2;
		}

		float neigh_h = 0.0f;
		float neigh_w = 0.0f;
		int sample_h = 0;
		if(lx <= 3 && gen_sample_neighbor_edge_height(w, chunk_x, chunk_z, 0, lz, &sample_h)) {
			float k = (4.0f - (float)lx) / 4.0f;
			neigh_h += (float)sample_h * k;
			neigh_w += k;
		}
		if(lx >= CHUNK_SIZE - 4 && gen_sample_neighbor_edge_height(w, chunk_x, chunk_z, 1, lz, &sample_h)) {
			float k = (4.0f - (float)((CHUNK_SIZE - 1) - lx)) / 4.0f;
			neigh_h += (float)sample_h * k;
			neigh_w += k;
		}
		if(lz <= 3 && gen_sample_neighbor_edge_height(w, chunk_x, chunk_z, 2, lx, &sample_h)) {
			float k = (4.0f - (float)lz) / 4.0f;
			neigh_h += (float)sample_h * k;
			neigh_w += k;
		}
		if(lz >= CHUNK_SIZE - 4 && gen_sample_neighbor_edge_height(w, chunk_x, chunk_z, 3, lx, &sample_h)) {
			float k = (4.0f - (float)((CHUNK_SIZE - 1) - lz)) / 4.0f;
			neigh_h += (float)sample_h * k;
			neigh_w += k;
		}
		if(neigh_w > 0.0f) {
			surface = (int)((float)surface * gen_args->edge_blend_weight_self
				+ (neigh_h / neigh_w) * gen_args->edge_blend_weight_neighbor);
		}

		int dirt_depth = 4;
		if(profile.top_block == BLOCK_SAND)
			dirt_depth = 5;
		if(profile.top_block == BLOCK_STONE)
			dirt_depth = 2;
		bool submerged = surface <= gen_args->sea_level;
		uint8_t top_block = profile.top_block;
		uint8_t filler_block = profile.filler_block;
		if(surface >= gen_args->sea_level + gen_args->beach_band_low
		   && surface <= gen_args->sea_level + gen_args->beach_band_high
		   && !profile.oceanic && profile.top_block != BLOCK_STONE) {
			top_block = BLOCK_SAND;
			filler_block = BLOCK_SAND;
			dirt_depth = 4;
		}
		if(submerged) {
			float sea_floor = gen_fbm2d(wx * gen_args->sea_floor_frequency_x,
				wz * gen_args->sea_floor_frequency_z, seed ^ 0x51A17E2BU, 2, 2.0f, 0.5f);
			bool gravel_floor = (profile.top_block == BLOCK_STONE)
				|| (sea_floor > gen_args->sea_floor_gravel_threshold);
			if(gravel_floor) {
				top_block = BLOCK_GRAVEL;
				filler_block = BLOCK_STONE;
			} else {
				top_block = BLOCK_SAND;
				filler_block = BLOCK_SAND;
			}
		}

		surface_map[lx + lz * CHUNK_SIZE] = surface;
		for(int y = 0; y < WORLD_HEIGHT; y++) {
			size_t idx = S_CHUNK_IDX(lx, y, lz);
			uint8_t block = BLOCK_AIR;
			if(y == 0) {
				block = BLOCK_BEDROCK;
			} else if(y <= surface) {
				if(y == surface) {
					block = top_block;
				} else if(y >= surface - dirt_depth) {
					block = filler_block;
				} else {
					block = BLOCK_STONE;
				}
			} else if(y <= gen_args->sea_level) {
				block = BLOCK_WATER_STILL;
			}
			sc->ids[idx] = block;
			gen_set_nibble(sc->metadata, idx, 0);
			gen_set_nibble(sc->lighting_torch, idx, 0);
		}

		if(w->generator.finisher_snow && surface > gen_args->sea_level + 1 && isSnowy(biome_id)) {
			size_t top_idx = S_CHUNK_IDX(lx, surface + 1, lz);
			if(sc->ids[top_idx] == BLOCK_AIR)
				sc->ids[top_idx] = BLOCK_SNOW;
		}

		for(int y = 5; y < surface - 3; y++) {
			gen_set_ore_if_stone(sc, lx, y, lz, BLOCK_COAL_ORE, seed ^ 0x1111U, 0.985f);
			if(y < 72)
				gen_set_ore_if_stone(sc, lx, y, lz, BLOCK_IRON_ORE, seed ^ 0x2222U, 0.992f);
			if(y < 32)
				gen_set_ore_if_stone(sc, lx, y, lz, BLOCK_GOLD_ORE, seed ^ 0x3333U, 0.9975f);
			if(y < 18)
				gen_set_ore_if_stone(sc, lx, y, lz, BLOCK_DIAMOND_ORE, seed ^ 0x4444U, 0.9986f);
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
		if(w->generator.finisher_worm_nest_caves) {
			gen_carve_worm_nest_caves(sc, seed, world_x0, world_z0, &w->generator);
			gen_carve_chunk_cave_entrance(sc, seed, chunk_x, chunk_z, surface_map);
		}
		return;
	case 1:
		if(w->generator.finisher_rough_ravines)
			gen_carve_ravine_pass(sc, seed, world_x0, world_z0, gen_args,
								  &w->generator);
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
		if(w->generator.finisher_mineshafts)
			gen_place_mineshaft(sc, seed, world_x0, world_z0);
		return;
	case 6:
		if(w->generator.finisher_pre_simulator
		   && (w->generator.pre_simulator_water || w->generator.pre_simulator_lava)) {
			gen_presimulate_fluids_local(sc, w->generator.pre_simulator_water,
										 w->generator.pre_simulator_lava);
		}
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
		gen_cleanup_floating_cave_blocks(sc);
		gen_cleanup_coastal_spires_and_pits(sc, gen_args->sea_level);
		gen_cleanup_sandy_water_channels(sc, gen_args->sea_level);
		gen_cleanup_floating_ores(sc);
		return;
	case 9:
		if(w->generator.finisher_single_piece_structures)
			gen_place_single_piece_structure(sc, seed, world_x0, world_z0, biome_gen);
		return;
	case 10:
		if(w->generator.finisher_villages)
			gen_place_village(sc, seed, world_x0, world_z0, gen_args->sea_level,
							  biome_gen);
		return;
	case 11:
		if(w->generator.finisher_ice)
			gen_place_ice_surfaces(sc, seed, world_x0, world_z0,
								   gen_args->sea_level, biome_gen);
		return;
	case 12:
		if(w->generator.finisher_overworld_clump_flowers)
			gen_place_flower_clumps(sc, seed, world_x0, world_z0, biome_gen);
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

static void gen_generate_deco_columns(struct server_world* w, struct server_chunk* sc,
									  Generator* biome_gen,
									  const struct gen_cuberite_runtime_args* gen_args,
									  uint32_t seed, int32_t world_x0, int32_t world_z0,
									  int start_col, int num_cols) {
	int end_col = start_col + num_cols;
	if(end_col > (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2))
		end_col = (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2);
	for(int col = start_col; col < end_col; col++) {
		int lx = 1 + (col / (CHUNK_SIZE - 2));
		int lz = 1 + (col % (CHUNK_SIZE - 2));
		int y = -1;
		for(int yy = WORLD_HEIGHT - 2; yy >= 1; yy--) {
			uint8_t b = sc->ids[S_CHUNK_IDX(lx, yy, lz)];
			if(b != BLOCK_AIR && b != BLOCK_WATER_STILL && b != BLOCK_WATER_FLOW) {
				y = yy;
				break;
			}
		}
		if(y < 1)
			continue;
		int wx = world_x0 + lx;
		int wz = world_z0 + lz;
		int biome_id = gen_biome_at_safe(biome_gen, wx, wz);
		float deco = gen_rand01_from_hash(gen_hash3i(wx, y, wz, seed ^ 0xABCD1234U));
		uint8_t top = sc->ids[S_CHUNK_IDX(lx, y, lz)];
		if(w->generator.finisher_trees
		   && (biome_id == forest || biome_id == birch_forest
		   || biome_id == flower_forest || biome_id == taiga
		   || biome_id == taiga_hills || biome_id == giant_tree_taiga
		   || biome_id == giant_tree_taiga_hills || biome_id == swamp
		   || biome_id == jungle || biome_id == jungle_hills)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > ((biome_id == forest || biome_id == birch_forest)
			? gen_args->tree_threshold_forest : gen_args->tree_threshold_dense)) {
			gen_try_place_tree(sc, lx, lz, y + 1, seed, wx, wz, biome_id);
			continue;
		}
		if(w->generator.finisher_tall_grass
		   && (biome_id == plains || biome_id == forest || biome_id == flower_forest)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR) {
			if(deco > gen_args->grass_threshold)
				gen_set_block_with_meta(sc, lx, y + 1, lz, BLOCK_TALL_GRASS, 1);
			else if(deco > gen_args->flower_threshold)
				sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_FLOWER;
		}
		if(w->generator.finisher_tall_grass
		   && (biome_id == taiga || biome_id == taiga_hills || biome_id == giant_tree_taiga
		   || biome_id == giant_tree_taiga_hills || biome_id == snowy_taiga
		   || biome_id == snowy_taiga_hills)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->flower_threshold) {
			gen_set_block_with_meta(sc, lx, y + 1, lz, BLOCK_TALL_GRASS, 2);
		}
		if((biome_id == desert || biome_id == desert_hills)
		   && top == BLOCK_SAND
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->cactus_threshold
		   && y + 2 < WORLD_HEIGHT) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_CACTUS;
			if(deco > gen_args->cactus_tall_threshold)
				sc->ids[S_CHUNK_IDX(lx, y + 2, lz)] = BLOCK_CACTUS;
		}
		if((biome_id == desert || biome_id == desert_hills || biome_id == badlands
		   || biome_id == badlands_plateau || biome_id == wooded_badlands_plateau)
		   && top == BLOCK_SAND
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->dead_bush_threshold) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = 32;
		}
		if((biome_id == forest || biome_id == roofedForest || biome_id == swamp)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->lily_threshold) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = (deco > gen_args->reed_tall_threshold)
				? BLOCK_RED_MUSHROOM : BLOCK_BROWM_MUSHROOM;
		}
		if(w->generator.finisher_lilypads
		   && (biome_id == swamp || biome_id == river)
		   && y >= gen_args->sea_level - 1 && y <= gen_args->sea_level + 1
		   && sc->ids[S_CHUNK_IDX(lx, y, lz)] == BLOCK_WATER_STILL
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->lily_threshold) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_WATERLILY;
		}
		if((biome_id == swamp || biome_id == river || biome_id == beach)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && (gen_is_water(sc, lx + 1, y, lz) || gen_is_water(sc, lx - 1, y, lz)
			   || gen_is_water(sc, lx, y, lz + 1) || gen_is_water(sc, lx, y, lz - 1))
		   && deco > gen_args->reed_threshold && y + 2 < WORLD_HEIGHT) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_REED;
			if(deco > gen_args->reed_tall_threshold && gen_is_air(sc, lx, y + 2, lz))
				sc->ids[S_CHUNK_IDX(lx, y + 2, lz)] = BLOCK_REED;
		}
		if((biome_id == plains || biome_id == forest) && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->pumpkin_threshold) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_PUMPKIN;
		}
		if((biome_id == jungle || biome_id == jungle_hills) && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > gen_args->melon_threshold) {
			sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_MELON;
		}
		if((biome_id == jungle || biome_id == jungle_hills || biome_id == swamp)
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && y > gen_args->sea_level - 2 && deco > gen_args->vine_threshold) {
			bool near_leaf_or_log
				= (gen_get_block(sc, lx + 1, y + 1, lz) == BLOCK_LEAVES
				   || gen_get_block(sc, lx - 1, y + 1, lz) == BLOCK_LEAVES
				   || gen_get_block(sc, lx, y + 1, lz + 1) == BLOCK_LEAVES
				   || gen_get_block(sc, lx, y + 1, lz - 1) == BLOCK_LEAVES
				   || gen_get_block(sc, lx + 1, y + 1, lz) == BLOCK_LOG
				   || gen_get_block(sc, lx - 1, y + 1, lz) == BLOCK_LOG
				   || gen_get_block(sc, lx, y + 1, lz + 1) == BLOCK_LOG
				   || gen_get_block(sc, lx, y + 1, lz - 1) == BLOCK_LOG);
			if(near_leaf_or_log)
				sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_VINE;
		}
	}
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
		gen_surface_artifact_cleanup(sc);
		return false;
	case 2:
		gen_recompute_height_and_skylight(sc);
		dict_server_chunks_set_at(w->chunks, S_CHUNK_ID(x, z), *sc);
		*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
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

static bool server_world_generate_chunk(struct server_world* w, w_coord_t x,
										w_coord_t z, struct server_chunk** out) {
	assert(w && out);
	struct gen_cuberite_runtime_args gen_args = gen_runtime_args(w);
	int choice_octaves = (w->generator.biomal_noise3d_num_choice_octaves > 0)
		? w->generator.biomal_noise3d_num_choice_octaves : 4;

	if(server_world_is_chunk_loaded(w, x, z)) {
		*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
		return *out != NULL;
	}
	struct server_chunk sc;
	if(!gen_alloc_chunk_buffers(&sc))
		return false;

	uint32_t seed = (uint32_t)w->world_seed;
	Generator biome_gen;
	setupGenerator(&biome_gen, MC_1_7, 0);
	applySeed(&biome_gen, DIM_OVERWORLD, (uint64_t)w->world_seed);
	int32_t world_x0 = x * CHUNK_SIZE;
	int32_t world_z0 = z * CHUNK_SIZE;
	int surface_map[CHUNK_SIZE * CHUNK_SIZE];
	gen_generate_terrain_columns(w, &sc, surface_map, &biome_gen, &gen_args, seed,
								 choice_octaves, world_x0, world_z0, x, z, 0,
								 CHUNK_SIZE * CHUNK_SIZE);
	gen_apply_feature_pass(w, &sc, surface_map, &biome_gen, &gen_args, seed,
						   world_x0, world_z0, x, z);
	gen_generate_deco_columns(w, &sc, &biome_gen, &gen_args, seed, world_x0,
							  world_z0, 0,
							  (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2));
	return gen_finalize_chunk(w, &sc, &biome_gen, &gen_args, seed, world_x0,
							  world_z0, x, z, out);
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

static bool server_world_advance_pending(struct server_world* w,
										 struct server_chunk** out) {
	assert(w && out);
	if(!w->pending_chunk.active)
		return false;

	struct gen_cuberite_runtime_args gen_args = gen_runtime_args(w);
	int choice_octaves = (w->generator.biomal_noise3d_num_choice_octaves > 0)
		? w->generator.biomal_noise3d_num_choice_octaves : 4;
	uint32_t seed = (uint32_t)w->world_seed;
	Generator biome_gen;
	setupGenerator(&biome_gen, MC_1_7, 0);
	applySeed(&biome_gen, DIM_OVERWORLD, (uint64_t)w->world_seed);
	int32_t world_x0 = w->pending_chunk.x * CHUNK_SIZE;
	int32_t world_z0 = w->pending_chunk.z * CHUNK_SIZE;

	switch(w->pending_chunk.phase) {
	case SERVER_WORLD_PENDING_TERRAIN:
		gen_generate_terrain_columns(w, &w->pending_chunk.chunk,
									 w->pending_chunk.surface_map, &biome_gen,
									 &gen_args, seed, choice_octaves, world_x0,
									 world_z0, w->pending_chunk.x,
									 w->pending_chunk.z,
									 w->pending_chunk.next_column,
									 GEN_CHUNK_COLUMNS_PER_STEP);
		w->pending_chunk.next_column += GEN_CHUNK_COLUMNS_PER_STEP;
		if(w->pending_chunk.next_column >= CHUNK_SIZE * CHUNK_SIZE) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FEATURES;
			w->pending_chunk.next_column = 0;
		}
		return false;

	case SERVER_WORLD_PENDING_FEATURES:
		gen_apply_feature_step(w, &w->pending_chunk.chunk,
							   w->pending_chunk.surface_map, &biome_gen,
							   &gen_args, seed, world_x0, world_z0,
							   w->pending_chunk.x, w->pending_chunk.z,
							   w->pending_chunk.next_column);
		w->pending_chunk.next_column++;
		if(w->pending_chunk.next_column >= GEN_FEATURE_STEP_COUNT) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_DECO;
			w->pending_chunk.next_column = 0;
		}
		return false;

	case SERVER_WORLD_PENDING_DECO:
		gen_generate_deco_columns(w, &w->pending_chunk.chunk, &biome_gen,
								  &gen_args, seed, world_x0, world_z0,
								  w->pending_chunk.next_column,
								  GEN_CHUNK_COLUMNS_PER_STEP);
		w->pending_chunk.next_column += GEN_CHUNK_COLUMNS_PER_STEP;
		if(w->pending_chunk.next_column >= (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2)) {
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FINALIZE;
			w->pending_chunk.next_column = 0;
		}
		return false;

	case SERVER_WORLD_PENDING_FINALIZE: {
		bool ok = gen_finalize_chunk_step(
			w, &w->pending_chunk.chunk, &biome_gen, &gen_args, seed, world_x0, world_z0,
			w->pending_chunk.x, w->pending_chunk.z, out,
			w->pending_chunk.next_column);
		w->pending_chunk.next_column++;
		if(ok || w->pending_chunk.next_column >= GEN_FINALIZE_STEP_COUNT) {
			memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
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

	free(sc->ids);
	free(sc->metadata);
	free(sc->lighting_sky);
	free(sc->lighting_torch);
	free(sc->heightmap);
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
		.finisher_rough_ravines = true,
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
		.finisher_worm_nest_caves = true,
		.worm_nest_caves_size = 40,
		.worm_nest_caves_grid = 128,
		.worm_nest_max_offset = 16,
		.finisher_water_lakes = true,
		.water_lakes_probability = 25,
		.finisher_water_springs = true,
		.finisher_lava_lakes = true,
		.lava_lakes_probability = 10,
		.finisher_lava_springs = true,
		.finisher_ore_nests = true,
		.finisher_mineshafts = true,
		.mineshafts_grid_size = 512,
		.mineshafts_max_offset = 256,
		.mineshafts_max_system_size = 160,
		.mineshafts_chance_corridor = 600,
		.mineshafts_chance_crossing = 200,
		.mineshafts_chance_staircase = 30,
		.finisher_trees = true,
		.finisher_villages = true,
		.finisher_single_piece_structures = true,
		.finisher_tall_grass = true,
		.finisher_sprinkle_foliage = true,
		.finisher_ice = true,
		.finisher_snow = true,
		.finisher_lilypads = true,
		.finisher_bottom_lava = true,
		.bottom_lava_level = 10,
		.finisher_dead_bushes = true,
		.finisher_natural_patches = true,
		.finisher_pre_simulator = true,
		.pre_simulator_falling_blocks = true,
		.pre_simulator_water = true,
		.pre_simulator_lava = true,
		.finisher_animals = true,
		.finisher_overworld_clump_flowers = true,
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
	struct server_world* w = user;
	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));
	assert(sc);

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

bool server_world_set_block(struct server_local* s, w_coord_t x, w_coord_t y, w_coord_t z, struct block_data blk) {
    struct server_world* w = &s->world;
	assert(w);
	if(y < 0 || y >= WORLD_HEIGHT)
		return false;

	struct server_chunk* sc = dict_server_chunks_get(
		w->chunks, S_CHUNK_ID(WCOORD_CHUNK_OFFSET(x), WCOORD_CHUNK_OFFSET(z)));

	if(sc) {
		size_t idx = S_CHUNK_IDX(x, y, z);
		sc->modified = true;
		sc->ids[idx] = blk.type;
		nibble_write(sc->metadata, idx, blk.metadata);

		if(w->dimension != WORLD_DIM_NETHER)
			lighting_heightmap_update(sc->heightmap, W2C_COORD(x), y,
									  W2C_COORD(z), blk.type,
									  server_chunk_get_block, sc);

		lighting_update_at_block(
			(struct world_modification_entry) {
				.x = x,
				.y = y,
				.z = z,
				.blk = blk,
			},
			w->dimension == WORLD_DIM_NETHER, server_world_light_get_block,
			server_world_light_set_light, w);

		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_SET_BLOCK,
			.payload.set_block.x = x,
			.payload.set_block.y = y,
			.payload.set_block.z = z,
			.payload.set_block.block = blk,
		});
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

	struct server_chunk tmp = (struct server_chunk) {.modified = false};
	if(!region_archive_get_blocks(ra, x, z, &tmp))
		return false;

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

	if(c->modified) {
		// load region archive into cache
		struct region_archive tmp;
		struct region_archive* ra = server_world_chunk_region(w, x, z);

		if(!ra) {
			if(!region_archive_create_new(&tmp, w->level_name,
										  CHUNK_REGION_COORD(x),
										  CHUNK_REGION_COORD(z), w->dimension))
				return;
			ra = &tmp;
		}

		bool saved = region_archive_set_blocks(ra, x, z, c);
		if(!saved) {
#ifdef CHUNK_DEBUG
			fprintf(stderr, "server_world_save_chunk_obj: failed to save chunk %d,%d — keeping in memory\n", (int)x, (int)z);
#endif
		} else {
			c->modified = false;
		}
	}

	if(erase) {
		/* Only erase the in-memory chunk if the last save succeeded (not modified),
		 * otherwise keep it so we can retry saving later — prevents data loss when
		 * region writes fail. */
		if(c->modified) {
#ifdef CHUNK_DEBUG
			fprintf(stderr, "server_world_save_chunk_obj: skip erase of chunk %d,%d because save failed\n", (int)x, (int)z);
#endif
			return;
		}

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

        for (int cx = 0; cx < CHUNK_SIZE; cx++) {
            for (int cz = 0; cz < CHUNK_SIZE; cz++) {
                for (int y = 0; y < WORLD_HEIGHT; y++) {
                    struct block_data blk;
                    if (!server_chunk_get_block(sc, cx, y, cz, &blk))
                        continue;

                    const struct block* b = blocks[blk.type];
                    if (!b || !b->onWorldTick)
                        continue;


                    // determine if we need any neigbour info, only is needed for these types
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

                            if (!server_world_get_block(&s->world,
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
            if (!server_world_get_block(&s->world, bx, by, bz, &blk))
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
        server_world_get_block(&s->world, bx, by, bz, &old);
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
