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
#include <stdio.h>
#include <stdlib.h>

#include "../game/game_state.h"
#include "../network/server_local.h"
#include "blocks.h"

struct block* blocks[256];

/* Enable with -DCAVEX_DEBUG_BLOCK_PLACEMENT while investigating placement. */
#ifdef CAVEX_DEBUG_BLOCK_PLACEMENT
#define BLOCK_PLACE_DEBUG 1
#else
#define BLOCK_PLACE_DEBUG 0
#endif
#define BLOCK_PLACE_PLAYER_EYE_HEIGHT 1.62F
#define BLOCK_PLACE_PLAYER_WIDTH 0.6F
#define BLOCK_PLACE_PLAYER_HEIGHT 1.8F
#if BLOCK_PLACE_DEBUG
#define BLOCK_PLACE_PRINTF(...) printf(__VA_ARGS__)
#else
#define BLOCK_PLACE_PRINTF(...) ((void)0)
#endif

static void block_place_debug_bbox(const char* label, const struct AABB* a) {
	BLOCK_PLACE_PRINTF("%s=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)", label,
	                  a->x1, a->y1, a->z1, a->x2, a->y2, a->z2);
}

static bool block_place_debug_aabb_overlap(const struct AABB* a,
																   const struct AABB* b) {
	bool x = a->x1 <= b->x2 && b->x1 <= a->x2;
	bool y = a->y1 <= b->y2 && b->y1 <= a->y2;
	bool z = a->z1 <= b->z2 && b->z1 <= a->z2;
	BLOCK_PLACE_PRINTF(" axes=(x:%d y:%d z:%d) overlap=%d", x, y, z,
	                  x && y && z);
	return x && y && z;
}

