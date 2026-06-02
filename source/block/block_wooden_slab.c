/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "blocks.h"

enum block_material wooden_slab_get_material(struct block_info* this) {
	(void)this;
	return MATERIAL_WOOD;
}

size_t wooden_slab_get_bounding_box(struct block_info* this, bool entity,
									struct AABB* x) {
	(void)entity;
	if(x) {
		aabb_setsize(x, 1.0F, 0.5F, 1.0F);
		if(this->block->metadata & 0x08)
			aabb_translate(x, 0.0F, 0.5F, 0.0F);
	}
	return 1;
}

struct face_occlusion* wooden_slab_get_side_mask(struct block_info* this,
												 enum side side,
												 struct block_info* it) {
	const bool top_half = (this->block->metadata & 0x08) != 0;

	(void)it;
	switch(side) {
		case SIDE_TOP: return top_half ? face_occlusion_full() : face_occlusion_empty();
		case SIDE_BOTTOM:
			return top_half ? face_occlusion_empty() : face_occlusion_full();
		default: return face_occlusion_rect(8);
	}
}

uint16_t wooden_slab_get_texture_index(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_PLANKS);
}

static bool wooden_slab_place_double(struct server_local* s, struct block_info* pos,
									 uint8_t variant) {
	struct block_data blk = (struct block_data) {
		.type = BLOCK_DOUBLE_WOODEN_SLAB,
		.metadata = variant & 0x07,
		.sky_light = 0,
		.torch_light = 0,
	};
	struct block_info blk_info = *pos;

	blk_info.block = &blk;
	if(entity_local_player_block_collide(
		   (vec3) {s->players[s->active_player_id].x,
				   s->players[s->active_player_id].y,
				   s->players[s->active_player_id].z},
		   &blk_info))
		return false;

	server_world_set_block(s, pos->x, pos->y, pos->z, blk);
	return true;
}

bool wooden_slab_on_item_place(struct server_local* s, struct item_data* it,
							   struct block_info* where, struct block_info* on,
							   enum side on_side) {
	const uint8_t variant = it->durability & 0x07;

	if(on->block->type == BLOCK_WOODEN_SLAB
	   && (on->block->metadata & 0x07) == variant) {
		const bool on_top_half = (on->block->metadata & 0x08) != 0;

		if((on_side == SIDE_TOP && !on_top_half)
		   || (on_side == SIDE_BOTTOM && on_top_half))
			return wooden_slab_place_double(s, on, variant);
	}

	if(where->block->type == BLOCK_AIR) {
		uint8_t metadata = variant;
		struct block_data blk;
		struct block_info blk_info = *where;

		if(on_side == SIDE_BOTTOM)
			metadata |= 0x08;

		blk = (struct block_data) {
			.type = BLOCK_WOODEN_SLAB,
			.metadata = metadata,
			.sky_light = 0,
			.torch_light = 0,
		};
		blk_info.block = &blk;

		if(entity_local_player_block_collide(
			   (vec3) {s->players[s->active_player_id].x,
					   s->players[s->active_player_id].y,
					   s->players[s->active_player_id].z},
			   &blk_info))
			return false;

		server_world_set_block(s, where->x, where->y, where->z, blk);
		return true;
	}

	return false;
}

size_t wooden_slab_get_dropped_item(struct block_info* this, struct item_data* it,
									struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = BLOCK_WOODEN_SLAB;
		it->durability = this->block->metadata & 0x07;
		it->count = 1;
	}
	return 1;
}

struct block block_wooden_slab = {
	.name = "Wooden Slab",
	.getSideMask = wooden_slab_get_side_mask,
	.getBoundingBox = wooden_slab_get_bounding_box,
	.getMaterial = wooden_slab_get_material,
	.getTextureIndex = wooden_slab_get_texture_index,
	.getDroppedItem = wooden_slab_get_dropped_item,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_slab,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 15,
	.ignore_lighting = true,
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
