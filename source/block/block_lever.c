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

#include "../network/server_local.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static uint8_t lever_attachment(const struct block_data* blk) {
	return blk->metadata & 0x07;
}

static bool lever_is_on(const struct block_data* blk) {
	return (blk->metadata & 0x08) != 0;
}

static bool lever_support_pos(struct block_info* this, w_coord_t* sx, w_coord_t* sy,
							  w_coord_t* sz) {
	*sx = this->x;
	*sy = this->y;
	*sz = this->z;

	switch(lever_attachment(this->block)) {
		case 1: (*sx)--; return true;
		case 2: (*sx)++; return true;
		case 3: (*sz)--; return true;
		case 4: (*sz)++; return true;
		case 5:
		case 6: (*sy)--; return true;
		case 7: (*sy)++; return true;
		default: return false;
	}
}

static bool lever_has_support(struct server_local* s, struct block_info* this) {
	w_coord_t sx, sy, sz;
	struct block_data support;

	if(!lever_support_pos(this, &sx, &sy, &sz))
		return false;
	if(!server_world_get_block(&s->world, sx, sy, sz, &support))
		return false;
	return blocks[support.type] && !blocks[support.type]->can_see_through;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x) {
		switch(lever_attachment(this->block)) {
			case 7:
				aabb_setsize(x, 0.5F, 0.875F, 0.5F);
				aabb_translate(x, 0.0F, 0.0625F, 0.0F);
				break;
			case 1:
				aabb_setsize(x, 0.5F, 0.75F, 0.5F);
				aabb_translate(x, -0.25F, 0.125F, 0.0F);
				break;
			case 2:
				aabb_setsize(x, 0.5F, 0.75F, 0.5F);
				aabb_translate(x, 0.25F, 0.125F, 0.0F);
				break;
			case 3:
				aabb_setsize(x, 0.5F, 0.75F, 0.5F);
				aabb_translate(x, 0.0F, 0.125F, -0.25F);
				break;
			case 4:
				aabb_setsize(x, 0.5F, 0.75F, 0.5F);
				aabb_translate(x, 0.0F, 0.125F, 0.25F);
				break;
			default:
				aabb_setsize(x, 0.5F, 0.75F, 0.5F);
				aabb_translate(x, 0.0F, -0.125F, 0.0F);
				break;
		}
	}

	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_LEVER);
}

static uint8_t lever_top_orientation(struct server_local* s, struct block_info* where) {
	double dx = s->players[s->active_player_id].x - (where->x + 0.5);
	double dz = s->players[s->active_player_id].z - (where->z + 0.5);
	return fabs(dx) > fabs(dz) ? 5 : 6;
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	if(!blocks[on->block->type] || blocks[on->block->type]->can_see_through)
		return false;

	uint8_t metadata = 0;
	switch(on_side) {
		case SIDE_RIGHT: metadata = 1; break;
		case SIDE_LEFT: metadata = 2; break;
		case SIDE_BACK: metadata = 3; break;
		case SIDE_FRONT: metadata = 4; break;
		case SIDE_TOP: metadata = lever_top_orientation(s, where); break;
		case SIDE_BOTTOM: metadata = 7; break;
		default: return false;
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
		   (vec3) {s->players[s->active_player_id].x,
				   s->players[s->active_player_id].y,
				   s->players[s->active_player_id].z},
		   &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	notifyNeighbours(s, where->x, where->y, where->z);
	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	struct block_data cur = *on->block;
	cur.metadata ^= 0x08;
	server_world_set_block(s, on->x, on->y, on->z, cur);
	notifyNeighbours(s, on->x, on->y, on->z);
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	if(lever_has_support(s, info))
		return;

	server_world_set_block(s, info->x, info->y, info->z,
						   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
	server_local_spawn_block_drops(s, info);
}

struct block block_lever = {
	.name = "Lever",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.transparent = false,
	.renderBlock = render_block_lever,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 250,
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
		.render_data.block.default_metadata = 1,
		.render_data.block.default_rotation = 1,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
