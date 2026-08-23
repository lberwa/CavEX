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

#include "../block/blocks.h"
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

// #define ALL_FALSE_FINISHER

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
static bool gen_is_dry_sandy_biome(int biome_id);
static bool gen_is_water(const struct server_chunk* sc, int x, int y, int z);

/* Terrain debug switches: turn pieces of the base generator on/off individually. */
static const bool GEN_FORCE_LEGACY_WII_SURFACE = 				true;
static const bool GEN_ENABLE_LEGACY_RARE_OCEANS = 				true;
#ifdef ALL_FALSE_FINISHER
static const bool GEN_ENABLE_RIVER_LOWERING = 					false;
static const bool GEN_ENABLE_NEIGHBOR_EDGE_BLEND = 				false;
static const bool GEN_ENABLE_OCEAN_ISLAND_MASK = 				false;
static const bool GEN_ENABLE_DRY_SANDY_SEA_CLAMP = 				false;
static const bool GEN_ENABLE_LOWLAND_NEIGHBOR_SMOOTHING = 		false;
static const bool GEN_ENABLE_LEGACY_LOWLAND_FLOOR = 			false;
static const bool GEN_ENABLE_BEACH_SURFACE_REPLACEMENT = 		false;
static const bool GEN_ENABLE_SUBMERGED_SEAFLOOR_REPLACEMENT = 	false;
static const bool GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL = 			false;
static const bool GEN_ENABLE_SNOW_SURFACE_LAYER = 				false;
#else
static const bool GEN_ENABLE_RIVER_LOWERING = 					true;
static const bool GEN_ENABLE_NEIGHBOR_EDGE_BLEND = 				true;
static const bool GEN_ENABLE_OCEAN_ISLAND_MASK = 				true;
static const bool GEN_ENABLE_DRY_SANDY_SEA_CLAMP = 				true;
static const bool GEN_ENABLE_LOWLAND_NEIGHBOR_SMOOTHING = 		true;
static const bool GEN_ENABLE_LEGACY_LOWLAND_FLOOR = 			true;
static const bool GEN_ENABLE_BEACH_SURFACE_REPLACEMENT = 		true;
static const bool GEN_ENABLE_SUBMERGED_SEAFLOOR_REPLACEMENT = 	true;
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
	int sand_votes = 0;
	int stone_votes = 0;
	int ocean_votes = 0;
	int snowy_votes = 0;
	int river_votes = 0;

	int first = gen_apply_forest_biome_variation(
		gen_reduce_extreme_biomes(gen_biome_at_safe(biome_gen, wx, wz), wx, wz), wx, wz);
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
	float micro_factor = 2.0f;
	if(profile->top_block == BLOCK_SAND) {
		mountain_factor = 0.35f;
		micro_factor = 0.8f;
	} else if(profile->top_block == BLOCK_STONE && !profile->oceanic) {
		mountain_factor = 0.95f;
		micro_factor = 1.6f;
	}

	int surface = (int)(profile->base_height
		+ continental * profile->amplitude
		+ mountain_lift * (profile->amplitude * mountain_factor)
		+ micro * micro_factor);

	if(GEN_ENABLE_RIVER_LOWERING && profile->riverine) {
		int river_floor = gen_args->sea_level - 1 + (int)(micro * 0.75f);
		if(river_floor < gen_args->sea_level - 1)
			river_floor = gen_args->sea_level - 1;
		if(river_floor > gen_args->sea_level)
			river_floor = gen_args->sea_level;
		surface = (surface + river_floor * 3) / 4;
		if(surface < gen_args->sea_level - 2)
			surface = gen_args->sea_level - 2;
		if(surface > gen_args->sea_level + 1)
			surface = gen_args->sea_level + 1;
	}

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

	if(GEN_ENABLE_DRY_SANDY_SEA_CLAMP
	   && gen_is_dry_sandy_biome(biome_id) && !profile->oceanic
	   && biome_id != beach && biome_id != river) {
		if(surface < gen_args->sea_level + 1)
			surface = gen_args->sea_level + 1;
		if(surface <= gen_args->sea_level + 4)
			surface = gen_args->sea_level + 2;
	}

	return surface;
}

