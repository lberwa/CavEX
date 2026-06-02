/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "blocks.h"

enum block_material redstone_lamp_get_material(struct block_info* this);
size_t redstone_lamp_get_bounding_box(struct block_info* this, bool entity,
									  struct AABB* x);
struct face_occlusion* redstone_lamp_get_side_mask(struct block_info* this,
												   enum side side,
												   struct block_info* it);
uint16_t redstone_lamp_get_texture_index(struct block_info* this, enum side side);
size_t redstone_lamp_get_dropped_item(struct block_info* this,
									  struct item_data* it,
									  struct random_gen* g,
									  struct server_local* s);
bool redstone_lamp_on_item_place(struct server_local* s, struct item_data* it,
								 struct block_info* where, struct block_info* on,
								 enum side on_side);
void redstone_lamp_on_world_tick(struct server_local* s, struct block_info* info);
void redstone_lamp_on_neighbour_block_change(struct server_local* s,
											 struct block_info* info);

struct block block_lit_redstone_lamp = {
	.name = "Lit Redstone Lamp",
	.getSideMask = redstone_lamp_get_side_mask,
	.getBoundingBox = redstone_lamp_get_bounding_box,
	.getMaterial = redstone_lamp_get_material,
	.getTextureIndex = redstone_lamp_get_texture_index,
	.getDroppedItem = redstone_lamp_get_dropped_item,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = redstone_lamp_on_world_tick,
	.onNeighbourBlockChange = redstone_lamp_on_neighbour_block_change,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 15,
	.double_sided = false,
	.can_see_through = false,
	.opacity = 15,
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
		.onItemPlace = redstone_lamp_on_item_place,
		.fuel = 0,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
