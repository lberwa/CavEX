/*
	Copyright (c) 2026
*/

#include "blocks.h"

static bool iron_bars_connects_to(struct block_info* this, enum side side) {
	if(!this->neighbours)
		return false;

	struct block_data neighbour = this->neighbours[side];

	if(neighbour.type == BLOCK_IRON_BARS)
		return true;
	if(!blocks[neighbour.type])
		return false;

	return !blocks[neighbour.type]->can_see_through;
}

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	bool connect_left = iron_bars_connects_to(this, SIDE_LEFT);
	bool connect_right = iron_bars_connects_to(this, SIDE_RIGHT);
	bool connect_front = iron_bars_connects_to(this, SIDE_FRONT);
	bool connect_back = iron_bars_connects_to(this, SIDE_BACK);
	float min_x = connect_left ? 0.0F : 0.4375F;
	float max_x = connect_right ? 1.0F : 0.5625F;
	float min_z = connect_front ? 0.0F : 0.4375F;
	float max_z = connect_back ? 1.0F : 0.5625F;

	if(x) {
		x[0].x1 = min_x;
		x[0].y1 = 0.0F;
		x[0].z1 = min_z;
		x[0].x2 = max_x;
		x[0].y2 = 1.0F;
		x[0].z2 = max_z;
	}

	(void)entity;
	return 1;
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
	return tex_atlas_lookup(TEXAT_IRON_BARS);
}

struct block block_iron_bars = {
	.name = "Iron Bars",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_iron_bars,
	.renderBlockAlways = render_block_iron_bars_always,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 2500,
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
