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

#include "../network/client_interface.h"
#include "../network/inventory_logic.h"
#include "../network/server_local.h"
#include "blocks.h"

#define FURNACE_COOK_TOTAL 200
#define FURNACE_FUEL_UNIT 200

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
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

static bool furnace_side_is_front(struct block_info* this, enum side side) {
	switch(side) {
		case SIDE_FRONT: return this->block->metadata == 2;
		case SIDE_BACK: return this->block->metadata == 3;
		case SIDE_LEFT: return this->block->metadata == 4;
		case SIDE_RIGHT: return this->block->metadata == 5;
		default: return false;
	}
}

static uint8_t getTextureIndex1(struct block_info* this, enum side side) {
	switch(side) {
		case SIDE_TOP:
		case SIDE_BOTTOM:
			return tex_atlas_lookup(TEXAT_FURNACE_TOP);
		default:
			return tex_atlas_lookup(furnace_side_is_front(this, side)
										? TEXAT_FURNACE_FRONT
										: TEXAT_FURNACE_SIDE);
	}
}

static uint8_t getTextureIndex2(struct block_info* this, enum side side) {
	switch(side) {
		case SIDE_TOP:
		case SIDE_BOTTOM:
			return tex_atlas_lookup(TEXAT_FURNACE_TOP);
		default:
			return tex_atlas_lookup(furnace_side_is_front(this, side)
										? TEXAT_FURNACE_FRONT_LIT
										: TEXAT_FURNACE_SIDE);
	}
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	int metadata = 0;
	int player_id = 0;
	double dx = s->players[player_id].x - (where->x + 0.5);
	double dz = s->players[player_id].z - (where->z + 0.5);

	if(fabs(dx) > fabs(dz)) {
		metadata = (dx >= 0) ? 5 : 4;
	} else {
		metadata = (dz >= 0) ? 3 : 2;
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
		   (vec3) {s->players[player_id].x, s->players[player_id].y,
				   s->players[player_id].z},
		   &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	const uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	if(player->active_inventory == &player->inventory) {
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(pid)
			.type = CRPC_OPEN_WINDOW,
			.payload.window_open.window = WINDOWC_FURNACE,
			.payload.window_open.type = WINDOW_TYPE_FURNACE,
			.payload.window_open.slot_count = FURNACE_SIZE,
		});

		struct inventory* inv = malloc(sizeof(struct inventory));
		inventory_create(inv, &inventory_logic_furnace, s, FURNACE_SIZE, on->x,
						 on->y, on->z);
		player->active_inventory = inv;
	}
}

static bool furnace_can_output(struct furnace_data* furnace,
							   struct item_data result) {
	struct item_data output = furnace->items[FURNACE_SLOT_OUTPUT];
	if(result.id == 0)
		return false;

	if(output.id == 0)
		return true;

	if(output.id != result.id || output.durability != result.durability)
		return false;

	struct item* out_type = item_get(&output);
	return out_type && output.count + result.count <= out_type->max_stack;
}

static void furnace_clear_data(struct furnace_data* furnace) {
	if(!furnace)
		return;

	furnace->pos.x = -1;
	furnace->pos.y = -1;
	furnace->pos.z = -1;
	memset(furnace->items, 0, sizeof(furnace->items));
	furnace->burn_time = 0;
	furnace->burn_total = 0;
	furnace->cook_time = 0;
	furnace->cook_total = 0;
}

static void furnace_finish_smelt(struct furnace_data* furnace,
								 struct item_data result) {
	struct item_data* input = &furnace->items[FURNACE_SLOT_INPUT];
	struct item_data* output = &furnace->items[FURNACE_SLOT_OUTPUT];

	if(output->id == 0) {
		*output = result;
	} else {
		output->count += result.count;
	}

	if(input->count > 1) {
		input->count--;
	} else {
		input->id = 0;
		input->durability = 0;
		input->count = 0;
	}
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = 61;
		it->durability = 0;
		it->count = 1;
	} else {
		struct furnace_data* furnace
			= furnace_data_get(s, this->x, this->y, this->z, false);

		if(furnace) {
			for(int i = 0; i < FURNACE_SIZE_STORAGE; i++) {
				if(furnace->items[i].id != 0) {
					server_local_spawn_item(
						(vec3) {this->x + 0.5F, this->y + 0.5F, this->z + 0.5F},
						&furnace->items[i], false, s);
				}
			}

			furnace_clear_data(furnace);
		}
	}

	return 1;
}

static void onWorldTick(struct server_local* s, struct block_info* this) {
	struct furnace_data* furnace
		= furnace_data_get(s, this->x, this->y, this->z, true);
	if(!furnace)
		return;

	bool changed_items = false;
	bool changed_state = false;

	struct item_data input = furnace->items[FURNACE_SLOT_INPUT];
	struct item_data fuel = furnace->items[FURNACE_SLOT_INPUT + 1];
	struct item_data result = furnace_recipe_result(input);
	bool can_smelt = furnace_can_output(furnace, result);

	if(furnace->burn_time > 0) {
		furnace->burn_time--;
		changed_state = true;
	}

	if(furnace->burn_time == 0 && can_smelt && fuel.id != 0) {
		struct item* fuel_item = item_get(&fuel);
		if(fuel_item && fuel_item->fuel > 0) {
			furnace->burn_total = fuel_item->fuel * FURNACE_FUEL_UNIT;
			furnace->burn_time = furnace->burn_total;
			furnace->cook_total = FURNACE_COOK_TOTAL;
			changed_state = true;

			if(fuel.count > 1) {
				furnace->items[FURNACE_SLOT_INPUT + 1].count--;
			} else {
				furnace->items[FURNACE_SLOT_INPUT + 1].id = 0;
				furnace->items[FURNACE_SLOT_INPUT + 1].durability = 0;
				furnace->items[FURNACE_SLOT_INPUT + 1].count = 0;
			}
			changed_items = true;
		}
	}

	if(furnace->burn_time > 0 && can_smelt) {
		if(furnace->cook_total == 0)
			furnace->cook_total = FURNACE_COOK_TOTAL;
		furnace->cook_time++;
		changed_state = true;

		if(furnace->cook_time >= furnace->cook_total) {
			furnace_finish_smelt(furnace, result);
			furnace->cook_time = 0;
			changed_items = true;
			changed_state = true;
		}
	} else if(furnace->cook_time != 0) {
		furnace->cook_time = 0;
		changed_state = true;
	}

	if(!can_smelt && furnace->cook_total != 0) {
		furnace->cook_total = FURNACE_COOK_TOTAL;
	}

	uint8_t want_type = furnace->burn_time > 0 ? 62 : 61;
	if(this->block->type != want_type) {
		struct block_data blk = *this->block;
		blk.type = want_type;
		server_world_set_block(s, this->x, this->y, this->z, blk);
	}

	if(changed_items || changed_state)
		furnace_send_updates(s, this->x, this->y, this->z, changed_items,
							 changed_state);
}

struct block block_furnaceoff = {
	.name = "Furnace",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex1,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = render_block_furnace,
	.renderBlockAlways = NULL,
	.luminance = 0, //depends on metadata
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 5250,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 2,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_furnaceon = {
	.name = "Furnace",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex2,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = render_block_furnace,
	.renderBlockAlways = NULL,
	.luminance = 13,
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 5250,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 2,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
