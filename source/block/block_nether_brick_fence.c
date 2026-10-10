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
	const float h = entity ? 1.5F : 1.0F;
	const bool connect_pos_x = this->neighbours
		&& (this->neighbours[SIDE_RIGHT].type == BLOCK_FENCE
			|| this->neighbours[SIDE_RIGHT].type == BLOCK_NETHER_BRICK_FENCE
			|| this->neighbours[SIDE_RIGHT].type == BLOCK_FENCE_GATE);
	const bool connect_neg_x = this->neighbours
		&& (this->neighbours[SIDE_LEFT].type == BLOCK_FENCE
			|| this->neighbours[SIDE_LEFT].type == BLOCK_NETHER_BRICK_FENCE
			|| this->neighbours[SIDE_LEFT].type == BLOCK_FENCE_GATE);
	const bool connect_pos_z = this->neighbours
		&& (this->neighbours[SIDE_BACK].type == BLOCK_FENCE
			|| this->neighbours[SIDE_BACK].type == BLOCK_NETHER_BRICK_FENCE
			|| this->neighbours[SIDE_BACK].type == BLOCK_FENCE_GATE);
	const bool connect_neg_z = this->neighbours
		&& (this->neighbours[SIDE_FRONT].type == BLOCK_FENCE
			|| this->neighbours[SIDE_FRONT].type == BLOCK_NETHER_BRICK_FENCE
			|| this->neighbours[SIDE_FRONT].type == BLOCK_FENCE_GATE);
	size_t count = 1;

	if(x) {
		x[0] = (struct AABB) {0.375F, 0.0F, 0.375F, 0.625F, h, 0.625F};
		if(connect_pos_x)
			x[count++] = (struct AABB) {0.625F, 0.0F, 0.375F, 1.0F, h, 0.625F};
		if(connect_neg_x)
			x[count++] = (struct AABB) {0.0F, 0.0F, 0.375F, 0.375F, h, 0.625F};
		if(connect_pos_z)
			x[count++] = (struct AABB) {0.375F, 0.0F, 0.625F, 0.625F, h, 1.0F};
		if(connect_neg_z)
			x[count++] = (struct AABB) {0.375F, 0.0F, 0.0F, 0.625F, h, 0.375F};
	} else {
		count += connect_pos_x + connect_neg_x + connect_pos_z + connect_neg_z;
	}

	return count;
}

static struct face_occlusion side_top_bottom = {
	.mask = {0x00000000, 0x00000000, 0x00000000, 0x03C003C0, 0x03C003C0,
			 0x00000000, 0x00000000, 0x00000000},
};

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)it;
	switch(side) {
		case SIDE_TOP:
		case SIDE_BOTTOM: return &side_top_bottom;
		default: return face_occlusion_empty();
	}
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_NETHER_BRICK);
}

struct block block_nether_brick_fence = {
	.name = "Nether Brick Fence",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_fence,
	.renderBlockAlways = render_block_fence_always,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
		.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 3000,
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
