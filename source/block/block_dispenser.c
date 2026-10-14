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

#include "../network/server_local.h"
#include "blocks.h"
#include "../network/client_interface.h"
#include "../network/inventory_logic.h"

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

/* Dispenser metadata stores the outward-facing side using the old block
 * rotation values, which are not the same as enum side values. */
static enum side dispenser_output_side(uint8_t metadata) {
	switch(metadata & 0x7) {
		case 2: return SIDE_FRONT;
		case 3: return SIDE_BACK;
		case 4: return SIDE_LEFT;
		case 5: return SIDE_RIGHT;
		default: return SIDE_FRONT;
	}
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	switch(side) {
		case SIDE_FRONT:
			if(this->block->metadata == 2)
				return tex_atlas_lookup(TEXAT_DISPENSER_FRONT);
			else
				return tex_atlas_lookup(TEXAT_FURNACE_SIDE);
		case SIDE_BACK:
			if(this->block->metadata == 3)
				return tex_atlas_lookup(TEXAT_DISPENSER_FRONT);
			else
				return tex_atlas_lookup(TEXAT_FURNACE_SIDE);
		case SIDE_RIGHT:
			if(this->block->metadata == 5)
				return tex_atlas_lookup(TEXAT_DISPENSER_FRONT);
			else
				return tex_atlas_lookup(TEXAT_FURNACE_SIDE);
		case SIDE_LEFT:
			if(this->block->metadata == 4)
				return tex_atlas_lookup(TEXAT_DISPENSER_FRONT);
			else
				return tex_atlas_lookup(TEXAT_FURNACE_SIDE);
		default: return tex_atlas_lookup(TEXAT_FURNACE_TOP);
	}
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	int metadata = 0;
	int player_id = s->active_player_id;
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
(vec3) {s->players[player_id].x, s->players[player_id].y, s->players[player_id].z}, &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	for(int i = 0; i < MAX_DISPENSERS; i++) {
		if(s->dispenser_pos[i].y < 0) {
			s->dispenser_pos[i] = (struct complex_block_pos) {
				.x = where->x, .y = where->y, .z = where->z
			};
			memset(s->dispenser_items[i], 0,
				   sizeof(s->dispenser_items[i]));
			s->dispenser_powered[i]
				= block_redstone_is_powered(s, where->x, where->y, where->z);
			break;
		}
	}
	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	const uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	if(player->active_inventory != &player->inventory)
		return;

	clin_rpc_send(&(struct client_rpc) {
		CRPC_PLAYER_ID(pid)
		.type = CRPC_OPEN_WINDOW,
		.payload.window_open.window = WINDOWC_DISPENSER,
		.payload.window_open.type = WINDOW_TYPE_DISPENSER,
		.payload.window_open.slot_count = DISPENSER_SIZE,
	});

	struct inventory* inv = malloc(sizeof(*inv));
	inventory_create(inv, &inventory_logic_dispenser, s, DISPENSER_SIZE,
					 on->x, on->y, on->z);
	player->active_inventory = inv;
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	bool powered = block_redstone_is_powered(s, info->x, info->y, info->z);
	for(int i = 0; i < MAX_DISPENSERS; i++) {
		if(s->dispenser_pos[i].x == info->x
		   && s->dispenser_pos[i].y == info->y
		   && s->dispenser_pos[i].z == info->z) {
			if(powered && !s->dispenser_powered[i]) {
				int available[DISPENSER_SIZE_STORAGE];
				int count = 0;
				for(int k = 0; k < DISPENSER_SIZE_STORAGE; k++) {
					if(s->dispenser_items[i][k].id)
						available[count++] = k;
				}

				if(count > 0) {
					int slot = available[rand_gen_range(&s->rand_src, 0, count)];
					struct item_data item = s->dispenser_items[i][slot];
					item.count = 1;
					if(--s->dispenser_items[i][slot].count == 0)
						s->dispenser_items[i][slot] = (struct item_data) {0};

					int ox, oy, oz;
					blocks_side_offset(dispenser_output_side(info->block->metadata),
										 &ox, &oy, &oz);
					vec3 pos = {
						info->x + 0.5F + ox * 0.7F,
						info->y + 0.5F + oy * 0.7F,
						info->z + 0.5F + oz * 0.7F,
					};
					vec3 velocity = {ox * 0.25F, oy * 0.25F + 0.08F,
									 oz * 0.25F};
					server_local_spawn_item_velocity(pos, &item, velocity, s);
				}
			}
			s->dispenser_powered[i] = powered;
			break;
		}
	}

}

static void dispenser_drop_contents(struct block_info* this,
									struct server_local* s) {
	for(int i = 0; i < MAX_DISPENSERS; i++) {
		if(s->dispenser_pos[i].x == this->x
		   && s->dispenser_pos[i].y == this->y
		   && s->dispenser_pos[i].z == this->z) {
			for(int k = 0; k < DISPENSER_SIZE_STORAGE; k++) {
				if(s->dispenser_items[i][k].id)
					server_local_spawn_item(
						(vec3) {this->x + 0.5F, this->y + 0.5F, this->z + 0.5F},
						&s->dispenser_items[i][k], false, s);
			}
			s->dispenser_pos[i].y = -1;
			memset(s->dispenser_items[i], 0,
				   sizeof(s->dispenser_items[i]));
			return;
		}
	}
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
								 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = this->block->type;
		it->durability = this->block->metadata;
		it->count = 1;
	} else {
		dispenser_drop_contents(this, s);
	}
	return 1;
}

struct block block_dispenser = {
	.name = "Dispenser",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
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
