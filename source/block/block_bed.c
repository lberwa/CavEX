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
#include "../network/server_local.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_WOOD;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F, 0.5625F, 1.0F);
	return 1;
}

// TODO: add missing bed legs to mask? (difficult, many states)
static struct face_occlusion side_mask = {
	.mask = {0x00000000, 0x00000000, 0x00000000, 0x0000FFFF, 0xFFFFFFFF,
			 0xFFFFFFFF, 0xFFFF0000, 0x00000000},
};

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	switch(side) {
		case SIDE_TOP:
		case SIDE_BOTTOM: return face_occlusion_empty();
		default: return &side_mask;
	}
}

static enum side bed_head_side(uint8_t metadata) {
	switch(metadata & 0x3) {
		default:
		case 0: return SIDE_BACK;
		case 1: return SIDE_RIGHT;
		case 2: return SIDE_FRONT;
		case 3: return SIDE_LEFT;
	}
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	bool head = (this->block->metadata & 0x8) != 0;
	enum side head_side = bed_head_side(this->block->metadata);

	if(side == SIDE_TOP)
		return tex_atlas_lookup(head ? TEXAT_BED_TOP_2 : TEXAT_BED_TOP_1);
	if(side == SIDE_BOTTOM)
		return tex_atlas_lookup(TEXAT_PLANKS);

	if(head)
		return side == head_side ? tex_atlas_lookup(TEXAT_BED_FRONT)
		                         : tex_atlas_lookup(TEXAT_BED_SIDE_2);

	return side == blocks_side_opposite(head_side)
	         ? tex_atlas_lookup(TEXAT_BED_BACK)
	         : tex_atlas_lookup(TEXAT_BED_SIDE_1);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
    if (it) {
        it->id = ITEM_BED;
        it->durability = 0;
        it->count = 1;
    }
    if (this->block->metadata < 8) return 1;
	return 0;
}

static void onRightClick(struct server_local* s, struct item_data* it,
                         struct block_info* where, struct block_info* on,
                         enum side on_side) {
    uint64_t t = s->world_time % 24000;

    // If it's night (after 13000), skip to morning (or the next day)
    if (t >= 13000) {
        s->world_time += (24000 - t); // Next day
    } else {
        s->world_time += (13000 - t); // Skip to morning
    }

    // Send the updated world time to the client
    clin_rpc_send(&(struct client_rpc) {
        .type = CRPC_TIME_SET,
        .payload.time_set = s->world_time
    });

    // Todo: Trigger any other effects (like a message on screen or so)

}


struct block block_bed = {
	.name = "Bed",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_bed,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 1,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 300,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
