/*
	Copyright (c) 2026
*/

#include <stdlib.h>

#include "../network/server_local.h"
#include "../network/server_world.h"
#include "blocks.h"

#define MELON_STEM_STAGE_MAX 7
#define MELON_STEM_ATTACHED_LEFT 8
#define MELON_STEM_ATTACHED_RIGHT 9
#define MELON_STEM_ATTACHED_FRONT 10
#define MELON_STEM_ATTACHED_BACK 11

static bool is_melon_ground(uint8_t type) {
	return type == BLOCK_DIRT || type == BLOCK_GRASS || type == BLOCK_FARMLAND;
}

static bool melon_stem_is_attached(uint8_t metadata) {
	return metadata >= MELON_STEM_ATTACHED_LEFT
		&& metadata <= MELON_STEM_ATTACHED_BACK;
}

static enum side melon_stem_side_from_metadata(uint8_t metadata) {
	switch(metadata) {
		case MELON_STEM_ATTACHED_LEFT: return SIDE_LEFT;
		case MELON_STEM_ATTACHED_RIGHT: return SIDE_RIGHT;
		case MELON_STEM_ATTACHED_FRONT: return SIDE_FRONT;
		case MELON_STEM_ATTACHED_BACK: return SIDE_BACK;
		default: return SIDE_MAX;
	}
}

static uint8_t melon_stem_metadata_from_side(enum side side) {
	switch(side) {
		case SIDE_LEFT: return MELON_STEM_ATTACHED_LEFT;
		case SIDE_RIGHT: return MELON_STEM_ATTACHED_RIGHT;
		case SIDE_FRONT: return MELON_STEM_ATTACHED_FRONT;
		case SIDE_BACK: return MELON_STEM_ATTACHED_BACK;
		default: return MELON_STEM_STAGE_MAX;
	}
}

static bool stem_has_support(struct server_local* s, struct block_info* this) {
	struct block_data below;
	if(!server_world_get_block(&s->world, this->x, this->y - 1, this->z, &below))
		return false;
	return below.type == BLOCK_FARMLAND;
}

static bool stem_can_spawn_melon_here(struct server_local* s, w_coord_t x,
									   w_coord_t y, w_coord_t z) {
	struct block_data target;
	struct block_data below;

	if(!server_world_get_block(&s->world, x, y, z, &target)
	   || !server_world_get_block(&s->world, x, y - 1, z, &below))
		return false;

	return target.type == BLOCK_AIR && is_melon_ground(below.type);
}

static bool stem_has_stored_melon(struct server_local* s, struct block_info* this) {
	struct block_data blk;
	int dx = 0;
	int dz = 0;

	if(!melon_stem_is_attached(this->block->metadata))
		return false;

	switch(melon_stem_side_from_metadata(this->block->metadata)) {
		case SIDE_LEFT: dx = -1; break;
		case SIDE_RIGHT: dx = 1; break;
		case SIDE_FRONT: dz = -1; break;
		case SIDE_BACK: dz = 1; break;
		default: return false;
	}

	return server_world_get_block(&s->world, this->x + dx, this->y, this->z + dz, &blk)
		&& blk.type == BLOCK_MELON;
}

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_ORGANIC;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	const float height = melon_stem_is_attached(this->block->metadata) ? 0.75F
		: (float)(this->block->metadata + 1) / 8.0F;

	if(x)
		*x = (struct AABB) {0.375F, 0.0F, 0.375F, 0.625F, height, 0.625F};

	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)side;
	const uint8_t age = this->block->metadata > MELON_STEM_STAGE_MAX
		? MELON_STEM_STAGE_MAX
		: this->block->metadata;
	return tex_atlas_lookup(TEXAT_MELON_STEM_0 + age);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	const uint8_t count = rand_gen_range(g, 0, 3);

	if(it) {
		it->id = ITEM_MELON_SEEDS;
		it->durability = 0;
		it->count = count;
	}

	(void)this;
	(void)s;
	return count > 0 ? 1 : 0;
}

static void onRandomTick(struct server_local* s, struct block_info* this) {
	static const int off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	static const enum side sides[4] = {
		SIDE_LEFT, SIDE_RIGHT, SIDE_FRONT, SIDE_BACK
	};
	struct block_data above;
	struct block_data stem_block = *this->block;

	if(this->block->sky_light < 9 && this->block->torch_light < 9)
		return;
	if(!stem_has_support(s, this)) {
		server_world_set_block(s, this->x, this->y, this->z,
							   (struct block_data) {.type = BLOCK_AIR});
		return;
	}
	if(!server_world_get_block(&s->world, this->x, this->y + 1, this->z, &above))
		return;
	if(above.type != BLOCK_AIR
	   && (!blocks[above.type] || !blocks[above.type]->can_see_through))
		return;

	if(melon_stem_is_attached(stem_block.metadata)) {
		if(!stem_has_stored_melon(s, this)) {
			stem_block.metadata = MELON_STEM_STAGE_MAX;
			server_world_set_block(s, this->x, this->y, this->z, stem_block);
		}
		return;
	}

	if(stem_block.metadata < MELON_STEM_STAGE_MAX) {
		stem_block.metadata++;
		server_world_set_block(s, this->x, this->y, this->z, stem_block);
		return;
	}

	const int choice = rand() % 4;
	const w_coord_t tx = this->x + off[choice][0];
	const w_coord_t tz = this->z + off[choice][1];
	if(!stem_can_spawn_melon_here(s, tx, this->y, tz))
		return;

	server_world_set_block(s, tx, this->y, tz,
						   (struct block_data) {.type = BLOCK_MELON});
	stem_block.metadata = melon_stem_metadata_from_side(sides[choice]);
	server_world_set_block(s, this->x, this->y, this->z, stem_block);
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	struct block_data stem_block;

	if(stem_has_support(s, info))
	{
		if(!server_world_get_block(&s->world, info->x, info->y, info->z, &stem_block))
			return;

		if(melon_stem_is_attached(stem_block.metadata)) {
			if(!stem_has_stored_melon(s, info)) {
				stem_block.metadata = MELON_STEM_STAGE_MAX;
				server_world_set_block(s, info->x, info->y, info->z, stem_block);
			}
		}
		return;
	}

	server_world_set_block(s, info->x, info->y, info->z,
						   (struct block_data) {.type = BLOCK_AIR});
}

struct block block_melon_stem = {
	.name = "Melon Stem",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = onRandomTick,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_melon_stem,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 0,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