static bool block_place_collides_with_player(struct server_local* s,
											 struct block_info* blk_info) {
	int player_count = 1;
#ifdef SPLITSCREEN
	player_count = splitscreen_player_count();
#endif
	BLOCK_PLACE_PRINTF("[PLACE_PLAYER] count=%d target=(%d,%d,%d)\n",
	                  player_count, blk_info->x, blk_info->y, blk_info->z);
	for(int i = 0; i < player_count; i++) {
		if(!s->players[i].has_pos || !s->players[i].finished_loading) {
			BLOCK_PLACE_PRINTF(
				"[PLACE_PLAYER] skip player=%d has_pos=%d loaded=%d target=(%d,%d,%d)\n",
				i, s->players[i].has_pos, s->players[i].finished_loading,
				blk_info->x, blk_info->y, blk_info->z);
			continue;
		}

		double player_x = s->players[i].x;
		double player_y = s->players[i].y;
		double player_z = s->players[i].z;
		if(s->placement_player_pos_valid && i == s->active_player_id) {
			player_x = s->placement_player_x;
			player_y = s->placement_player_y;
			player_z = s->placement_player_z;
		}
		float probe_y = player_y;
		struct AABB player_bbox;
		aabb_setsize_centered(&player_bbox, BLOCK_PLACE_PLAYER_WIDTH,
		                      BLOCK_PLACE_PLAYER_HEIGHT,
		                      BLOCK_PLACE_PLAYER_WIDTH);
		aabb_translate(&player_bbox, player_x,
		               probe_y + BLOCK_PLACE_PLAYER_HEIGHT / 2.0F
						 - BLOCK_PLACE_PLAYER_EYE_HEIGHT,
		               player_z);

		bool collision = entity_local_player_block_collide(
			(vec3) {player_x, probe_y, player_z},
			blk_info);
		struct block* target_block = blocks[blk_info->block->type];
		bool direct_collision = false;
		if(target_block && target_block->getBoundingBox) {
			size_t target_bbox_count
				= target_block->getBoundingBox(blk_info, true, NULL);
			struct AABB target_bbox[target_bbox_count > 0 ? target_bbox_count : 1];
			if(target_bbox_count > 0) {
				target_block->getBoundingBox(blk_info, true, target_bbox);
				for(size_t k = 0; k < target_bbox_count; k++) {
					aabb_translate(target_bbox + k, blk_info->x, blk_info->y,
					              blk_info->z);
					BLOCK_PLACE_PRINTF(
						"[PLACE_PLAYER_AABB] player=%d block_type=%u meta=%u bbox=%zu ",
						i, blk_info->block->type, blk_info->block->metadata, k);
					block_place_debug_bbox("player", &player_bbox);
					BLOCK_PLACE_PRINTF(" ");
					block_place_debug_bbox("block", target_bbox + k);
					direct_collision |= block_place_debug_aabb_overlap(
						&player_bbox, target_bbox + k);
					BLOCK_PLACE_PRINTF(" helper=%d direct=%d\n", collision,
					                  direct_collision);
				}
			}
		}
		BLOCK_PLACE_PRINTF(
			"[PLACE_PLAYER] player=%d active=%d pos=(%.3f,%.3f,%.3f) "
			"server=(%.3f,%.3f,%.3f) probe_y=%.3f vel_y=%.3f "
			"target=(%d,%d,%d) type=%u meta=%u placement_valid=%d id=%u ",
			i, s->active_player_id, player_x, player_y, player_z,
			s->players[i].x, s->players[i].y, s->players[i].z,
			probe_y, s->players[i].vel_y, blk_info->x, blk_info->y, blk_info->z,
			blk_info->block->type, blk_info->block->metadata,
			s->placement_player_pos_valid, s->placement_sync_id);
		block_place_debug_bbox("player", &player_bbox);
		BLOCK_PLACE_PRINTF(" helper_collision=%d direct_collision=%d\n",
		                  collision, direct_collision);
		if(direct_collision != collision)
			BLOCK_PLACE_PRINTF(
				"[PLACE_PLAYER_MISMATCH] helper=%d direct=%d target=(%d,%d,%d)\n",
				collision, direct_collision, blk_info->x, blk_info->y, blk_info->z);

		/* The player may have moved between the click and the server handling
		 * the packet. Reject the placement if the current server position also
		 * intersects the block, even when the click position was still valid. */
		if(!collision && s->placement_player_pos_valid
		   && (fabs(player_x - s->players[i].x) > 0.001
		       || fabs(player_y - s->players[i].y) > 0.001
		       || fabs(player_z - s->players[i].z) > 0.001)) {
			float current_probe_y = s->players[i].y;
			bool current_collision = entity_local_player_block_collide(
				(vec3) {s->players[i].x, current_probe_y, s->players[i].z},
				blk_info);
			BLOCK_PLACE_PRINTF(
				"[PLACE_PLAYER] server_pos=(%.3f,%.3f,%.3f) "
				"probe_y=%.3f collision=%d\n",
				s->players[i].x, s->players[i].y, s->players[i].z,
				current_probe_y, current_collision);
			if(current_collision)
				collision = true;
		}

		if(collision)
			return true;
	}

	return false;
}

static bool block_place_collides_with_entity(struct server_local* s,
											 struct block_info* blk_info) {
	struct block *b = blocks[blk_info->block->type];
	if(!b || !b->getBoundingBox)
		return false;

	size_t block_bbox_count = b->getBoundingBox(blk_info, true, NULL);
	if(block_bbox_count == 0)
		return false;

	struct AABB block_bbox[block_bbox_count];
	b->getBoundingBox(blk_info, true, block_bbox);

	for(size_t i = 0; i < block_bbox_count; i++) {
		aabb_translate(block_bbox + i, blk_info->x, blk_info->y, blk_info->z);
		BLOCK_PLACE_PRINTF("[PLACE_BLOCK] target type=%u meta=%u bbox[%zu] ",
		                  blk_info->block->type, blk_info->block->metadata, i);
		block_place_debug_bbox("block", block_bbox + i);
		BLOCK_PLACE_PRINTF("\n");
	}

	dict_entity_it_t it;
	dict_entity_it(it, s->entities);

	while(!dict_entity_end_p(it)) {
		struct entity *e = dict_entity_ref(it)->value;
		dict_entity_next(it);

		// Allow placement through loose item drops, but not through mobs/carts.
		if(!e || !e->getBoundingBox || e->type == ENTITY_ITEM
		   || e->type == ENTITY_LOCAL_PLAYER)
			continue;

		struct AABB entity_bbox;
		if(e->getBoundingBox(e, &entity_bbox) == 0)
			continue;

		bool collision = false;
		for(size_t i = 0; i < block_bbox_count; i++) {
			if(aabb_intersection(block_bbox + i, &entity_bbox)) {
				collision = true;
				break;
			}
		}

		if(collision)
			return true;
	}

	return false;
}

