/*
	Copyright (c) 2026
*/

#include "blocks.h"

enum block_material wooden_slab_get_material(struct block_info* this);
uint16_t wooden_slab_get_texture_index(struct block_info* this, enum side side);
bool wooden_slab_on_item_place(struct server_local* s, struct item_data* it,
							   struct block_info* where, struct block_info* on,
							   enum side on_side);

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

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = BLOCK_WOODEN_SLAB;
		it->durability = this->block->metadata & 0x07;
		it->count = 2;
	}
	return 1;
}

struct block block_double_wooden_slab = {
	.name = "Double Wooden Slab",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = wooden_slab_get_material,
	.getTextureIndex = wooden_slab_get_texture_index,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.opacity = 15,
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
		.onItemPlace = wooden_slab_on_item_place,
		.fuel = 1,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
