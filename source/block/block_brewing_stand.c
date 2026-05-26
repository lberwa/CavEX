/*
	Copyright (c) 2026
*/

#include "../network/client_interface.h"
#include "../network/inventory_logic.h"
#include "../network/server_local.h"
#include <stdlib.h>
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)this;
	// No collision, but keep a full selection box for easy targeting.
	if(entity)
		return 0;
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
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
	return tex_atlas_lookup(TEXAT_BREWING_STAND);
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	(void)it;
	(void)where;
	(void)on_side;
	const uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	if(player->active_inventory == &player->inventory) {
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(pid)
			.type = CRPC_OPEN_WINDOW,
			.payload.window_open.window = WINDOWC_BREWING_STAND,
			.payload.window_open.type = WINDOW_TYPE_BREWING_STAND,
			.payload.window_open.slot_count = BREWING_STAND_SIZE,
		});

		struct inventory* inv = malloc(sizeof(struct inventory));
		inventory_create(inv, &inventory_logic_brewing_stand, s,
						 BREWING_STAND_SIZE, on->x, on->y, on->z);
		player->active_inventory = inv;
	}
}

struct block block_brewing_stand = {
	.name = "Brewing Stand",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_brewing_stand,
	.renderBlockAlways = NULL,
	.luminance = 1,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 750,
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