void blocks_init() {
	for(int k = 0; k < 256; k++)
		blocks[k] = NULL;

	render_block_init();

	blocks[1] = &block_stone;
	blocks[2] = &block_grass;
	blocks[3] = &block_dirt;
	blocks[4] = &block_cobblestone;
	blocks[5] = &block_planks;
	blocks[6] = &block_sapling;
	blocks[7] = &block_bedrock;
	blocks[8] = &block_water_flowing;
	blocks[9] = &block_water_still;
	blocks[10] = &block_lava;
	blocks[11] = &block_lava;
	blocks[12] = &block_sand;
	blocks[13] = &block_gravel;
	blocks[14] = &block_goldore;
	blocks[15] = &block_ironore;
	blocks[16] = &block_coalore;
	blocks[17] = &block_log;
	blocks[18] = &block_leaves;
	blocks[19] = &block_sponge;
	blocks[20] = &block_glass;
	blocks[21] = &block_lapisore;
	blocks[22] = &block_lapis;
	blocks[23] = &block_dispenser;
	blocks[24] = &block_sandstone;
	blocks[25] = &block_noteblock;
	blocks[34] = &block_piston_head;
	blocks[26] = &block_bed;
	blocks[27] = &block_powered_rail;
	blocks[28] = &block_detector_rail;
	blocks[29] = &block_sticky_piston;
	blocks[30] = &block_gobweb;
	blocks[31] = &block_tallgrass;
	blocks[32] = &block_deadbush;
	blocks[33] = &block_piston;
	blocks[35] = &block_wool;
	// moving piston head
	blocks[37] = &block_flower;
	blocks[38] = &block_rose;
	blocks[39] = &block_brown_mushroom;
	blocks[40] = &block_red_mushroom;
	blocks[41] = &block_gold;
	blocks[42] = &block_iron;
	blocks[43] = &block_double_slab;
	blocks[44] = &block_slab;
	blocks[45] = &block_bricks;
	blocks[46] = &block_tnt;
	blocks[47] = &block_bookshelf;
	blocks[48] = &block_mossstone;
	blocks[49] = &block_obsidian;
	blocks[50] = &block_torch;
	blocks[51] = &block_fire;
	blocks[52] = &block_spawner;
	blocks[53] = &block_wooden_stairs;
	blocks[54] = &block_chest;
	blocks[55] = &block_redstone_wire;
	blocks[56] = &block_diamondore;
	blocks[57] = &block_diamond;
	blocks[58] = &block_workbench;
	blocks[59] = &block_crops;
	blocks[60] = &block_farmland;
	blocks[61] = &block_furnaceoff;
	blocks[62] = &block_furnaceon;
	// sign standing (may be unused)
	blocks[64] = &block_wooden_door;
	blocks[65] = &block_ladder;
	blocks[66] = &block_rail;
	blocks[67] = &block_stone_stairs;
	blocks[68] = &block_sign;
	blocks[69] = &block_lever;
	blocks[70] = &block_stone_pressure_plate;
	blocks[71] = &block_iron_door;
	blocks[72] = &block_wooden_pressure_plate;
	blocks[73] = &block_redstoneore;
	blocks[74] = &block_redstoneore_lit;
	blocks[75] = &block_redstone_torch;
	blocks[76] = &block_redstone_torch_lit;
	blocks[77] = &block_stone_button;
	blocks[78] = &block_snow;
	blocks[79] = &block_ice;
	blocks[80] = &block_snow_block;
	blocks[81] = &block_cactus;
	blocks[82] = &block_clay;
	blocks[83] = &block_reed;
	blocks[84] = &block_jukebox;
	blocks[85] = &block_fence;
	blocks[86] = &block_pumpkin;
	blocks[87] = &block_netherrack;
	blocks[88] = &block_soulsand;
	blocks[89] = &block_glowstone;
	blocks[90] = &block_portal;
	blocks[91] = &block_pumpkin_lit;
	blocks[92] = &block_cake;
	blocks[93] = &block_repeater_off;
	blocks[94] = &block_repeater_on;

	blocks[96] = &block_trapdoor;
	
	blocks[99] = &block_brown_mushroom_block;
	blocks[100] = &block_red_mushroom_block;
	blocks[101] = &block_iron_bars;
	blocks[102] = &block_glass_pane;
	blocks[103] = &block_melon;
	blocks[104] = &block_pumpkin_stem;
	blocks[105] = &block_melon_stem;
	blocks[106] = &block_vine;
	blocks[107] = &block_fence_gate;
	blocks[108] = &block_brick_stairs;
	blocks[109] = &block_stone_brick_stairs;
	blocks[110] = &block_mycelium;
	blocks[111] = &block_waterlily;
	blocks[112] = &block_nether_brick;
	blocks[113] = &block_nether_brick_fence;
	blocks[114] = &block_nether_brick_stairs;
	blocks[115] = &block_nether_wart;
	blocks[116] = &block_enchanting_table;
	blocks[117] = &block_brewing_stand;
	blocks[118] = &block_cauldron;
	blocks[120] = &block_end_portal_frame;
	blocks[121] = &block_end_stone;
	blocks[123] = &block_redstone_lamp;
	blocks[124] = &block_lit_redstone_lamp;
	blocks[125] = &block_double_wooden_slab;
	blocks[126] = &block_wooden_slab;
	blocks[128] = &block_sandstone_stairs;
	blocks[129] = &block_emerald_ore;
	blocks[130] = &block_ender_chest;
	blocks[131] = &block_tripwire_hook;
	blocks[132] = &block_tripwire;
	blocks[137] = &block_command_block;
	blocks[253] = &block_iron_chest;
	
	blocks[98] = &block_stonebrick;
	blocks[254] = &block_tree2d;
	blocks[255] = &block_record_cat;
	//blocks[98] = &block_minecart;

	for(int k = 0; k < 256; k++) {
		if(blocks[k]) {
			assert(blocks[k]->getMaterial);
			assert(blocks[k]->getTextureIndex);
			assert(blocks[k]->getSideMask);
			assert(blocks[k]->getBoundingBox);
			assert(blocks[k]->renderBlock);
			assert(blocks[k]->getDroppedItem);
			assert(blocks[k]->block_item.renderItem);
			assert(blocks[k]->block_item.onItemPlace);
		}
	}
}

