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

#include <math.h>

#define PRESSURE_PLATE_DEBUG 0

static enum block_material getMaterial1(struct block_info* this) {
	return MATERIAL_STONE;
}

static enum block_material getMaterial2(struct block_info* this) {
	return MATERIAL_WOOD;
}

#define EYE_HEIGHT 1.62F

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 0.875F, 0.0625F, 0.875F);
	return entity ? 0 : 1;
}

struct face_occlusion side_bottom = {
	.mask = {0x00007FFE, 0x7FFE7FFE, 0x7FFE7FFE, 0x7FFE7FFE, 0x7FFE7FFE,
			 0x7FFE7FFE, 0x7FFE7FFE, 0x7FFE0000},
};

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return (side == SIDE_BOTTOM) ? &side_bottom : face_occlusion_empty();
}

static uint16_t getTextureIndex1(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_STONE);
}

static uint16_t getTextureIndex2(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_PLANKS);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	struct block_data blk;
	if(!server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z,
							   &blk))
		return false;

	if(!blocks[blk.type] || blocks[blk.type]->can_see_through)
		return false;

	bool placed = block_place_default(s, it, where, on, on_side);
#if PRESSURE_PLATE_DEBUG
	printf("[PLATE_DEBUG] place type=%u pos=%d,%d,%d result=%d\n",
		   (unsigned)it->id, (int)where->x, (int)where->y,
		   (int)where->z, placed ? 1 : 0);
#endif
	return placed;
}

static bool pressure_plate_triggered_by_entities(struct server_local* s,
														 struct block_info* info) {
	struct AABB hitbox;
	dict_entity_it_t it;

	aabb_setsize_centered(&hitbox, 0.875F, 0.125F, 0.875F);
	aabb_translate(&hitbox, info->x + 0.5F, info->y + 0.0625F,
					info->z + 0.5F);

	dict_entity_it(it, s->entities);
	while(!dict_entity_end_p(it)) {
		struct entity* e = dict_entity_ref(it)->value;
		struct AABB entity_box;
		dict_entity_next(it);

		if(!e || e->type == ENTITY_LOCAL_PLAYER)
			continue;
		if(e->type == ENTITY_ITEM) {
			/* Drops have no usable block collision box here. Compare their
			 * current world cell with the wooden plate directly. */
			bool same_block = info->block->type == BLOCK_WOOD_PRESSURE_PLATE
				&& (w_coord_t)floorf(e->pos[0]) == info->x
				&& (w_coord_t)floorf(e->pos[1]) == info->y
				&& (w_coord_t)floorf(e->pos[2]) == info->z;
			if(e->pos[0] >= info->x - 2.0F
			   && e->pos[0] < info->x + 3.0F
			   && e->pos[2] >= info->z - 2.0F
			   && e->pos[2] < info->z + 3.0F) {
#if PRESSURE_PLATE_DEBUG
				printf("[PLATE_DEBUG] plate=%u at %d,%d,%d item=%u "
					   "id=%u pos=%.3f,%.3f,%.3f same=%d\n",
					   info->block->type, (int)info->x, (int)info->y,
					   (int)info->z, (unsigned)e->id,
					   (unsigned)e->data.item.item.id, e->pos[0], e->pos[1],
					   e->pos[2], same_block ? 1 : 0);
#endif
			}
			if(same_block)
				return true;
			continue;
		}
		if(!e->getBoundingBox)
			continue;
		if(e->getBoundingBox(e, &entity_box) == 0)
			continue;
		if(aabb_intersection(&hitbox, &entity_box))
			return true;
	}

	return false;
}


static void onWorldTick(struct server_local* s, struct block_info* info) {
    if (!info->neighbours) return;
	struct block_data cur = *info->block;
    bool onPlate = false;
	for(int i = 0; i < MAX_SERVER_PLAYERS; i++) {
		struct server_player* p = &s->players[i];
		if(!p->has_pos)
			continue;

		float px = p->x;
		float pz = p->z;
		float footY = p->y - EYE_HEIGHT;

		bool insideXZ = (px >= info->x && px < info->x + 1.0f)
					 && (pz >= info->z && pz < info->z + 1.0f);
		if(insideXZ && (footY >= info->y && footY <= info->y + 0.1f)) {
			onPlate = true;
			break;
		}
	}

	if(!onPlate)
		onPlate = pressure_plate_triggered_by_entities(s, info);

    uint8_t newState = onPlate ? 1 : 0;

    if ((cur.metadata & 0x01) != newState) {
#if PRESSURE_PLATE_DEBUG
		printf("[PLATE_DEBUG] state type=%u pos=%d,%d,%d on=%u\n",
			   (unsigned)cur.type, (int)info->x, (int)info->y,
			   (int)info->z, (unsigned)newState);
#endif
        cur.metadata = (cur.metadata & ~0x01) | newState;
        server_world_set_block(s,
                               info->x, info->y, info->z,
                               cur);
        notifyNeighbours(s, info->x, info->y, info->z);
    }
}

struct block block_stone_pressure_plate = {
	.name = "Pressure Plate",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial1,
	.getTextureIndex = getTextureIndex1,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onWorldTick = onWorldTick,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_pressure_plate,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 750,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_wooden_pressure_plate = {
	.name = "Pressure Plate",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial2,
	.getTextureIndex = getTextureIndex2,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onWorldTick = onWorldTick,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_pressure_plate,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 750,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
