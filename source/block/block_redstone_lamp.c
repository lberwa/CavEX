/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "blocks.h"

enum block_material redstone_lamp_get_material(struct block_info* this) {
	(void)this;
	return MATERIAL_GLASS;
}

size_t redstone_lamp_get_bounding_box(struct block_info* this, bool entity,
									  struct AABB* x) {
	(void)this;
	(void)entity;
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

struct face_occlusion* redstone_lamp_get_side_mask(struct block_info* this,
												   enum side side,
												   struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_full();
}

uint8_t redstone_lamp_get_texture_index(struct block_info* this, enum side side) {
	(void)side;
	return tex_atlas_lookup(this->block->type == BLOCK_LIT_REDSTONE_LAMP ?
								TEXAT_LIT_REDSTONE_LAMP :
								TEXAT_REDSTONE_LAMP);
}

size_t redstone_lamp_get_dropped_item(struct block_info* this,
									  struct item_data* it,
									  struct random_gen* g,
									  struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = BLOCK_REDSTONE_LAMP;
		it->durability = 0;
		it->count = 1;
	}
	return 1;
}

bool redstone_lamp_on_item_place(struct server_local* s, struct item_data* it,
								 struct block_info* where, struct block_info* on,
								 enum side on_side) {
	return block_place_default(s, it, where, on, on_side);
}

static bool redstone_lamp_repeater_powers_from(enum side side, uint8_t metadata) {
	switch(metadata & 0x03) {
		case 0: return side == SIDE_FRONT;
		case 1: return side == SIDE_RIGHT;
		case 2: return side == SIDE_BACK;
		default: return side == SIDE_LEFT;
	}
}

static bool redstone_lamp_is_powered(struct server_local* s,
									 struct block_info* info) {
	for(int side = 0; side < SIDE_MAX; side++) {
		int ox, oy, oz;
		struct block_data nb;
		uint8_t m;

		blocks_side_offset((enum side) side, &ox, &oy, &oz);
		if(!server_world_get_block(&s->world, info->x + ox, info->y + oy,
								   info->z + oz, &nb))
			continue;

		m = nb.metadata & 0x0F;
		if((nb.type == BLOCK_REDSTONE_WIRE && m > 0)
		   || nb.type == BLOCK_REDSTONE_TORCH_LIT
		   || (nb.type == BLOCK_LEVER && (m & 0x08))
		   || (nb.type == BLOCK_STONE_BUTTON && (m & 0x04))
		   || (nb.type == BLOCK_TRIPWIRE_HOOK && (m & 0x08))
		   || ((nb.type == BLOCK_STONE_PRESSURE_PLATE
				|| nb.type == BLOCK_WOOD_PRESSURE_PLATE)
			   && (m & 0x01)))
			return true;

		if(nb.type == BLOCK_REPEATER_ON
		   && redstone_lamp_repeater_powers_from((enum side) side, nb.metadata))
			return true;
	}

	return false;
}

static void redstone_lamp_sync_state(struct server_local* s, struct block_info* info) {
	struct block_data cur = *info->block;
	const bool powered = redstone_lamp_is_powered(s, info);

	if(powered) {
		if(cur.type != BLOCK_LIT_REDSTONE_LAMP || cur.metadata != 0) {
			cur.type = BLOCK_LIT_REDSTONE_LAMP;
			cur.metadata = 0;
			server_world_set_block(s, info->x, info->y, info->z, cur);
		}
		return;
	}

	if(cur.type != BLOCK_LIT_REDSTONE_LAMP)
		return;

	if(cur.metadata < 3) {
		cur.metadata++;
		server_world_set_block(s, info->x, info->y, info->z, cur);
	} else {
		cur.type = BLOCK_REDSTONE_LAMP;
		cur.metadata = 0;
		server_world_set_block(s, info->x, info->y, info->z, cur);
	}
}

void redstone_lamp_on_world_tick(struct server_local* s, struct block_info* info) {
	redstone_lamp_sync_state(s, info);
}

void redstone_lamp_on_neighbour_block_change(struct server_local* s,
											 struct block_info* info) {
	redstone_lamp_sync_state(s, info);
}

struct block block_redstone_lamp = {
	.name = "Redstone Lamp",
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
	.luminance = 0,
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