enum side blocks_side_opposite(enum side s) {
	switch(s) {
		default:
		case SIDE_TOP: return SIDE_BOTTOM;
		case SIDE_BOTTOM: return SIDE_TOP;
		case SIDE_LEFT: return SIDE_RIGHT;
		case SIDE_RIGHT: return SIDE_LEFT;
		case SIDE_FRONT: return SIDE_BACK;
		case SIDE_BACK: return SIDE_FRONT;
	}
}

const char* block_side_name(enum side s) {
	switch(s) {
		case SIDE_TOP: return "top";
		case SIDE_BOTTOM: return "bottom";
		case SIDE_LEFT: return "left";
		case SIDE_RIGHT: return "right";
		case SIDE_FRONT: return "front";
		case SIDE_BACK: return "back";
		default: return "invalid";
	}
}

void blocks_side_offset(enum side s, int* x, int* y, int* z) {
	assert(x && y && z);

	switch(s) {
		default:
		case SIDE_TOP:
			*x = 0;
			*y = 1;
			*z = 0;
			break;
		case SIDE_BOTTOM:
			*x = 0;
			*y = -1;
			*z = 0;
			break;
		case SIDE_LEFT:
			*x = -1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_RIGHT:
			*x = 1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_BACK:
			*x = 0;
			*y = 0;
			*z = 1;
			break;
		case SIDE_FRONT:
			*x = 0;
			*y = 0;
			*z = -1;
			break;
	}
}

