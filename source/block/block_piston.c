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

#define PISTON_MAX_PUSH 12
#define PISTON_EXTENDED 0x8

static enum side piston_facing(const struct block_data* blk) {
	return (enum side)(blk->metadata & 0x7);
}

static bool piston_is_extended(const struct block_data* blk) {
	return (blk->metadata & PISTON_EXTENDED) != 0;
}

static void piston_front_pos(const struct block_data* blk, w_coord_t x,
							 w_coord_t y, w_coord_t z, w_coord_t* fx,
							 w_coord_t* fy, w_coord_t* fz) {
	int ox, oy, oz;
	blocks_side_offset(piston_facing(blk), &ox, &oy, &oz);
	*fx = x + ox;
	*fy = y + oy;
	*fz = z + oz;
}

static void piston_get_block(struct server_local* s, w_coord_t x, w_coord_t y,
							 w_coord_t z, struct block_data* out) {
	if(!server_world_get_block(&s->world, x, y, z, out)) {
		out->type = BLOCK_AIR;
		out->metadata = 0;
		out->sky_light = 0;
		out->torch_light = 0;
	}
}

static bool piston_is_power_source(struct block_data blk) {
	uint8_t meta = blk.metadata & 0x0F;

	return (blk.type == BLOCK_REDSTONE_WIRE && meta > 0)
		   || blk.type == BLOCK_REDSTONE_TORCH_LIT
		   || ((blk.type == BLOCK_STONE_PRESSURE_PLATE
				|| blk.type == BLOCK_WOOD_PRESSURE_PLATE)
			   && (meta & 0x01));
}

static bool piston_is_powered(struct server_local* s, struct block_info* info) {
	struct block_data tmp;

	for(int side = 0; side < SIDE_MAX; side++) {
		int ox, oy, oz;
		blocks_side_offset((enum side)side, &ox, &oy, &oz);
		piston_get_block(s, info->x + ox, info->y + oy, info->z + oz, &tmp);
		if(piston_is_power_source(tmp))
			return true;
	}

	const enum side horiz[4]
		= {SIDE_RIGHT, SIDE_LEFT, SIDE_FRONT, SIDE_BACK};

	piston_get_block(s, info->x, info->y + 1, info->z, &tmp);
	bool above_air = tmp.type == BLOCK_AIR;

	for(int i = 0; i < 4; i++) {
		int ox, oy, oz;
		blocks_side_offset(horiz[i], &ox, &oy, &oz);

		piston_get_block(s, info->x + ox, info->y, info->z + oz, &tmp);
		if(piston_is_power_source(tmp))
			return true;

		if(tmp.type == BLOCK_AIR) {
			piston_get_block(s, info->x + ox, info->y - 1, info->z + oz, &tmp);
			if(piston_is_power_source(tmp))
				return true;
		}

		if(above_air) {
			piston_get_block(s, info->x + ox, info->y + 1, info->z + oz, &tmp);
			if(piston_is_power_source(tmp))
				return true;
		}
	}

	return false;
}

static bool piston_is_pushable(struct block_data blk) {
	switch(blk.type) {
		case BLOCK_AIR:
		case BLOCK_BEDROCK:
		case BLOCK_OBSIDIAN:
		case BLOCK_PORTAL:
		case BLOCK_PISTON:
		case BLOCK_PISTON_HEAD:
			return false;
		default: return true;
	}
}

static bool piston_try_extend(struct server_local* s, struct block_info* info) {
	enum side facing = piston_facing(info->block);
	int ox, oy, oz;
	blocks_side_offset(facing, &ox, &oy, &oz);

	struct block_data chain[PISTON_MAX_PUSH];
	size_t count = 0;

	for(int step = 1; step <= PISTON_MAX_PUSH + 1; step++) {
		struct block_data blk;
		w_coord_t x = info->x + ox * step;
		w_coord_t y = info->y + oy * step;
		w_coord_t z = info->z + oz * step;

		if(!server_world_get_block(&s->world, x, y, z, &blk))
			return false;

		if(blk.type == BLOCK_AIR)
			break;

		if(count >= PISTON_MAX_PUSH || !piston_is_pushable(blk)) {
			return false;
		}

		chain[count++] = blk;
	}

	if(count == PISTON_MAX_PUSH) {
		struct block_data end_blk;
		if(server_world_get_block(&s->world, info->x + ox * (PISTON_MAX_PUSH + 1),
								  info->y + oy * (PISTON_MAX_PUSH + 1),
								  info->z + oz * (PISTON_MAX_PUSH + 1), &end_blk)
		   && end_blk.type != BLOCK_AIR)
			return false;
	}

	for(int idx = (int)count - 1; idx >= 0; idx--) {
		w_coord_t from_x = info->x + ox * (idx + 1);
		w_coord_t from_y = info->y + oy * (idx + 1);
		w_coord_t from_z = info->z + oz * (idx + 1);
		w_coord_t to_x = info->x + ox * (idx + 2);
		w_coord_t to_y = info->y + oy * (idx + 2);
		w_coord_t to_z = info->z + oz * (idx + 2);
		server_world_set_block(s, to_x, to_y, to_z, chain[idx]);
		server_world_set_block(s, from_x, from_y, from_z,
							   (struct block_data) {.type = BLOCK_AIR,
													.metadata = 0,
													.sky_light = 0,
													.torch_light = 0});
	}

	return true;
}

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)entity;
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_full();
}

