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

#include <math.h>

#include "../network/client_interface.h"
#include "../network/inventory_logic.h"
#include "../network/server_local.h"
#include "blocks.h"

#define CHEST_DOUBLE_LEFT 0x04
#define CHEST_DOUBLE_RIGHT 0x08
#define CHEST_DOUBLE_MASK (CHEST_DOUBLE_LEFT | CHEST_DOUBLE_RIGHT)

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_WOOD;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_full();
}

static void chest_pair_offset(uint8_t direction, int* dx, int* dz) {
	switch(direction & 0x03) {
		case 0: *dx = -1; *dz = 0; break;
		case 1: *dx = 0; *dz = -1; break;
		case 2: *dx = 1; *dz = 0; break;
		default: *dx = 0; *dz = 1; break;
	}
}

static void chest_double_anchor(const struct block_info* block,
								w_coord_t* x, w_coord_t* z) {
	uint8_t metadata = block->block->metadata;
	int dx, dz;

	*x = block->x;
	*z = block->z;
	if(metadata & CHEST_DOUBLE_RIGHT) {
		chest_pair_offset(metadata, &dx, &dz);
		*x -= dx;
		*z -= dz;
	}
}

static int chest_storage_index(struct server_local* s,
							 const struct block_info* block) {
	w_coord_t x, z;
	chest_double_anchor(block, &x, &z);

	for(int i = 0; i < MAX_CHESTS; i++) {
		if(s->chest_pos[i].x == x && s->chest_pos[i].y == block->y
		   && s->chest_pos[i].z == z)
			return i;
	}
	return -1;
}

static bool chest_is_double(const struct block_info* block) {
	return (block->block->metadata & CHEST_DOUBLE_MASK) != 0;
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	uint8_t tex[SIDE_MAX] = {
		[SIDE_TOP] = tex_atlas_lookup(TEXAT_CHEST_TOP),
		[SIDE_BOTTOM] = tex_atlas_lookup(TEXAT_CHEST_TOP),
		[SIDE_BACK] = tex_atlas_lookup(TEXAT_CHEST_SIDE),
		[SIDE_FRONT] = tex_atlas_lookup(TEXAT_CHEST_SIDE),
		[SIDE_LEFT] = tex_atlas_lookup(TEXAT_CHEST_SIDE),
		[SIDE_RIGHT] = tex_atlas_lookup(TEXAT_CHEST_SIDE),
	};

	uint8_t metadata = this->block->metadata;
	uint8_t direction = metadata & 0x03;

	if(metadata & CHEST_DOUBLE_MASK) {
		bool left = (metadata & CHEST_DOUBLE_RIGHT) != 0;
		tex[SIDE_TOP] = tex_atlas_lookup(left ? TEXAT_CHEST_TOP_1
												: TEXAT_CHEST_TOP_2);
		tex[SIDE_BOTTOM] = tex[SIDE_TOP];
		tex[SIDE_FRONT] = tex_atlas_lookup(left ? TEXAT_CHEST_FRONT_2
												 : TEXAT_CHEST_FRONT_1);
		tex[SIDE_BACK] = tex_atlas_lookup(left ? TEXAT_CHEST_BACK_1
												: TEXAT_CHEST_BACK_2);
			switch(direction) {
			case 1:
				tex[SIDE_RIGHT] = tex[SIDE_FRONT];
				tex[SIDE_LEFT] = tex[SIDE_BACK];
				break;
			case 2:
				{
					uint16_t front = tex[SIDE_FRONT];
					tex[SIDE_FRONT] = tex[SIDE_BACK];
					tex[SIDE_BACK] = front;
				}
				break;
			case 3:
				tex[SIDE_LEFT] = tex[SIDE_FRONT];
				tex[SIDE_RIGHT] = tex[SIDE_BACK];
				break;
			default:
				break;
		}
		return tex[side];
	}

	switch(direction) {
		case 0: tex[SIDE_FRONT] = tex_atlas_lookup(TEXAT_CHEST_FRONT_SINGLE); break;
		case 1: tex[SIDE_RIGHT] = tex_atlas_lookup(TEXAT_CHEST_FRONT_SINGLE); break;
		case 2: tex[SIDE_BACK] = tex_atlas_lookup(TEXAT_CHEST_FRONT_SINGLE); break;
		case 3: tex[SIDE_LEFT] = tex_atlas_lookup(TEXAT_CHEST_FRONT_SINGLE); break;
	}

 	return tex[side];
}