bool block_place_default(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = it->durability,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
	blk_info.block = &blk;

	bool player_collision = block_place_collides_with_player(s, &blk_info);
	bool entity_collision = block_place_collides_with_entity(s, &blk_info);
	const char* decision = player_collision || entity_collision
		? "SERVER_REJECT" : "SERVER_ACCEPT";
	const char* reason = player_collision ? "player" :
		(entity_collision ? "entity" : "none");
	BLOCK_PLACE_PRINTF("[PLACE_RESULT] decision=%s reason=%s item=%u "
	                  "target=(%d,%d,%d) player=%d entity=%d allowed=%d\n",
	                  decision, reason,
	                  it->id, blk_info.x, blk_info.y, blk_info.z,
	                  player_collision, entity_collision,
	                  !player_collision && !entity_collision);

	if(player_collision || entity_collision)
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

size_t block_drop_default(struct block_info* this, struct item_data* it,
						  struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = this->block->type;
		it->durability = 0;
		it->count = 1;
	}

	return 1;
}

bool block_can_support_attachment(uint8_t type) {
	return blocks[type] && (!blocks[type]->can_see_through || type == BLOCK_GLASS);
}

static enum side redstone_torch_support_side(const struct block_data* blk) {
	switch(blk->metadata & 0x7) {
		case 1: return SIDE_LEFT;
		case 2: return SIDE_RIGHT;
		case 3: return SIDE_FRONT;
		case 4: return SIDE_BACK;
		default: return SIDE_BOTTOM;
	}
}

static enum side repeater_output_side(const struct block_data* blk) {
	switch(blk->metadata & 0x03) {
		case 0: return SIDE_FRONT;
		case 1: return SIDE_RIGHT;
		case 2: return SIDE_BACK;
		default: return SIDE_LEFT;
	}
}

static bool redstone_button_support_matches(const struct block_data* blk,
														 enum side side_to_support);
static bool redstone_lever_support_matches(const struct block_data* blk,
														  enum side side_to_support);

static bool redstone_connectable_to_wire(uint8_t type, uint8_t meta,
										 enum side side_to_wire) {
	if(type == BLOCK_REDSTONE_WIRE)
		return true;
	if(type == BLOCK_REDSTONE_TORCH || type == BLOCK_REDSTONE_TORCH_LIT) {
		struct block_data blk = { .type = type, .metadata = meta };
		return side_to_wire != redstone_torch_support_side(&blk);
	}
	if(type == BLOCK_REPEATER_OFF || type == BLOCK_REPEATER_ON) {
		struct block_data blk = { .type = type, .metadata = meta };
		enum side out = repeater_output_side(&blk);
		return side_to_wire == out || side_to_wire == blocks_side_opposite(out);
	}
	if(type == BLOCK_LEVER)
		return true;
	if(type == BLOCK_STONE_PRESSURE_PLATE
	   || type == BLOCK_WOOD_PRESSURE_PLATE)
		return true;
	if(type == BLOCK_DETECTOR_RAIL)
		return true;
	if(type == BLOCK_STONE_BUTTON)
		return true;
	return false;
}

static int redstone_wire_side_index(enum side side_to_target) {
	switch(side_to_target) {
		case SIDE_RIGHT: return 0;
		case SIDE_LEFT: return 1;
		case SIDE_FRONT: return 2;
		case SIDE_BACK: return 3;
		default: return -1;
	}
}