static struct face_occlusion*
getHeadSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	enum side facing = piston_facing(this->block);

	if(side == blocks_side_opposite(facing))
		return tex_atlas_lookup(TEXAT_PISTON_BACK);
	if(side == facing)
		return tex_atlas_lookup(piston_is_extended(this->block) ?
									TEXAT_PISTON_FRONT_EXTENDED :
									TEXAT_PISTON_PLATE);

	return tex_atlas_lookup(TEXAT_PISTON_SIDE);
}

static bool piston_head_matches(struct block_data blk, enum side facing) {
	return blk.type == BLOCK_PISTON_HEAD && piston_facing(&blk) == facing;
}

static bool piston_base_valid(struct server_local* s, struct block_info* info) {
	struct block_data front;
	w_coord_t fx, fy, fz;
	piston_front_pos(info->block, info->x, info->y, info->z, &fx, &fy, &fz);
	piston_get_block(s, fx, fy, fz, &front);

	return !piston_is_extended(info->block)
		   || piston_head_matches(front, piston_facing(info->block));
}

static bool piston_head_valid(struct server_local* s, struct block_info* info) {
	enum side facing = piston_facing(info->block);
	int ox, oy, oz;
	blocks_side_offset(facing, &ox, &oy, &oz);

	struct block_data base;
	piston_get_block(s, info->x - ox, info->y - oy, info->z - oz, &base);

	return base.type == BLOCK_PISTON && piston_is_extended(&base)
		   && piston_facing(&base) == facing;
}

static size_t piston_head_drop(struct block_info* this, struct item_data* it,
							   struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;

	if(it) {
		it->id = BLOCK_PISTON;
		it->durability = 0;
		it->count = 1;
	}

	return 1;
}

static void piston_onNeighbourBlockChange(struct server_local* s,
										  struct block_info* info) {
	if(!piston_base_valid(s, info))
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR});
}

static void piston_head_onNeighbourBlockChange(struct server_local* s,
											   struct block_info* info) {
	if(!piston_head_valid(s, info))
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR});
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	struct server_player* player = &s->players[s->active_player_id];
	float rx = glm_rad(-player->rx);
	float ry = glm_rad(player->ry + 90.0F);
	vec3 dir = {
		sinf(rx) * sinf(ry),
		cosf(ry),
		cosf(rx) * sinf(ry),
	};

	enum side facing;
	float ax = fabsf(dir[0]);
	float ay = fabsf(dir[1]);
	float az = fabsf(dir[2]);

	if(ay > ax && ay > az)
		facing = dir[1] > 0.0F ? SIDE_BOTTOM : SIDE_TOP;
	else if(ax > az)
		facing = dir[0] > 0.0F ? SIDE_LEFT : SIDE_RIGHT;
	else
		facing = dir[2] > 0.0F ? SIDE_FRONT : SIDE_BACK;

	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = (uint8_t)facing,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
	blk_info.block = &blk;

	if(entity_local_player_block_collide(
		   (vec3) {player->x, player->y, player->z}, &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	struct block_data cur = *info->block;
	bool powered = piston_is_powered(s, info);
	bool extended = piston_is_extended(&cur);

	if(powered && !extended) {
		if(!piston_try_extend(s, info)) {
			return;
		}

		w_coord_t fx, fy, fz;
		piston_front_pos(&cur, info->x, info->y, info->z, &fx, &fy, &fz);
		cur.metadata |= PISTON_EXTENDED;
		server_world_set_block(s, info->x, info->y, info->z, cur);
		server_world_set_block(s, fx, fy, fz,
							   (struct block_data) {
								   .type = BLOCK_PISTON_HEAD,
								   .metadata = (cur.metadata & 0x7)
									   | PISTON_EXTENDED,
								   .sky_light = 0,
								   .torch_light = 0,
							   });
	} else if(powered && extended) {
		w_coord_t fx, fy, fz;
		struct block_data front;
		piston_front_pos(info->block, info->x, info->y, info->z, &fx, &fy, &fz);
		piston_get_block(s, fx, fy, fz, &front);
		if(front.type != BLOCK_PISTON_HEAD) {
			server_world_set_block(s, fx, fy, fz,
								   (struct block_data) {
									   .type = BLOCK_PISTON_HEAD,
									   .metadata = (cur.metadata & 0x7)
										   | PISTON_EXTENDED,
									   .sky_light = 0,
									   .torch_light = 0,
								   });
		}
	} else if(!powered && extended) {
		w_coord_t fx, fy, fz;
		struct block_data front;
		piston_front_pos(info->block, info->x, info->y, info->z, &fx, &fy, &fz);
		piston_get_block(s, fx, fy, fz, &front);
		cur.metadata &= ~PISTON_EXTENDED;
		server_world_set_block(s, info->x, info->y, info->z, cur);
		if(front.type == BLOCK_PISTON_HEAD) {
			server_world_set_block(s, fx, fy, fz,
								   (struct block_data) {.type = BLOCK_AIR});
		}
	}
}

struct block block_piston = {
	.name = "Piston",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = onWorldTick,
	.onNeighbourBlockChange = piston_onNeighbourBlockChange,
	.transparent = false,
	.renderBlock = render_block_piston,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.opacity = 15,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 7500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = SIDE_FRONT,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_piston_head = {
	.name = "Piston Head",
	.getSideMask = getHeadSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = piston_head_drop,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = piston_head_onNeighbourBlockChange,
	.transparent = false,
	.renderBlock = render_block_piston_head,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 15,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 7500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = SIDE_FRONT,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