static int gen_compute_legacy_wii_surface(
	const struct gen_cuberite_runtime_args* gen_args, uint32_t seed,
	int32_t wx, int32_t wz) {
	float h1 = gen_fbm2d(wx * gen_args->land_frequency_x,
		wz * gen_args->land_frequency_z, seed ^ 0x11AABBCCU, 2, 2.0f, 0.5f);
	float h2 = gen_fbm2d(wx * gen_args->detail_frequency_x,
		wz * gen_args->detail_frequency_z, seed ^ 0x77CC22AAU, 1, 2.0f, 0.5f);
	float m = gen_fbm2d(wx * gen_args->mountain_frequency_x,
		wz * gen_args->mountain_frequency_z, seed ^ 0x55DD33AAU, 2, 2.0f, 0.5f);
	int surface = (int)(63.0f + h1 * 11.0f + h2 * 2.0f + fmaxf(0.0f, m) * 8.0f);
	if(surface < 6)
		surface = 6;
	if(surface > WORLD_HEIGHT - 2)
		surface = WORLD_HEIGHT - 2;
	return surface;
}

static int gen_apply_legacy_rare_oceans(
	const struct gen_cuberite_runtime_args* gen_args, uint32_t seed,
	int32_t wx, int32_t wz, int surface, int biome_id) {
	if(!GEN_ENABLE_LEGACY_RARE_OCEANS)
		return surface;
	if(biome_id == river || biome_id == frozen_river)
		return surface;

	float ocean_macro = gen_fbm2d(wx * 0.0032f, wz * 0.0032f,
		seed ^ 0x2F6E2B1DU, 3, 2.0f, 0.5f);
	if(ocean_macro > -0.42f)
		return surface;

	float t = (-0.42f - ocean_macro) / 0.28f;
	if(t < 0.0f)
		t = 0.0f;
	if(t > 1.0f)
		t = 1.0f;
	t = t * t * (3.0f - 2.0f * t);

	int ocean_surface = gen_args->sea_level - 2;
	int blended = (int)((1.0f - t) * (float)surface + t * (float)ocean_surface);
	if(blended > surface)
		blended = surface;
	if(blended < gen_args->sea_level - 4)
		blended = gen_args->sea_level - 4;
	return blended;
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
		int len = 5 + (int)(gen_hash3i(chunk_x, 1501 + seg * 41, chunk_z, seed) % 5U);
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
		   && (int)(gen_hash3i(chunk_x, 1589 + seg * 47, chunk_z, seed) % 100U) < 72) {
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
		if((int)(gen_hash3i(chunk_x, 1097 + i * 31, chunk_z, seed) % 100U) >= 35U)
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
		int branch_count = 2 + (int)(gen_hash3i(chunk_x, 1163 + i * 41, chunk_z, seed) % 2U);
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
	unsigned int entrance_chance = mountainous_chunk ? 80U : 20U;
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
	int required_slope = mountainous_chunk ? 2 : 1;
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
		int surface = gen_compute_surface_height_base(
			&profile, biome_id, gen_args, seed, choice_octaves, wx, wz);
		int legacy_surface = gen_compute_legacy_wii_surface(gen_args, seed, wx, wz);

		if(GEN_FORCE_LEGACY_WII_SURFACE) {
			surface = legacy_surface;
			surface = gen_apply_legacy_rare_oceans(
				gen_args, seed, wx, wz, surface, biome_id);
		}

		if(!GEN_FORCE_LEGACY_WII_SURFACE
		   && GEN_ENABLE_LOWLAND_NEIGHBOR_SMOOTHING && !profile.oceanic) {
			int avg = surface;
			int count = 1;
			static const int smooth_offs[4][2] = {
				{1, 0}, {-1, 0}, {0, 1}, {0, -1},
			};
			for(int i = 0; i < 4; i++) {
				int nbiome = plains;
				struct gen_biome_profile np = gen_blended_profile(
					biome_gen, wx + smooth_offs[i][0], wz + smooth_offs[i][1], &nbiome);
				if(np.oceanic)
					continue;
				avg += gen_compute_surface_height_base(
					&np, nbiome, gen_args, seed, choice_octaves,
					wx + smooth_offs[i][0], wz + smooth_offs[i][1]);
				count++;
			}
			avg /= count;

			bool near_water_level = surface <= gen_args->sea_level + 3;
			bool sandy_or_river = profile.riverine
				|| profile.top_block == BLOCK_SAND
				|| biome_id == beach
				|| biome_id == river
				|| biome_id == frozen_river;
			if(surface <= avg - 4 && (near_water_level || sandy_or_river)) {
				surface = avg - 2;
			}
			if(surface <= avg - 6) {
				surface = avg - 3;
			}
			if(sandy_or_river && surface < gen_args->sea_level - 1) {
				surface = gen_args->sea_level - 1;
			}

			bool lowland = surface <= gen_args->sea_level + 6;
			bool non_mountain = profile.top_block != BLOCK_STONE;
			if(GEN_ENABLE_LEGACY_LOWLAND_FLOOR
			   && (sandy_or_river || lowland) && non_mountain) {
				int min_legacy = legacy_surface - 2;
				if(surface < min_legacy)
					surface = min_legacy;
				if(surface < legacy_surface && lowland) {
					surface = (surface * 2 + legacy_surface) / 3;
				}
			}
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
		if(GEN_ENABLE_NEIGHBOR_EDGE_BLEND && neigh_w > 0.0f) {
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
		if(GEN_ENABLE_BEACH_SURFACE_REPLACEMENT
		   && surface >= gen_args->sea_level + gen_args->beach_band_low
		   && surface <= gen_args->sea_level + gen_args->beach_band_high
		   && !profile.oceanic && profile.top_block != BLOCK_STONE) {
			top_block = BLOCK_SAND;
			filler_block = BLOCK_SAND;
			dirt_depth = 4;
		}
		if(GEN_ENABLE_SUBMERGED_SEAFLOOR_REPLACEMENT && submerged) {
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
			} else if(GEN_ENABLE_WATER_FILL_TO_SEA_LEVEL
					  && y <= gen_args->sea_level) {
				block = BLOCK_WATER_STILL;
			}
			sc->ids[idx] = block;
			gen_set_nibble(sc->metadata, idx, 0);
			gen_set_nibble(sc->lighting_torch, idx, 0);
		}

		if(GEN_ENABLE_SNOW_SURFACE_LAYER && w->generator.finisher_snow
		   && surface > gen_args->sea_level + 1 && isSnowy(biome_id)
		   && surface < WORLD_HEIGHT - 1) { /* surface+1 darf nicht WORLD_HEIGHT sein */
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
			gen_carve_chunk_cave_entrance(sc, seed, chunk_x, chunk_z, surface_map,
										  biome_gen, gen_args);
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
		int biome_id = gen_apply_forest_biome_variation(
			gen_reduce_extreme_biomes(gen_biome_at_safe(biome_gen, wx, wz), wx, wz), wx, wz);
		float deco = gen_rand01_from_hash(gen_hash3i(wx, y, wz, seed ^ 0xABCD1234U));
		uint8_t top = sc->ids[S_CHUNK_IDX(lx, y, lz)];
		float tree_threshold = gen_args->tree_threshold_dense;
		if(biome_id == forest || biome_id == birch_forest
		   || biome_id == birch_forest_hills || biome_id == wooded_hills
		   || biome_id == flower_forest) {
			tree_threshold = gen_args->tree_threshold_forest;
		}
		if(biome_id == dark_forest || biome_id == jungle || biome_id == jungle_hills
		   || biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills) {
			tree_threshold = gen_args->tree_threshold_dense - 0.10f;
			if(tree_threshold < 0.45f)
				tree_threshold = 0.45f;
		}

		if(w->generator.finisher_trees
		   && (biome_id == forest || biome_id == birch_forest
		   || biome_id == flower_forest || biome_id == dark_forest
		   || biome_id == birch_forest_hills || biome_id == wooded_hills
		   || biome_id == taiga
		   || biome_id == taiga_hills || biome_id == giant_tree_taiga
		   || biome_id == giant_tree_taiga_hills || biome_id == swamp
		   || biome_id == jungle || biome_id == jungle_hills)
		   && top == BLOCK_GRASS
		   && sc->ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
		   && deco > tree_threshold) {
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
		gen_propagate_lighting(sc, w->dimension == WORLD_DIM_NETHER);
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

static Generator* server_world_biome_gen(struct server_world* w) {
	if(!g_biome_gen_ready || g_biome_gen_seed != w->world_seed) {
		setupGenerator(&g_biome_gen, MC_1_7, 0);
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
	gen_generate_terrain_columns(w, &sc, surface_map, biome_gen, &gen_args, seed,
								 choice_octaves, world_x0, world_z0, x, z, 0,
								 CHUNK_SIZE * CHUNK_SIZE);
	gen_apply_feature_pass(w, &sc, surface_map, biome_gen, &gen_args, seed,
						   world_x0, world_z0, x, z);
	gen_generate_deco_columns(w, &sc, biome_gen, &gen_args, seed, world_x0,
							  world_z0, 0,
							  (CHUNK_SIZE - 2) * (CHUNK_SIZE - 2));
	return gen_finalize_chunk(w, &sc, biome_gen, &gen_args, seed, world_x0,
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
		case SERVER_WORLD_PENDING_TERRAIN:
			gen_generate_nether_chunk(w, &w->pending_chunk.chunk,
			                          w->pending_chunk.x, w->pending_chunk.z);
			w->pending_chunk.phase = SERVER_WORLD_PENDING_FINALIZE;
			w->pending_chunk.next_column = 0;
			return false;
		case SERVER_WORLD_PENDING_FINALIZE: {
			int32_t wx0 = w->pending_chunk.x * CHUNK_SIZE;
			int32_t wz0 = w->pending_chunk.z * CHUNK_SIZE;
			bool ok = gen_finalize_chunk_step(w, &w->pending_chunk.chunk,
			                                  NULL, NULL, 0, wx0, wz0,
			                                  w->pending_chunk.x,
			                                  w->pending_chunk.z, out, 2);
			if(ok) memset(&w->pending_chunk, 0, sizeof(w->pending_chunk));
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

	switch(w->pending_chunk.phase) {
	case SERVER_WORLD_PENDING_TERRAIN:
		gen_generate_terrain_columns(w, &w->pending_chunk.chunk,
									 w->pending_chunk.surface_map, biome_gen,
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
							   w->pending_chunk.surface_map, biome_gen,
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
		gen_generate_deco_columns(w, &w->pending_chunk.chunk, biome_gen,
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
			w, &w->pending_chunk.chunk, biome_gen, &gen_args, seed, world_x0, world_z0,
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
		sc->modified = true;
		sc->needs_save = true; /* echte Spieleraenderung -> muss auf Disk */
		sc->ids[idx] = blk.type;
		nibble_write(sc->metadata, idx, blk.metadata);
		sc->tick_valid = false; /* tick_count neu zaehlen beim naechsten Tick */

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
			.payload.set_block.dimension = w->dimension,
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

	/* Nur echte Spieleraenderungen brauchen einen Disk-Save. Rein generierte
	 * oder von Disk geladene (unveraenderte) Chunks regenerieren identisch aus
	 * dem Seed -> beim Entladen einfach verwerfen, KEIN malloc/Save noetig.
	 * Das bricht den OOM-Deadlock: Speichern braucht Speicher, den es bei
	 * vollem RAM nicht gibt, sonst blieben alle Chunks fuer immer geladen. */
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