bool block_redstone_wire_points_to_side(struct server_local* s, w_coord_t x,
										w_coord_t y, w_coord_t z,
										enum side side_to_target) {
	struct block_data cur;
	if(!server_world_get_block(AWORLD(s), x, y, z, &cur)
	   || cur.type != BLOCK_REDSTONE_WIRE)
		return false;
	if((cur.metadata & 0x0F) == 0)
		return false;

	int target_index = redstone_wire_side_index(side_to_target);
	if(target_index < 0)
		return true;

	const struct { int dx, dz; enum side side; } dirs[4] = {
		{ +1,  0, SIDE_RIGHT },
		{ -1,  0, SIDE_LEFT },
		{  0, -1, SIDE_FRONT },
		{  0, +1, SIDE_BACK },
	};
	uint8_t dir_state[4] = { 0, 0, 0, 0 };
	struct block_data above;
	bool above_air = server_world_get_block(AWORLD(s), x, y + 1, z, &above)
					 && above.type == BLOCK_AIR;

	for(int i = 0; i < 4; i++) {
		struct block_data nb;
		if(!server_world_get_block(AWORLD(s), x + dirs[i].dx, y,
								   z + dirs[i].dz, &nb))
			nb = (struct block_data){ .type = BLOCK_AIR, .metadata = 0 };

		if(redstone_connectable_to_wire(nb.type, nb.metadata & 0x0F,
										blocks_side_opposite(dirs[i].side))) {
			dir_state[i] = 1;
			continue;
		}

		if(nb.type == BLOCK_AIR) {
			struct block_data lower;
			if(server_world_get_block(AWORLD(s), x + dirs[i].dx, y - 1,
									  z + dirs[i].dz, &lower)
			   && lower.type == BLOCK_REDSTONE_WIRE) {
				dir_state[i] = 1;
				continue;
			}
		}

		if(above_air && blocks[nb.type] && !blocks[nb.type]->can_see_through) {
			struct block_data upper;
			if(server_world_get_block(AWORLD(s), x + dirs[i].dx, y + 1,
									  z + dirs[i].dz, &upper)
			   && upper.type == BLOCK_REDSTONE_WIRE)
				dir_state[i] = 2;
		}
	}

	bool has_x = dir_state[0] != 0 || dir_state[1] != 0;
	bool has_z = dir_state[2] != 0 || dir_state[3] != 0;
	if(has_x && !has_z)
		return side_to_target == SIDE_RIGHT || side_to_target == SIDE_LEFT;
	if(has_z && !has_x)
		return side_to_target == SIDE_FRONT || side_to_target == SIDE_BACK;
	return dir_state[target_index] != 0;
}

bool block_redstone_powers_side(const struct block_data* blk,
								enum side side_to_target) {
	uint8_t meta = blk->metadata & 0x0F;

	switch(blk->type) {
		case BLOCK_REDSTONE_WIRE:
			return meta > 0;
		case BLOCK_REDSTONE_TORCH_LIT:
			return side_to_target != redstone_torch_support_side(blk);
		case BLOCK_LEVER:
			return (meta & 0x08) != 0
				&& !redstone_lever_support_matches(blk, side_to_target);
		case BLOCK_STONE_BUTTON:
			return (meta & 0x08) != 0
				&& redstone_button_support_matches(blk, side_to_target);
		case BLOCK_STONE_PRESSURE_PLATE:
		case BLOCK_WOOD_PRESSURE_PLATE:
			return (meta & 0x01) != 0;
		case BLOCK_DETECTOR_RAIL:
			return (meta & 0x08) != 0;
		case BLOCK_TRIPWIRE_HOOK:
			return (meta & 0x08) != 0;
		case BLOCK_REPEATER_ON:
			return repeater_output_side(blk) == side_to_target;
		default:
			return false;
	}
}

bool block_redstone_powers_wire(const struct block_data* blk,
								enum side side_to_wire) {
	return block_redstone_powers_side(blk, side_to_wire);
}

static bool redstone_button_support_matches(const struct block_data* blk,
													   enum side side_to_support) {
	switch(blk->metadata & 0x07) {
		case 1: return side_to_support == SIDE_LEFT;
		case 2: return side_to_support == SIDE_RIGHT;
		case 3: return side_to_support == SIDE_FRONT;
		case 4: return side_to_support == SIDE_BACK;
		case 5: return side_to_support == SIDE_BOTTOM;
		case 6: return side_to_support == SIDE_TOP;
		default: return false;
	}
}

