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
};

static struct gen_biome_profile gen_profile_for_biome(int biome_id) {
	struct gen_biome_profile p = {
		.base_height = 63.0f,
		.amplitude = 9.0f,
		.top_block = BLOCK_GRASS,
		.filler_block = BLOCK_DIRT,
		.oceanic = false,
	};

	if(isOceanic(biome_id) || biome_id == river || biome_id == frozen_river) {
		p.base_height = 49.0f;
		p.amplitude = 4.0f;
		p.top_block = BLOCK_SAND;
		p.filler_block = BLOCK_SAND;
		p.oceanic = true;
		return p;
	}

	switch(biome_id) {
		case desert:
		case desert_hills:
		case desert_lakes:
		case badlands:
		case badlands_plateau:
		case wooded_badlands_plateau:
		case eroded_badlands:
		case modified_badlands_plateau:
		case modified_wooded_badlands_plateau:
			p.base_height = 64.0f;
			p.amplitude = 6.0f;
			p.top_block = BLOCK_SAND;
			p.filler_block = BLOCK_SAND;
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

		case beach:
		case stone_shore:
		case snowy_beach:
			p.base_height = 62.0f;
			p.amplitude = 4.0f;
			p.top_block = BLOCK_SAND;
			p.filler_block = BLOCK_SAND;
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
		if(isSnowy(biome_id))
			snowy_votes++;
	}