static void onRightClick(struct server_local* s, struct item_data* it,
							 struct block_info* where, struct block_info* on,
							 enum side on_side) {
	const uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	w_coord_t anchor_x = on->x;
	w_coord_t anchor_z = on->z;
	uint8_t window_type = WINDOW_TYPE_CHEST;
	size_t slot_count = CHEST_SIZE;
	struct inventory_logic* logic = &inventory_logic_chest;

	(void)it;
	(void)where;
	(void)on_side;
	if(chest_is_double(on)) {
		chest_double_anchor(on, &anchor_x, &anchor_z);
		window_type = WINDOW_TYPE_IRON_CHEST;
		slot_count = IRON_CHEST_SIZE;
		logic = &inventory_logic_iron_chest;
	}

	if(player->active_inventory == &player->inventory) {
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(pid)
			.type = CRPC_OPEN_WINDOW,
			.payload.window_open.window = chest_is_double(on)
				? WINDOWC_IRON_CHEST : WINDOWC_CHEST,
			.payload.window_open.type = window_type,
			.payload.window_open.slot_count = slot_count,
		});

		struct inventory* inv = malloc(sizeof(struct inventory));
		inventory_create(inv, logic, s, slot_count, anchor_x, on->y, anchor_z);
		player->active_inventory = inv;
	}
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	const uint8_t pid = s->active_player_id;
	const double dx = s->players[pid].x - (where->x + 0.5);
	const double dz = s->players[pid].z - (where->z + 0.5);
	uint8_t metadata;

	if(fabs(dx) > fabs(dz)) {
		metadata = (dx >= 0) ? 1 : 3;
	} else {
		metadata = (dz >= 0) ? 2 : 0;
	}

	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = metadata,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
	blk_info.block = &blk;

	if(entity_local_player_block_collide(
		   (vec3) {s->players[pid].x, s->players[pid].y, s->players[pid].z},
		   &blk_info))
		return false;

	int pair_dx, pair_dz;
	chest_pair_offset(metadata, &pair_dx, &pair_dz);
	struct block_data neighbour;
	struct block_info neighbour_info;
	int neighbour_dx = 0, neighbour_dz = 0;
	bool found_neighbour = false;
	for(int side = -1; side <= 1 && !found_neighbour; side += 2) {
		neighbour_dx = pair_dx * side;
		neighbour_dz = pair_dz * side;
		if(server_world_get_block(AWORLD(s), where->x + neighbour_dx,
							  where->y, where->z + neighbour_dz, &neighbour)
		   && neighbour.type == BLOCK_CHEST
		   && (neighbour.metadata & 0x03) == metadata
		   && !(neighbour.metadata & CHEST_DOUBLE_MASK)) {
			found_neighbour = true;
		}
	}
	if(found_neighbour) {
		neighbour_info = (struct block_info) {
			.block = &neighbour,
			.x = where->x + neighbour_dx,
			.y = where->y,
			.z = where->z + neighbour_dz,
		};
		int storage = chest_storage_index(s, &neighbour_info);
		if(storage < 0)
			return false;

		bool new_is_left = neighbour_dx == pair_dx && neighbour_dz == pair_dz;
		uint8_t left_metadata = metadata | CHEST_DOUBLE_LEFT;
		uint8_t right_metadata = metadata | CHEST_DOUBLE_RIGHT;
		struct block_data left = new_is_left ? blk : neighbour;
		struct block_data right = new_is_left ? neighbour : blk;
		left.metadata = left_metadata;
		right.metadata = right_metadata;

		if(new_is_left) {
			s->chest_pos[storage].x = where->x;
			s->chest_pos[storage].z = where->z;
		} else {
			s->chest_pos[storage].x = neighbour_info.x;
			s->chest_pos[storage].z = neighbour_info.z;
		}
		memset(s->chest_items[storage] + CHEST_SIZE_STORAGE, 0,
		       CHEST_SIZE_STORAGE * sizeof(struct item_data));
		server_world_set_block(s, s->chest_pos[storage].x,
							 s->chest_pos[storage].y,
							 s->chest_pos[storage].z, left);
		server_world_set_block(s,
							 new_is_left ? neighbour_info.x : where->x,
							 where->y,
							 new_is_left ? neighbour_info.z : where->z, right);
		return true;
	}

	for (int i=0; i<MAX_CHESTS; i++) {
		if (s->chest_pos[i].y < 0) {
			s->chest_pos[i].x = where->x;
			s->chest_pos[i].y = where->y;
			s->chest_pos[i].z = where->z;
			memset(&s->chest_items[i], 0, MAX_CHEST_SLOTS * sizeof(struct item_data));
			server_world_set_block(s, where->x, where->y, where->z, blk);
			return true;
		}
	}