static bool redstone_lever_support_matches(const struct block_data* blk,
																	  enum side side_to_support) {
	switch(blk->metadata & 0x07) {
		case 1: return side_to_support == SIDE_LEFT;
		case 2: return side_to_support == SIDE_RIGHT;
		case 3: return side_to_support == SIDE_FRONT;
		case 4: return side_to_support == SIDE_BACK;
		/* Both top-mounted lever variants are supported from below. */
		case 5:
		case 6: return side_to_support == SIDE_BOTTOM;
		case 7: return side_to_support == SIDE_TOP;
		default: return false;
	}
}

static bool redstone_pressure_plate_support_matches(
														const struct block_data* blk,
														 enum side side_to_support) {
	(void)blk;
	/* Pressure plates are mounted on the top face of their support block. */
	return side_to_support == SIDE_BOTTOM;
}

bool block_redstone_is_button_powered_block(struct server_local* s, w_coord_t x,
											w_coord_t y, w_coord_t z) {
	for(int side = 0; side < SIDE_MAX; side++) {
		int ox, oy, oz;
		struct block_data nb;

		blocks_side_offset((enum side)side, &ox, &oy, &oz);
		if(!server_world_get_block(AWORLD(s), x + ox, y + oy, z + oz, &nb))
			continue;
		if((nb.type != BLOCK_STONE_BUTTON && nb.type != BLOCK_LEVER
			&& nb.type != BLOCK_STONE_PRESSURE_PLATE
			&& nb.type != BLOCK_WOOD_PRESSURE_PLATE
			&& nb.type != BLOCK_DETECTOR_RAIL))
			continue;
		bool powered = (nb.type == BLOCK_STONE_PRESSURE_PLATE
			|| nb.type == BLOCK_WOOD_PRESSURE_PLATE)
			? (nb.metadata & 0x01) != 0
			: (nb.metadata & 0x08) != 0;
		if(!powered)
			continue;
		bool matches;
		if(nb.type == BLOCK_LEVER)
			matches = redstone_lever_support_matches(&nb,
					blocks_side_opposite((enum side)side));
		else if(nb.type == BLOCK_STONE_BUTTON)
			matches = redstone_button_support_matches(&nb,
					blocks_side_opposite((enum side)side));
		else
			matches = redstone_pressure_plate_support_matches(&nb,
					blocks_side_opposite((enum side)side));
		if(matches)
			return true;
	}

	return false;
}

bool block_redstone_is_powered(struct server_local* s, w_coord_t x,
							   w_coord_t y, w_coord_t z) {
	for(int side = 0; side < SIDE_MAX; side++) {
		int ox, oy, oz;
		struct block_data nb;

		blocks_side_offset((enum side)side, &ox, &oy, &oz);
		if(!server_world_get_block(AWORLD(s), x + ox, y + oy, z + oz, &nb))
			continue;

		enum side side_to_target = blocks_side_opposite((enum side)side);
		if(nb.type == BLOCK_REDSTONE_WIRE) {
			if(block_redstone_wire_points_to_side(s, x + ox, y + oy, z + oz,
														 side_to_target))
				return true;
			continue;
		}

		/* A lever/button/pressure plate powers its own solid support block. */
		if(blocks[nb.type] && !blocks[nb.type]->can_see_through
		   && block_redstone_is_button_powered_block(
				s, x + ox, y + oy, z + oz))
			return true;

		if(block_redstone_powers_side(&nb, side_to_target))
			return true;
	}

	return false;
}

static const int dx[6] = {  1, -1,  0,  0,  0,  0 };
static const int dy[6] = {  0,  0,  0,  0,  1, -1 };
static const int dz[6] = {  0,  0,  1, -1,  0,  0 };

void notifyNeighbours(struct server_local* s,
                      w_coord_t x, w_coord_t y, w_coord_t z)
{
    for (int i = 0; i < 6; i++) {
        w_coord_t nx = x + dx[i];
        w_coord_t ny = y + dy[i];
        w_coord_t nz = z + dz[i];

        struct block_data nb;
        if (!server_world_get_block(AWORLD(s), nx, ny, nz, &nb))
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
}

