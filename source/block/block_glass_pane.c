/*
	Copyright (c) 2026
*/

#include "blocks.h"

static bool glass_pane_connects_to(struct block_info* this, enum side side) {
	if(!this->neighbours)
		return false;

	const struct block_data neighbour = this->neighbours[side];
	if(neighbour.type == BLOCK_GLASS_PANE || neighbour.type == BLOCK_GLASS
	   || neighbour.type == BLOCK_IRON_BARS)
		return true;
	if(!blocks[neighbour.type])
		return false;

	return !blocks[neighbour.type]->can_see_through;
}

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_GLASS;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	const bool connect_left = glass_pane_connects_to(this, SIDE_LEFT);
	const bool connect_right = glass_pane_connects_to(this, SIDE_RIGHT);
	const bool connect_front = glass_pane_connects_to(this, SIDE_FRONT);
	const bool connect_back = glass_pane_connects_to(this, SIDE_BACK);
	size_t count = 0;
	float x0 = connect_left ? 0.0F : 0.4375F;
	float x1 = connect_right ? 1.0F : 0.5625F;
	float z0 = connect_front ? 0.0F : 0.4375F;
	float z1 = connect_back ? 1.0F : 0.5625F;

	if(!connect_left && !connect_right && !connect_front && !connect_back) {
		if(x)
			x[0] = (struct AABB) {0.4375F, 0.0F, 0.4375F, 0.5625F, 1.0F, 0.5625F};
		return 1;
	}

	if(connect_left || connect_right) {
		if(x)
			x[count] = (struct AABB) {x0, 0.0F, 0.4375F, x1, 1.0F, 0.5625F};
		count++;
	}
	if(connect_front || connect_back) {
		if(x)
			x[count] = (struct AABB) {0.4375F, 0.0F, z0, 0.5625F, 1.0F, z1};
		count++;
	}

	if(count == 0) {
		if(x)
			x[0] = (struct AABB) {0.4375F, 0.0F, 0.4375F, 0.5625F, 1.0F, 0.5625F};
		count = 1;
	}

	(void)entity;
	return count;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_GLASS_PANE_EDGE);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = BLOCK_GLASS_PANE;
		it->durability = 0;
		it->count = 1;
	}

	(void)this;
	(void)g;
	(void)s;
	return 1;
}

struct block block_glass_pane = {
	.name = "Glass Pane",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_glass_pane,
	.renderBlockAlways = render_block_glass_pane_always,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 450,
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
