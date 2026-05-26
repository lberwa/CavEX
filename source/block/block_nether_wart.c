/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "../network/server_world.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_ORGANIC;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)this;
	// Similar to crops: no collision; selection exists.
	if(x) {
		const uint8_t age = this->block->metadata & 0x03;
		const float h = (age == 0) ? 0.25F :
			(age == 1) ? 0.375F :
			(age == 2) ? 0.5F :
			0.55F;
		aabb_setsize(x, 1.0F, h, 1.0F);
	}
	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	(void)side;
	// Use 3 distinct stages as requested:
	// stage 0 -> (10,9), stage 1 -> (11,9), stage 2+ -> (12,9)
	switch(this->block->metadata & 0x03) {
		default:
		case 0: return tex_atlas_lookup(TEXAT_NETHER_WART_0);
		case 1: return tex_atlas_lookup(TEXAT_NETHER_WART_1);
		case 2: return tex_atlas_lookup(TEXAT_NETHER_WART_2);
		case 3: return tex_atlas_lookup(TEXAT_NETHER_WART_2);
	}
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)s;

	if(!it)
		return 0;

	const uint8_t age = this->block->metadata & 0x03;
	it->id = BLOCK_NETHER_WART;
	it->durability = 0;
	it->count = (age >= 2) ? (uint16_t)rand_gen_range(g, 2, 5) : 1;
	return 1;
}

static bool nether_wart_has_support(struct server_local* s,
									struct block_info* this) {
	struct block_data below;
	if(!server_world_get_block(&s->world, this->x, this->y - 1, this->z, &below))
		return false;
	return below.type == BLOCK_SOULSAND;
}

static void onNeighbourBlockChange(struct server_local* s,
								   struct block_info* info) {
	if(!nether_wart_has_support(s, info)) {
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR,
												   .metadata = 0});
		server_local_spawn_block_drops(s, info);
	}
}

static void onRandomTick(struct server_local* s, struct block_info* this) {
	// Nether wart does not depend on light; it only needs soul sand support.
	if(!nether_wart_has_support(s, this)) {
		onNeighbourBlockChange(s, this);
		return;
	}

	uint8_t age = this->block->metadata & 0x03;
	if(age >= 2)
		return;

	// Each random tick: 10% chance to grow one stage.
	if(rand_gen_range(&s->rand_src, 0, 10) == 0) {
		this->block->metadata = (this->block->metadata & ~0x03) | (age + 1);
		server_world_set_block(s, this->x, this->y, this->z, *this->block);
	}
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)it;
	(void)on;
	(void)on_side;

	// Can only be planted on soul sand (like seeds on farmland).
	struct block_data below;
	if(!server_world_get_block(&s->world, where->x, where->y - 1, where->z, &below))
		return false;
	if(below.type != BLOCK_SOULSAND)
		return false;

	// Only place into air.
	struct block_data cur;
	if(!server_world_get_block(&s->world, where->x, where->y, where->z, &cur))
		return false;
	if(cur.type != BLOCK_AIR)
		return false;

	struct block_data blk = (struct block_data) {
		.type = BLOCK_NETHER_WART,
		.metadata = 0,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
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

struct block block_nether_wart = {
	.name = "Nether Wart",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = onRandomTick,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_nether_wart,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 50,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
