/*
	Copyright (c) 2026
*/

#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* out) {
	(void)this;
	(void)entity;

	// Shape: inset outer walls + bottom slab, leaving a hollow interior.
	// Dimensions (in blocks):
	// - outer inset: 1/16
	// - wall thickness: 2/16
	// - bottom thickness: 2/16
	const float o0 = 1.0F / 16.0F;
	const float o1 = 15.0F / 16.0F;
	const float t = 2.0F / 16.0F;
	const float b = 2.0F / 16.0F;

	if(!out)
		return 5;

	// bottom slab
	out[0] = (struct AABB) {o0, 0.0F, o0, o1, b, o1};
	// west wall
	out[1] = (struct AABB) {o0, 0.0F, o0, o0 + t, 1.0F, o1};
	// east wall
	out[2] = (struct AABB) {o1 - t, 0.0F, o0, o1, 1.0F, o1};
	// north wall (front)
	out[3] = (struct AABB) {o0, 0.0F, o0, o1, 1.0F, o0 + t};
	// south wall (back)
	out[4] = (struct AABB) {o0, 0.0F, o1 - t, o1, 1.0F, o1};

	return 5;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	// Do not occlude neighbor faces (so blocks behind are still rendered).
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	switch(side) {
		case SIDE_TOP: return tex_atlas_lookup(TEXAT_CAULDRON_TOP);
		case SIDE_BOTTOM: return tex_atlas_lookup(TEXAT_CAULDRON_BOTTOM);
		default: return tex_atlas_lookup(TEXAT_CAULDRON_SIDE);
	}
}

struct block block_cauldron = {
	.name = "Cauldron",
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
	.renderBlock = render_block_cauldron,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
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

