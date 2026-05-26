/*
	Copyright (c) 2026
*/

#include <math.h>

#include "../network/server_local.h"
#include "../network/server_world.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	// Same geometry as the built-in stairs.
	if(entity) {
		if(x) {
			aabb_setsize(x + 0, 1.0F, 0.5F, 1.0F);

			enum side facing
				= (enum side[4]) {SIDE_RIGHT, SIDE_LEFT, SIDE_BACK,
								  SIDE_FRONT}[this->block->metadata & 3];

			switch(facing) {
				default:
				case SIDE_FRONT:
					aabb_setsize(x + 1, 1.0F, 0.5F, 0.5F);
					aabb_translate(x + 1, 0, 0.5F, -0.25F);
					break;
				case SIDE_BACK:
					aabb_setsize(x + 1, 1.0F, 0.5F, 0.5F);
					aabb_translate(x + 1, 0, 0.5F, 0.25F);
					break;
				case SIDE_RIGHT:
					aabb_setsize(x + 1, 0.5F, 0.5F, 1.0F);
					aabb_translate(x + 1, 0.25F, 0.5F, 0);
					break;
				case SIDE_LEFT:
					aabb_setsize(x + 1, 0.5F, 0.5F, 1.0F);
					aabb_translate(x + 1, -0.25F, 0.5F, 0);
					break;
			}
		}

		return 2;
	} else {
		if(x)
			aabb_setsize(x, 1.0F, 1.0F, 1.0F);
		return 1;
	}
}

static struct face_occlusion side_mask = {
	.mask = {0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFFFFFFFF,
			 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
};

static struct face_occlusion side_mask_mirrored = {
	.mask = {0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0xFF00FF00, 0xFFFFFFFF,
			 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
};

static struct face_occlusion side_top_mask_1 = {
	.mask = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
			 0x00000000, 0x00000000, 0x00000000},
};

static struct face_occlusion side_top_mask_2 = {
	.mask = {0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF,
			 0x00FF00FF, 0x00FF00FF, 0x00FF00FF},
};

static struct face_occlusion side_top_mask_3 = {
	.mask = {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF,
			 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
};

static struct face_occlusion side_top_mask_4 = {
	.mask = {0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
			 0xFF00FF00, 0xFF00FF00, 0xFF00FF00},
};

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)it;
	enum side facing = (enum side[4]) {SIDE_RIGHT, SIDE_LEFT, SIDE_BACK,
									   SIDE_FRONT}[this->block->metadata & 3];

	if(side == facing || side == SIDE_BOTTOM) {
		return face_occlusion_full();
	} else if(side == blocks_side_opposite(facing)) {
		return face_occlusion_rect(8);
	}

	if(side == SIDE_TOP) {
		switch(facing) {
			case SIDE_FRONT: return &side_top_mask_1;
			case SIDE_BACK: return &side_top_mask_3;
			case SIDE_RIGHT: return &side_top_mask_2;
			case SIDE_LEFT: return &side_top_mask_4;
			default: return face_occlusion_empty();
		}
	} else {
		switch(facing) {
			case SIDE_FRONT: return &side_mask;
			case SIDE_BACK: return &side_mask_mirrored;
			case SIDE_RIGHT: return &side_mask;
			case SIDE_LEFT: return &side_mask_mirrored;
			default: return face_occlusion_empty();
		}
	}
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_NETHER_BRICK);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)on;
	(void)on_side;

	int metadata = 0;
	const uint8_t pid = s->active_player_id;
	const double dx = s->players[pid].x - (where->x + 0.5);
	const double dz = s->players[pid].z - (where->z + 0.5);

	if(fabs(dx) > fabs(dz)) {
		metadata = (dx >= 0) ? 1 : 0;
	} else {
		metadata = (dz >= 0) ? 3 : 2;
	}

	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = metadata,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
	blk_info.block = &blk;

	if(entity_local_player_block_collide(
		   (vec3) {s->players[pid].x, s->players[pid].y, s->players[pid].z},
		   &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;

	// Beta-style: drop crafting material, not the stair.
	if(it) {
		it->id = BLOCK_NETHER_BRICK;
		it->durability = 0;
		it->count = 1;
	}
	return 1;
}

struct block block_nether_brick_stairs = {
	.name = "Nether Brick Stairs",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_stairs,
	.renderBlockAlways = render_block_stairs_always,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 15,
	.ignore_lighting = true,
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
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 2,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
