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

#ifndef SERVER_WORLD_H
#define SERVER_WORLD_H

#include "../m-lib/m-dict.h"
#include <stdbool.h>
#include <stdint.h>

#include "region_archive.h"

struct server_chunk {
	uint8_t* ids;
	uint8_t* metadata;
	uint8_t* lighting_sky;
	uint8_t* lighting_torch;
	uint8_t* heightmap;
	bool modified;
};

enum server_world_pending_phase {
	SERVER_WORLD_PENDING_NONE = 0,
	SERVER_WORLD_PENDING_TERRAIN,
	SERVER_WORLD_PENDING_FEATURES,
	SERVER_WORLD_PENDING_DECO,
	SERVER_WORLD_PENDING_FINALIZE,
};

struct server_world_pending_chunk {
	bool active;
	w_coord_t x;
	w_coord_t z;
	enum server_world_pending_phase phase;
	int next_column;
	int surface_map[CHUNK_SIZE * CHUNK_SIZE];
	struct server_chunk chunk;
};

#define MAX_REGIONS 4
#define S_CHUNK_ID(x, z) (((int64_t)(z) << 32) | (((int64_t)(x) & 0xFFFFFFFF)))
#define S_CHUNK_X(id) ((int32_t)((id) & 0xFFFFFFFF))
#define S_CHUNK_Z(id) ((int32_t)((id) >> 32))

// key not!!! stored in multiples of CHUNK_SIZE
DICT_DEF2(dict_server_chunks, int64_t, M_BASIC_OPLIST, struct server_chunk,
		  M_POD_OPLIST)

enum server_world_cuberite_biome_gen {
	SERVER_WORLD_CUBERITE_BIOME_GEN_CONSTANT,
	SERVER_WORLD_CUBERITE_BIOME_GEN_CHECKERBOARD,
	SERVER_WORLD_CUBERITE_BIOME_GEN_VORONOI,
	SERVER_WORLD_CUBERITE_BIOME_GEN_DISTORTED_VORONOI,
	SERVER_WORLD_CUBERITE_BIOME_GEN_TWO_LEVEL,
	SERVER_WORLD_CUBERITE_BIOME_GEN_MULTI_STEP_MAP,
	SERVER_WORLD_CUBERITE_BIOME_GEN_GROWN,
};

enum server_world_cuberite_shape_gen {
	SERVER_WORLD_CUBERITE_SHAPE_GEN_BIOMAL_NOISE_3D,
	SERVER_WORLD_CUBERITE_SHAPE_GEN_DISTORTED_HEIGHTMAP,
	SERVER_WORLD_CUBERITE_SHAPE_GEN_HEIGHTMAP,
	SERVER_WORLD_CUBERITE_SHAPE_GEN_NOISE_3D,
	SERVER_WORLD_CUBERITE_SHAPE_GEN_END,
};

enum server_world_cuberite_composition_gen {
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_BIOMAL,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_BIOMAL_NOISE_3D,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_CLASSIC,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_DEBUG_BIOMES,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_DISTORTED_HEIGHTMAP,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_END,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_NETHER,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_NOISE_3D,
	SERVER_WORLD_CUBERITE_COMPOSITION_GEN_SAME_BLOCK,
};

struct server_world_cuberite_config {
	enum server_world_cuberite_biome_gen biome_gen;
	enum server_world_cuberite_shape_gen shape_gen;
	enum server_world_cuberite_composition_gen composition_gen;

	int biome_gen_cache_size;
	int biome_gen_multi_cache_length;
	int sea_level;
	float biomal_noise3d_frequency_x;
	float biomal_noise3d_frequency_y;
	float biomal_noise3d_frequency_z;
	float biomal_noise3d_base_frequency_x;
	float biomal_noise3d_base_frequency_z;
	float biomal_noise3d_choice_frequency_x;
	float biomal_noise3d_choice_frequency_y;
	float biomal_noise3d_choice_frequency_z;
	float biomal_noise3d_air_threshold;
	int biomal_noise3d_num_choice_octaves;
	int biomal_noise3d_num_density_octaves;
	int biomal_noise3d_num_base_octaves;
	float biomal_noise3d_base_amplitude;
	int composition_gen_cache_size;