	struct gen_biome_profile out = {
		.base_height = base / wsum,
		.amplitude = amp / wsum,
		.top_block = BLOCK_GRASS,
		.filler_block = BLOCK_DIRT,
		.oceanic = ocean_votes >= 3,
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

static void gen_try_place_tree(struct server_chunk* sc, int lx, int lz, int y,
							   uint32_t seed, int world_x, int world_z,
							   int biome_id) {
	uint8_t wood_meta = 0;
	if(biome_id == birch_forest || biome_id == birch_forest_hills)
		wood_meta = 2;
	else if(biome_id == taiga || biome_id == taiga_hills
			|| biome_id == giant_tree_taiga || biome_id == giant_tree_taiga_hills
			|| biome_id == snowy_taiga || biome_id == snowy_taiga_hills)
		wood_meta = 1;

	int trunk = 4 + (int)(gen_rand01_from_hash(
							  gen_hash3i(world_x, y, world_z, seed ^ 0xA17E0AA1U))
						  * 3.0f);
	if(wood_meta == 1)
		trunk++;
	if(y + trunk + 2 >= WORLD_HEIGHT)
		return;

	uint8_t ground = gen_get_block(sc, lx, y - 1, lz);
	if(ground != BLOCK_GRASS && ground != BLOCK_DIRT)
		return;

	for(int h = 0; h <= trunk + 1; h++) {
		if(gen_get_block(sc, lx, y + h, lz) != BLOCK_AIR)
			return;
	}

	for(int ly = y + trunk - 3; ly <= y + trunk + 1; ly++) {
		int rel = ly - (y + trunk);
		int radius = 2 - (abs(rel) / 2);
		if(rel == 1)
			radius = 1;
		for(int dz = -radius; dz <= radius; dz++) {
			for(int dx = -radius; dx <= radius; dx++) {
				if(!gen_inside_chunk(lx + dx, ly, lz + dz))
					continue;
				if(abs(dx) == radius && abs(dz) == radius && radius > 0) {
					float c = gen_rand01_from_hash(
						gen_hash3i(world_x + dx, ly, world_z + dz, seed ^ 0xACED5511U));
					if(c > 0.45f)
						continue;
				}
				uint8_t cur = gen_get_block(sc, lx + dx, ly, lz + dz);
				if(cur == BLOCK_AIR)
					gen_set_block_with_meta(sc, lx + dx, ly, lz + dz, BLOCK_LEAVES,
											wood_meta);
			}
		}
	}

	for(int h = 0; h < trunk; h++)
		gen_set_block_with_meta(sc, lx, y + h, lz, BLOCK_LOG, wood_meta);
	gen_set_block_with_meta(sc, lx, y + trunk, lz, BLOCK_LEAVES, wood_meta);
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

static int gen_chunk_surface_height(const struct server_chunk* sc, int lx, int lz) {
	if(!sc || !sc->heightmap)
		return -1;
	uint8_t h = sc->heightmap[lx + lz * CHUNK_SIZE];
	if(h == 0)
		return -1;
	return (int)h - 1;
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

static bool server_world_generate_chunk(struct server_world* w, w_coord_t x,
										w_coord_t z, struct server_chunk** out) {
	assert(w && out);

	if(server_world_is_chunk_loaded(w, x, z)) {
		*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
		return *out != NULL;
	}

#ifdef PLATFORM_WII
	size_t total_fast = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
	struct server_chunk fast = {
		.ids = calloc(total_fast, 1),
		.metadata = calloc(total_fast / 2, 1),
		.lighting_sky = calloc(total_fast / 2, 1),
		.lighting_torch = calloc(total_fast / 2, 1),
		.heightmap = calloc(CHUNK_SIZE * CHUNK_SIZE, 1),
		.modified = true,
	};
	if(!fast.ids || !fast.metadata || !fast.lighting_sky || !fast.lighting_torch
	   || !fast.heightmap) {
		free(fast.ids);
		free(fast.metadata);
		free(fast.lighting_sky);
		free(fast.lighting_torch);
		free(fast.heightmap);
		return false;
	}

	uint32_t seed_fast = (uint32_t)w->world_seed;
	int32_t world_x0_fast = x * CHUNK_SIZE;
	int32_t world_z0_fast = z * CHUNK_SIZE;

	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			int32_t wx = world_x0_fast + lx;
			int32_t wz = world_z0_fast + lz;
			float h1 = gen_fbm2d(wx * 0.0060f, wz * 0.0060f, seed_fast ^ 0x11AABBCCU, 2,
								 2.0f, 0.5f);
			float h2 = gen_fbm2d(wx * 0.0180f, wz * 0.0180f, seed_fast ^ 0x77CC22AAU, 1,
								 2.0f, 0.5f);
			float m = gen_fbm2d(wx * 0.0019f, wz * 0.0019f, seed_fast ^ 0x55DD33AAU, 2,
								2.0f, 0.5f);
			int surface = (int)(63.0f + h1 * 11.0f + h2 * 2.0f + fmaxf(0.0f, m) * 8.0f);
			if(surface < 6)
				surface = 6;
			if(surface > WORLD_HEIGHT - 2)
				surface = WORLD_HEIGHT - 2;

			float dry = gen_fbm2d(wx * 0.0025f, wz * 0.0025f, seed_fast ^ 0x12EF56ABU, 1,
								  2.0f, 0.5f);
			uint8_t top_block = BLOCK_GRASS;
			uint8_t filler_block = BLOCK_DIRT;
			if(surface <= GEN_SEA_LEVEL + 1 || dry > 0.42f) {
				top_block = BLOCK_SAND;
				filler_block = BLOCK_SAND;
			} else if(m > 0.35f && surface > GEN_SEA_LEVEL + 8) {
				top_block = BLOCK_STONE;
				filler_block = BLOCK_STONE;
			}

			fast.ids[S_CHUNK_IDX(lx, 0, lz)] = BLOCK_BEDROCK;
			for(int y = 1; y < surface - 4; y++)
				fast.ids[S_CHUNK_IDX(lx, y, lz)] = BLOCK_STONE;
			for(int y = surface - 4; y < surface; y++) {
				if(y > 0)
					fast.ids[S_CHUNK_IDX(lx, y, lz)] = filler_block;
			}
			fast.ids[S_CHUNK_IDX(lx, surface, lz)] = top_block;
			for(int y = surface + 1; y <= GEN_SEA_LEVEL && y < WORLD_HEIGHT; y++)
				fast.ids[S_CHUNK_IDX(lx, y, lz)] = BLOCK_WATER_STILL;

			for(int y = 8; y < surface - 3; y++) {
				float ore = gen_rand01_from_hash(gen_hash3i(wx, y, wz, seed_fast ^ 0x91919191U));
				if(ore > 0.994f)
					fast.ids[S_CHUNK_IDX(lx, y, lz)] = (y < 16) ? BLOCK_DIAMOND_ORE :
													 (y < 30) ? BLOCK_GOLD_ORE :
													 (y < 64) ? BLOCK_IRON_ORE :
																BLOCK_COAL_ORE;
			}
		}
	}

	gen_recompute_height_and_skylight(&fast);
	dict_server_chunks_set_at(w->chunks, S_CHUNK_ID(x, z), fast);
	*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
	return *out != NULL;
#endif

	size_t total = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
	struct server_chunk sc = {
		.ids = calloc(total, 1),
		.metadata = calloc(total / 2, 1),
		.lighting_sky = calloc(total / 2, 1),
		.lighting_torch = calloc(total / 2, 1),
		.heightmap = calloc(CHUNK_SIZE * CHUNK_SIZE, 1),
		.modified = true,
	};

	if(!sc.ids || !sc.metadata || !sc.lighting_sky || !sc.lighting_torch
	   || !sc.heightmap) {
		free(sc.ids);
		free(sc.metadata);
		free(sc.lighting_sky);
		free(sc.lighting_torch);
		free(sc.heightmap);
		return false;
	}

	uint32_t seed = (uint32_t)w->world_seed;
	Generator biome_gen;
	setupGenerator(&biome_gen, MC_1_7, 0);
	applySeed(&biome_gen, DIM_OVERWORLD, (uint64_t)w->world_seed);
	int32_t world_x0 = x * CHUNK_SIZE;
	int32_t world_z0 = z * CHUNK_SIZE;

	for(int lx = 0; lx < CHUNK_SIZE; lx++) {
		for(int lz = 0; lz < CHUNK_SIZE; lz++) {
			int32_t wx = world_x0 + lx;
			int32_t wz = world_z0 + lz;

				int biome_id = plains;
				struct gen_biome_profile profile
					= gen_blended_profile(&biome_gen, wx, wz, &biome_id);

				float continental = gen_fbm2d(wx * 0.0046f, wz * 0.0046f,
											  seed ^ 0x13579BDFU, 4, 2.0f, 0.5f);
				float mountain = gen_fbm2d(wx * 0.0020f, wz * 0.0020f,
										   seed ^ 0x4AFEB19DU, 4, 2.0f, 0.5f);
				float micro = gen_fbm2d(wx * 0.012f, wz * 0.012f, seed ^ 0xA53C9E3DU,
										2, 2.0f, 0.5f);

				float mountain_lift = fmaxf(0.0f, mountain);
				float mountain_factor = 0.85f;
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

				if(profile.oceanic) {
					float island_mask = gen_fbm2d(wx * 0.0018f, wz * 0.0018f,
												  seed ^ 0x6A1D51E1U, 3, 2.0f, 0.5f);
					if(island_mask < 0.08f) {
						surface = fminf(surface, GEN_SEA_LEVEL - 2);
					} else {
						float t = (island_mask - 0.08f) / 0.32f;
						if(t < 0.0f)
							t = 0.0f;
						if(t > 1.0f)
							t = 1.0f;
						float lift = t * t * (3.0f - 2.0f * t);
						int target = (int)(GEN_SEA_LEVEL - 1 + lift * 10.0f);
						if(target > surface)
							surface = target;
						if(lift < 0.35f && surface > GEN_SEA_LEVEL + 2)
							surface = GEN_SEA_LEVEL + 2;
					}
				}
				if(surface < 6)
					surface = 6;
				if(surface > WORLD_HEIGHT - 2)
					surface = WORLD_HEIGHT - 2;
				if(profile.oceanic && surface > GEN_SEA_LEVEL - 2)
					surface = GEN_SEA_LEVEL - 2;
				if(surface < GEN_SEA_LEVEL - 20)
					surface = GEN_SEA_LEVEL - 20;

				// Dry sandy biomes should not form inland water trenches.
				if(gen_is_dry_sandy_biome(biome_id) && !profile.oceanic
				   && biome_id != beach && biome_id != river) {
					if(surface < GEN_SEA_LEVEL + 1)
						surface = GEN_SEA_LEVEL + 1;
					if(surface <= GEN_SEA_LEVEL + 4)
						surface = GEN_SEA_LEVEL + 2;
				}

				float neigh_h = 0.0f;
				float neigh_w = 0.0f;
				int sample_h = 0;

				if(lx <= 3
				   && gen_sample_neighbor_edge_height(w, x, z, 0, lz, &sample_h)) {
					float k = (4.0f - (float)lx) / 4.0f;
					neigh_h += (float)sample_h * k;
					neigh_w += k;
				}
				if(lx >= CHUNK_SIZE - 4
				   && gen_sample_neighbor_edge_height(w, x, z, 1, lz, &sample_h)) {
					float k
						= (4.0f - (float)((CHUNK_SIZE - 1) - lx)) / 4.0f;
					neigh_h += (float)sample_h * k;
					neigh_w += k;
				}
				if(lz <= 3
				   && gen_sample_neighbor_edge_height(w, x, z, 2, lx, &sample_h)) {
					float k = (4.0f - (float)lz) / 4.0f;
					neigh_h += (float)sample_h * k;
					neigh_w += k;
				}
				if(lz >= CHUNK_SIZE - 4
				   && gen_sample_neighbor_edge_height(w, x, z, 3, lx, &sample_h)) {
					float k
						= (4.0f - (float)((CHUNK_SIZE - 1) - lz)) / 4.0f;
					neigh_h += (float)sample_h * k;
					neigh_w += k;
				}

				if(neigh_w > 0.0f) {
					float target = neigh_h / neigh_w;
					surface = (int)((float)surface * 0.70f + target * 0.30f);
					if(surface < 6)
						surface = 6;
					if(surface > WORLD_HEIGHT - 2)
						surface = WORLD_HEIGHT - 2;
				}

				int dirt_depth = 4;
				if(profile.top_block == BLOCK_SAND)
					dirt_depth = 5;
				if(profile.top_block == BLOCK_STONE)
					dirt_depth = 2;
				bool submerged = surface <= GEN_SEA_LEVEL;
				uint8_t top_block = profile.top_block;
				uint8_t filler_block = profile.filler_block;
				float cave_threshold = gen_cave_threshold_for_biome(biome_id);

				if(surface >= GEN_SEA_LEVEL - 2 && surface <= GEN_SEA_LEVEL + 2
				   && !profile.oceanic && profile.top_block != BLOCK_STONE) {
					top_block = BLOCK_SAND;
					filler_block = BLOCK_SAND;
					dirt_depth = 4;
				}

				if(submerged) {
					float sea_floor
						= gen_fbm2d(wx * 0.028f, wz * 0.028f, seed ^ 0x51A17E2BU, 2,
									2.0f, 0.5f);
					bool gravel_floor
						= (profile.top_block == BLOCK_STONE) || (sea_floor > 0.35f);
					if(gravel_floor) {
						top_block = BLOCK_GRAVEL;
						filler_block = BLOCK_STONE;
					} else {
						top_block = BLOCK_SAND;
						filler_block = BLOCK_SAND;
					}
				}

			for(int y = 0; y < WORLD_HEIGHT; y++) {
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				uint8_t block = BLOCK_AIR;

				if(y == 0) {
					block = BLOCK_BEDROCK;
				} else if(y <= surface) {
					bool carve = false;
						// Keep upper terrain compact: caves only deep underground
						// and never as open shafts from surface.
						if(y > 8 && y < surface - 20 && y < 32) {
							float cave = gen_value_noise3d(
								wx * 0.060f, y * 0.075f, wz * 0.060f,
								seed ^ 0xD00DFEEDU);
							float cave2 = gen_value_noise3d(
							wx * 0.105f, y * 0.045f, wz * 0.105f,
							seed ^ 0xC001D00DU);
							float cave_mix = cave * 0.72f + cave2 * 0.28f;
							float cave_bias = (float)(y - 24) / 80.0f;
							float threshold = cave_threshold + 0.06f + cave_bias * 0.10f;
							if(isSnowy(biome_id)) {
								// Snow biomes: avoid maze-like trenches close to surface.
								if(y > 40)
									threshold += 0.22f;
								else
									threshold += 0.10f;
							}
							if(gen_is_dry_sandy_biome(biome_id))
								threshold += 0.12f;
							carve = cave_mix > threshold;
						}

						if(!carve) {
							if(y == surface) {
								block = top_block;
							} else if(y >= surface - dirt_depth) {
								block = filler_block;
							} else {
								block = BLOCK_STONE;
							}
						}
				} else if(y <= GEN_SEA_LEVEL) {
					block = BLOCK_WATER_STILL;
				}

				sc.ids[idx] = block;
				gen_set_nibble(sc.metadata, idx, 0);
				gen_set_nibble(sc.lighting_torch, idx, 0);
			}

				if(surface > GEN_SEA_LEVEL + 1 && isSnowy(biome_id)) {
					size_t top_idx = S_CHUNK_IDX(lx, surface + 1, lz);
					if(sc.ids[top_idx] == BLOCK_AIR)
						sc.ids[top_idx] = BLOCK_SNOW;
			}

			for(int y = 5; y < surface - 3; y++) {
				gen_set_ore_if_stone(&sc, lx, y, lz, BLOCK_COAL_ORE,
									 seed ^ 0x1111U, 0.985f);
				if(y < 72)
					gen_set_ore_if_stone(&sc, lx, y, lz, BLOCK_IRON_ORE,
										 seed ^ 0x2222U, 0.992f);
				if(y < 32)
					gen_set_ore_if_stone(&sc, lx, y, lz, BLOCK_GOLD_ORE,
										 seed ^ 0x3333U, 0.9975f);
				if(y < 18)
					gen_set_ore_if_stone(&sc, lx, y, lz, BLOCK_DIAMOND_ORE,
										 seed ^ 0x4444U, 0.9986f);
			}

			// Prevent trench artifacts: keep a thick near-surface shell compact
			// in all biomes, with biome-appropriate filler.
			for(int y = surface - 14; y <= surface - 1; y++) {
				if(y <= 1 || y >= WORLD_HEIGHT - 1)
					continue;
				size_t idx = S_CHUNK_IDX(lx, y, lz);
				if(sc.ids[idx] != BLOCK_AIR)
					continue;

				if(top_block == BLOCK_SAND) {
					sc.ids[idx] = (y >= surface - 3) ? BLOCK_SAND : BLOCK_STONE;
				} else if(top_block == BLOCK_STONE) {
					sc.ids[idx] = BLOCK_STONE;
				} else {
					sc.ids[idx] = (y >= surface - 4) ? BLOCK_DIRT : BLOCK_STONE;
				}
			}

			}
		}

		for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
			for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
			int y = -1;
			for(int yy = WORLD_HEIGHT - 2; yy >= 1; yy--) {
				uint8_t b = sc.ids[S_CHUNK_IDX(lx, yy, lz)];
				if(b != BLOCK_AIR && b != BLOCK_WATER_STILL && b != BLOCK_WATER_FLOW) {
					y = yy;
					break;
				}
			}
			if(y < 1)
				continue;

			int wx = world_x0 + lx;
			int wz = world_z0 + lz;
				int biome_id = gen_biome_at_safe(&biome_gen, wx, wz);
				float deco = gen_rand01_from_hash(
					gen_hash3i(wx, y, wz, seed ^ 0xABCD1234U));

				uint8_t top = sc.ids[S_CHUNK_IDX(lx, y, lz)];

				if((biome_id == forest || biome_id == birch_forest
					|| biome_id == flower_forest || biome_id == taiga
					|| biome_id == taiga_hills || biome_id == giant_tree_taiga
					|| biome_id == giant_tree_taiga_hills || biome_id == swamp
					|| biome_id == jungle || biome_id == jungle_hills)
				   && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
				   && deco > ((biome_id == forest || biome_id == birch_forest) ? 0.96f :
																		 0.975f)) {
					gen_try_place_tree(&sc, lx, lz, y + 1, seed, wx, wz, biome_id);
					continue;
				}

				if((biome_id == plains || biome_id == forest || biome_id == flower_forest)
				   && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR) {
					if(deco > 0.94f)
						gen_set_block_with_meta(&sc, lx, y + 1, lz, BLOCK_TALL_GRASS, 1);
					else if(deco > 0.90f)
						sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_FLOWER;
				}

				if((biome_id == taiga || biome_id == taiga_hills || biome_id == giant_tree_taiga
					|| biome_id == giant_tree_taiga_hills || biome_id == snowy_taiga
					|| biome_id == snowy_taiga_hills)
				   && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.90f) {
					gen_set_block_with_meta(&sc, lx, y + 1, lz, BLOCK_TALL_GRASS, 2);
				}

				if((biome_id == desert || biome_id == desert_hills)
				   && top == BLOCK_SAND
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.985f
				   && y + 2 < WORLD_HEIGHT) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_CACTUS;
					if(deco > 0.995f)
						sc.ids[S_CHUNK_IDX(lx, y + 2, lz)] = BLOCK_CACTUS;
				}

