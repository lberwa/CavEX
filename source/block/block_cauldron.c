/*
	Copyright (c) 2026
*/

#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "blocks.h"

static void swap_held_item(struct server_local* s, struct item_data* held,
						   uint16_t new_id) {
	const uint8_t pid = s->active_player_id;
	struct inventory* inv = &s->players[pid].inventory;
	const size_t hotbar_rel = inventory_get_hotbar(inv);           // 0..8
	const size_t slot_abs = INVENTORY_SLOT_HOTBAR + hotbar_rel;    // 36..44

	const struct item_data new_it = {
		.id = new_id,
		.count = 1,
		.durability = 0,
	};
	inventory_set_slot(inv, slot_abs, new_it);

	set_inv_slot_t changes;
	set_inv_slot_init(changes);
	set_inv_slot_push(changes, slot_abs);
	server_local_send_inv_changes(pid, changes, inv, WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);

	*held = new_it;
}

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* out) {
	(void)this;
	(void)entity;

	// Normal full-block selection + collision box.
	if(out)
		aabb_setsize(out, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	// Behave like a normal solid block for face culling to avoid z-fighting with
	// neighbouring blocks (especially when rendering double-sided).
	return face_occlusion_full();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	switch(side) {
		case SIDE_TOP: return tex_atlas_lookup(TEXAT_CAULDRON_TOP);
		case SIDE_BOTTOM: return tex_atlas_lookup(TEXAT_CAULDRON_BOTTOM);
		default: return tex_atlas_lookup(TEXAT_CAULDRON_SIDE);
	}
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	(void)where;
	(void)on_side;

	if(!s || !it || !on || !on->block)
		return;

	// Use metadata as water level: 0..3
	uint8_t level = (uint8_t)(on->block->metadata & 0x03);

	// Water bucket -> fill to 3 and give empty bucket
	if(it->id == ITEM_BUCKET_WATER) {
		if(level < 3) {
			struct block_data nb = *on->block;
			nb.metadata = (uint8_t)((nb.metadata & 0x0C) | 3);
			server_world_set_block(s, on->x, on->y, on->z, nb);
			swap_held_item(s, it, ITEM_BUCKET);
		}
		return;
	}

	// Empty bucket -> only take water if full (level 3)
	if(it->id == ITEM_BUCKET) {
		if(level == 3) {
			struct block_data nb = *on->block;
			nb.metadata = (uint8_t)(nb.metadata & 0x0C);
			server_world_set_block(s, on->x, on->y, on->z, nb);
			swap_held_item(s, it, ITEM_BUCKET_WATER);
		}
		return;
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
	.onRightClick = onRightClick,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = render_block_cauldron_always,
	.renderBlockAlwaysTransparent = render_block_cauldron_water,
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
