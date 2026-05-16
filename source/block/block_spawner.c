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

#include "../entity/entity_monster.h"
#include "../network/server_local.h"
#include "blocks.h"

enum spawner_type {
	SPAWNER_TYPE_NONE = 0,
	SPAWNER_TYPE_CREEPER = 1,
	SPAWNER_TYPE_PIG = 2,
	SPAWNER_TYPE_SHEEP = 3,
};

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
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_SPAWNER);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	return 0;
}

static uint8_t item_to_spawner_type(uint16_t item_id) {
	switch(item_id) {
		case ITEM_EGG_CREEPER: return SPAWNER_TYPE_CREEPER;
		case ITEM_EGG_PIG: return SPAWNER_TYPE_PIG;
		case ITEM_EGG_SHEEP: return SPAWNER_TYPE_SHEEP;
		default: return SPAWNER_TYPE_NONE;
	}
}

static int spawner_type_to_monster_id(uint8_t spawner_type) {
	switch(spawner_type) {
		case SPAWNER_TYPE_CREEPER: return MONSTER_CREEPER;
		case SPAWNER_TYPE_PIG: return ANIMAIL_PIG;
		case SPAWNER_TYPE_SHEEP: return ANIMAIL_SHEEP;
		default: return -1;
	}
}

static bool spawner_requires_darkness(uint8_t spawner_type) {
	return spawner_type == SPAWNER_TYPE_CREEPER;
}

static bool spawner_can_spawn_here(struct server_local* s, w_coord_t x,
								   w_coord_t y, w_coord_t z) {
	struct block_data body;
	struct block_data below;

	if(!server_world_get_block(&s->world, x, y, z, &body)
	   || !server_world_get_block(&s->world, x, y - 1, z, &below))
		return false;

	if(body.type != BLOCK_AIR)
		return false;

	if(!blocks[below.type] || blocks[below.type]->can_see_through)
		return false;

	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* this, struct block_info* on,
						 enum side on_side) {
	if(!on) {
		place_block = true;
		return;
	}

	if(!it || on->block->metadata != SPAWNER_TYPE_NONE) {
		place_block = true;
		return;
	}

	uint8_t spawner_type = item_to_spawner_type(it->id);
	if(spawner_type == SPAWNER_TYPE_NONE) {
		place_block = true;
		return;
	}

	server_world_set_block(s, on->x, on->y, on->z,
						   (struct block_data) {
							   .type = on->block->type,
							   .metadata = spawner_type,
						   });
}

static void onRandomTick(struct server_local* s, struct block_info* this) {
	uint8_t spawner_type = this->block->metadata & 0xF;
	if(spawner_type == SPAWNER_TYPE_NONE)
		return;

	if(spawner_requires_darkness(spawner_type)
	   && (this->block->sky_light >= 8 || this->block->torch_light >= 8))
		return;

	int monster_id = spawner_type_to_monster_id(spawner_type);
	if(monster_id < 0)
		return;

	static const int offsets[4][2] = {
		{1, 0},
		{-1, 0},
		{0, 1},
		{0, -1},
	};

	int start = rand_gen_range(&s->rand_src, 0, 4);
	for(int i = 0; i < 4; i++) {
		int idx = (start + i) % 4;
		w_coord_t x = this->x + offsets[idx][0];
		w_coord_t y = this->y;
		w_coord_t z = this->z + offsets[idx][1];

		if(!spawner_can_spawn_here(s, x, y, z))
			continue;

		server_local_spawn_monster((vec3) {x, y, z}, monster_id, s);
		return;
	}
}

struct block block_spawner = {
	.name = "Mob Spawner",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = onRandomTick,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 1,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 7500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
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
