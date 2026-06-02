/*
	Copyright (c) 2026
*/

#include <math.h>

#include "../network/client_interface.h"
#include "../network/inventory_logic.h"
#include "../network/server_local.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)this;
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return entity ? 1 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_full();
}

static uint8_t ender_chest_place_dir(struct server_local* s,
									 struct block_info* where) {
	double dx = s->players[s->active_player_id].x - (where->x + 0.5);
	double dz = s->players[s->active_player_id].z - (where->z + 0.5);

	if(fabs(dx) > fabs(dz))
		return dx >= 0.0 ? 1 : 3;
	return dz >= 0.0 ? 2 : 0;
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	uint8_t tex[SIDE_MAX] = {
		[SIDE_TOP] = tex_atlas_lookup(TEXAT_ENDER_CHEST_TOP),
		[SIDE_BOTTOM] = tex_atlas_lookup(TEXAT_ENDER_CHEST_TOP),
		[SIDE_LEFT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_SIDE),
		[SIDE_RIGHT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_SIDE),
		[SIDE_FRONT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_SIDE),
		[SIDE_BACK] = tex_atlas_lookup(TEXAT_ENDER_CHEST_SIDE),
	};

	switch(this->block->metadata & 0x03) {
		case 0: tex[SIDE_FRONT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_FRONT); break;
		case 1: tex[SIDE_RIGHT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_FRONT); break;
		case 2: tex[SIDE_BACK] = tex_atlas_lookup(TEXAT_ENDER_CHEST_FRONT); break;
		default: tex[SIDE_LEFT] = tex_atlas_lookup(TEXAT_ENDER_CHEST_FRONT); break;
	}

	return tex[side];
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	const uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	struct inventory* inv;

	(void)it;
	(void)where;
	(void)on_side;

	if(player->active_inventory != &player->inventory)
		return;

	clin_rpc_send(&(struct client_rpc) {
		CRPC_PLAYER_ID(pid)
		.type = CRPC_OPEN_WINDOW,
		.payload.window_open.window = WINDOWC_CHEST,
		.payload.window_open.type = WINDOW_TYPE_ENDER_CHEST,
		.payload.window_open.slot_count = CHEST_SIZE,
	});

	inv = malloc(sizeof(struct inventory));
	inventory_create(inv, &inventory_logic_ender_chest, s, CHEST_SIZE,
					 on->x, on->y, on->z);
	player->active_inventory = inv;
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = ender_chest_place_dir(s, where),
		.sky_light = 0,
		.torch_light = 0,
	};
	struct block_info blk_info = *where;

	(void)on;
	(void)on_side;

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

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = BLOCK_OBSIDIAN;
		it->durability = 0;
		it->count = 8;
	}
	return 8;
}

struct block block_ender_chest = {
	.name = "Ender Chest",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 7,
	.double_sided = false,
	.can_see_through = false,
	.opacity = 15,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 22500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_MAX,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 0,
		.render_data.block.default_rotation = 2,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