				if((biome_id == desert || biome_id == desert_hills || biome_id == badlands
					|| biome_id == badlands_plateau || biome_id == wooded_badlands_plateau)
				   && top == BLOCK_SAND
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.955f) {
					if(sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR)
						sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = 32;
				}

				if((biome_id == forest || biome_id == roofedForest || biome_id == swamp)
				   && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.985f) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = (deco > 0.992f) ?
						BLOCK_RED_MUSHROOM :
						BLOCK_BROWM_MUSHROOM;
				}

				if((biome_id == swamp || biome_id == river)
				   && y >= GEN_SEA_LEVEL - 1 && y <= GEN_SEA_LEVEL + 1
				   && sc.ids[S_CHUNK_IDX(lx, y, lz)] == BLOCK_WATER_STILL
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.985f) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_WATERLILY;
				}

				if((biome_id == swamp || biome_id == river || biome_id == beach)
				   && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
				   && (gen_is_water(&sc, lx + 1, y, lz) || gen_is_water(&sc, lx - 1, y, lz)
					   || gen_is_water(&sc, lx, y, lz + 1)
					   || gen_is_water(&sc, lx, y, lz - 1))
				   && deco > 0.975f && y + 2 < WORLD_HEIGHT) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_REED;
					if(deco > 0.992f && gen_is_air(&sc, lx, y + 2, lz))
						sc.ids[S_CHUNK_IDX(lx, y + 2, lz)] = BLOCK_REED;
				}

				if((biome_id == plains || biome_id == forest) && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
				   && deco > 0.9975f) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_PUMPKIN;
				}

				if((biome_id == jungle || biome_id == jungle_hills) && top == BLOCK_GRASS
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR && deco > 0.997f) {
					sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_MELON;
				}

				if((biome_id == jungle || biome_id == jungle_hills || biome_id == swamp)
				   && sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] == BLOCK_AIR
				   && y > GEN_SEA_LEVEL - 2 && deco > 0.94f) {
					bool near_leaf_or_log
						= (gen_get_block(&sc, lx + 1, y + 1, lz) == BLOCK_LEAVES
						   || gen_get_block(&sc, lx - 1, y + 1, lz) == BLOCK_LEAVES
						   || gen_get_block(&sc, lx, y + 1, lz + 1) == BLOCK_LEAVES
						   || gen_get_block(&sc, lx, y + 1, lz - 1) == BLOCK_LEAVES
						   || gen_get_block(&sc, lx + 1, y + 1, lz) == BLOCK_LOG
						   || gen_get_block(&sc, lx - 1, y + 1, lz) == BLOCK_LOG
						   || gen_get_block(&sc, lx, y + 1, lz + 1) == BLOCK_LOG
						   || gen_get_block(&sc, lx, y + 1, lz - 1) == BLOCK_LOG);
					if(near_leaf_or_log)
						sc.ids[S_CHUNK_IDX(lx, y + 1, lz)] = BLOCK_VINE;
				}
			}
		}

	gen_try_place_dungeon(&sc, seed, world_x0, world_z0);
	gen_place_lava_sources(&sc, seed, world_x0, world_z0);

	for(int lx = 1; lx < CHUNK_SIZE - 1; lx++) {
		for(int lz = 1; lz < CHUNK_SIZE - 1; lz++) {
			int wx = world_x0 + lx;
			int wz = world_z0 + lz;
			int biome_id = gen_biome_at_safe(&biome_gen, wx, wz);
			if(!(isOceanic(biome_id) || biome_id == river || biome_id == swamp))
				continue;

			for(int y = GEN_SEA_LEVEL - 12; y <= GEN_SEA_LEVEL - 1; y++) {
				if(y < 2 || y >= WORLD_HEIGHT - 1)
					continue;
				if(!gen_is_water(&sc, lx, y + 1, lz))
					continue;

				uint8_t here = gen_get_block(&sc, lx, y, lz);
				if(here != BLOCK_SAND && here != BLOCK_DIRT && here != BLOCK_GRAVEL)
					continue;

				float p = gen_rand01_from_hash(
					gen_hash3i(wx, y, wz, seed ^ 0x7733AA11U));
				if(p > 0.92f)
					gen_set_block(&sc, lx, y, lz, BLOCK_CLAY);
				else if(p > 0.78f)
					gen_set_block(&sc, lx, y, lz, BLOCK_GRAVEL);
			}
		}
	}

	gen_surface_artifact_cleanup(&sc);
	gen_recompute_height_and_skylight(&sc);

	dict_server_chunks_set_at(w->chunks, S_CHUNK_ID(x, z), sc);
	*out = dict_server_chunks_get(w->chunks, S_CHUNK_ID(x, z));
	return *out != NULL;
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
			w->initialized = false;
			return;
		}

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

	if(server_world_is_chunk_loaded(w, x, z))
		return false;

	// Ensure the corresponding region archive is loaded (LRU is tiny; scanning
	// can evict the region we need before load happens).
	struct region_archive* ra = server_world_chunk_region(w, x, z);
	if(!ra)
		return server_world_generate_chunk(w, x, z, sc);

	bool chunk_exists = false;
	if(!region_archive_contains(ra, x, z, &chunk_exists) || !chunk_exists)
		return server_world_generate_chunk(w, x, z, sc);

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