	bool finisher_rough_ravines;
	int rough_ravines_grid_size;
	int rough_ravines_max_offset;
	int rough_ravines_max_size;
	int rough_ravines_min_size;
	float rough_ravines_max_center_width;
	float rough_ravines_min_center_width;
	float rough_ravines_max_roughness;
	float rough_ravines_min_roughness;
	float rough_ravines_max_floor_height_edge;
	float rough_ravines_min_floor_height_edge;
	float rough_ravines_max_floor_height_center;
	float rough_ravines_min_floor_height_center;
	float rough_ravines_max_ceiling_height_edge;
	float rough_ravines_min_ceiling_height_edge;
	float rough_ravines_max_ceiling_height_center;
	float rough_ravines_min_ceiling_height_center;

	bool finisher_worm_nest_caves;
	int worm_nest_caves_size;
	int worm_nest_caves_grid;
	int worm_nest_max_offset;

	bool finisher_water_lakes;
	int water_lakes_probability;
	bool finisher_water_springs;
	bool finisher_lava_lakes;
	int lava_lakes_probability;
	bool finisher_lava_springs;
	bool finisher_ore_nests;

	bool finisher_mineshafts;
	int mineshafts_grid_size;
	int mineshafts_max_offset;
	int mineshafts_max_system_size;
	int mineshafts_chance_corridor;
	int mineshafts_chance_crossing;
	int mineshafts_chance_staircase;

	bool finisher_trees;
	bool finisher_villages;
	bool finisher_single_piece_structures;
	bool finisher_tall_grass;
	bool finisher_sprinkle_foliage;
	bool finisher_ice;
	bool finisher_snow;
	bool finisher_lilypads;
	bool finisher_bottom_lava;
	int bottom_lava_level;
	bool finisher_dead_bushes;
	bool finisher_natural_patches;
	bool finisher_pre_simulator;
	bool pre_simulator_falling_blocks;
	bool pre_simulator_water;
	bool pre_simulator_lava;
	bool finisher_animals;
	bool finisher_overworld_clump_flowers;
};

struct server_world {
	dict_server_chunks_t chunks;
	enum world_dim dimension;
	int64_t world_seed;
	struct server_world_cuberite_config generator;
	string_t level_name;
	struct region_archive loaded_regions[MAX_REGIONS];
	ilist_regions_t loaded_regions_lru;
	size_t loaded_regions_length;
	struct server_world_pending_chunk pending_chunk;
	bool initialized;
};

void server_world_create(struct server_world* w, string_t level_name,
						 enum world_dim dimension);
void server_world_set_cuberite_defaults(struct server_world* w);
void server_world_set_seed(struct server_world* w, int64_t seed);
void server_world_destroy(struct server_world* w);

bool server_world_get_block(struct server_world* w, w_coord_t x, w_coord_t y,
							w_coord_t z, struct block_data* blk);
bool server_world_set_block(struct server_local* s, w_coord_t x, w_coord_t y, w_coord_t z, struct block_data blk);

bool server_world_furthest_chunk(struct server_world* w, w_coord_t dist,
								 w_coord_t px, w_coord_t pz, w_coord_t* x,
								 w_coord_t* z);

bool server_world_is_chunk_loaded(struct server_world* w, w_coord_t x,
								  w_coord_t z);
bool server_world_pending_chunk(struct server_world* w, w_coord_t* x,
								w_coord_t* z);
bool server_world_load_chunk(struct server_world* w, w_coord_t x, w_coord_t z,
							 struct server_chunk** sc);
void server_world_save_chunk(struct server_world* w, bool erase, w_coord_t x,
							 w_coord_t z);
void server_world_save_chunk_obj(struct server_world* w, bool erase,
								 w_coord_t x, w_coord_t z,
								 struct server_chunk* c);
struct region_archive* server_world_chunk_region(struct server_world* w,
												 w_coord_t x, w_coord_t z);
bool server_world_disk_has_chunk(struct server_world* w, w_coord_t x,
								 w_coord_t z);
void server_world_tick(struct server_world* w, struct server_local* s);
void server_world_random_tick(struct server_world* w, struct random_gen* g,
							  struct server_local* s, w_coord_t px,
							  w_coord_t pz, w_coord_t dist);
void server_world_explode(struct server_local *s, vec3 center, float power);

bool server_world_find_empty_spot_nearby(const float pos[3], const struct server_world *world, float out_pos[3]);

#endif