#if 0
	puts("Too many chests");
#endif
	return false;
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = this->block->type;
		it->durability = this->block->metadata & 0x03;
		it->count = 1;
	} else { //only free chest on first run of getDroppedItem
		if(chest_is_double(this)) {
			int storage = chest_storage_index(s, this);
			if(storage >= 0) {
				uint8_t metadata = this->block->metadata;
				uint8_t direction = metadata & 0x03;
				bool broken_left = metadata & CHEST_DOUBLE_LEFT;
				int pair_dx, pair_dz;
				w_coord_t remaining_x = this->x;
				w_coord_t remaining_z = this->z;
				int first_slot;

				chest_pair_offset(direction, &pair_dx, &pair_dz);
				if(broken_left) {
					remaining_x += pair_dx;
					remaining_z += pair_dz;
					first_slot = 0;
				} else {
					remaining_x -= pair_dx;
					remaining_z -= pair_dz;
					first_slot = CHEST_SIZE_STORAGE;
				}

				struct block_data remaining;
				if(server_world_get_block(AWORLD(s), remaining_x, this->y,
									 remaining_z, &remaining)) {
					remaining.metadata = direction;
					server_world_set_block(s, remaining_x, this->y, remaining_z,
										 remaining);
					s->chest_pos[storage].x = remaining_x;
					s->chest_pos[storage].z = remaining_z;

					for(int j = 0; j < CHEST_SIZE_STORAGE; j++) {
						struct item_data* item
							= &s->chest_items[storage][first_slot + j];
						if(item->id) {
							server_local_spawn_item(
									(vec3) {this->x + 0.5F, this->y + 0.5F,
											this->z + 0.5F},
									item, false, s);
						}
						memset(item, 0, sizeof(*item));
					}
					if(broken_left) {
						memmove(s->chest_items[storage],
								s->chest_items[storage] + CHEST_SIZE_STORAGE,
								CHEST_SIZE_STORAGE * sizeof(struct item_data));
					}
					memset(s->chest_items[storage] + CHEST_SIZE_STORAGE, 0,
						   CHEST_SIZE_STORAGE * sizeof(struct item_data));
					return 1;
				}
			}
		}

		for(int i=0; i<MAX_CHESTS; i++) {
			if(s->chest_pos[i].x == this->x && s->chest_pos[i].y == this->y && s->chest_pos[i].z == this->z) {
				for(int j=0; j<MAX_CHEST_SLOTS; j++) {
					if(s->chest_items[i][j].id) {
						server_local_spawn_item((vec3) {this->x + 0.5F,
									this->y + 0.5F,
									this->z + 0.5F},
									&s->chest_items[i][j], false, s);
					}
				}
				s->chest_pos[i].y = -1;
				break;
			}
		}
	}

	return 1;
}

struct block block_chest = {
	.name = "Chest",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_chest,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 3750,
	.digging.tool = TOOL_TYPE_AXE,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_MAX,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 2,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 0,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
