/*
	Copyright (c) 2026
*/

#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)this;
	(void)entity;
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_full();
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_ORE_EMERALD);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;

	if(it) {
		it->id = ITEM_EMERALD;
		it->durability = 0;
		it->count = 1;
	}

	return 1;
}

struct block block_emerald_ore = {
	.name = "Emerald Ore",
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
	.opacity = 15,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 4500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_IRON,
	.digging.best = TOOL_TIER_MAX,
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
