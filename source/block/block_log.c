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

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	const uint8_t metadata = this->block->metadata;
	const uint8_t species = metadata & 0x03;
	const bool end = ((metadata & 0x04)
		&& (side == SIDE_LEFT || side == SIDE_RIGHT))
		|| ((metadata & 0x08)
			&& (side == SIDE_FRONT || side == SIDE_BACK))
		|| (!(metadata & 0x0C)
			&& (side == SIDE_TOP || side == SIDE_BOTTOM));

	if(end)
		return tex_atlas_lookup(TEXAT_LOG_OAK_TOP);

	switch(species) {
		default:
		case 0: return tex_atlas_lookup(TEXAT_LOG_OAK_SIDE);
		case 1: return tex_atlas_lookup(TEXAT_LOG_SPRUCE_SIDE);
		case 2: return tex_atlas_lookup(TEXAT_LOG_BIRCH_SIDE);
	}
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	uint8_t axis = 0;
	switch(on_side) {
		case SIDE_LEFT:
		case SIDE_RIGHT: axis = 0x04; break;
		case SIDE_FRONT:
		case SIDE_BACK: axis = 0x08; break;
		default: break;
	}

	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = (it->durability & 0x03) | axis,
		.sky_light = 0,
		.torch_light = 0,
	};
	struct block_info blk_info = *where;
	blk_info.block = &blk;

	if(entity_local_player_block_collide(
			(vec3) {s->players[s->active_player_id].x,
					 s->players[s->active_player_id].y,
					 s->players[s->active_player_id].z}, &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = this->block->type;
		it->durability = this->block->metadata & 0x03;
		it->count = 1;
	}

	return 1;
}

struct block block_log = {
	.name = "Log",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = true,
	.place_ignore = false,
	.digging.hardness = 3000,
	.digging.tool = TOOL_TYPE_AXE,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_MAX,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 1,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
