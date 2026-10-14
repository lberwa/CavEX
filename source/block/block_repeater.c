/*
	Copyright (c) 2026
*/

#include <math.h>

#include "../network/server_local.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static uint8_t repeater_dir(const struct block_data* blk) {
	return blk->metadata & 0x03;
}

static uint8_t repeater_delay(const struct block_data* blk) {
	return (blk->metadata >> 2) & 0x03;
}

static enum side repeater_output_side(const struct block_data* blk) {
	switch(repeater_dir(blk)) {
		case 0: return SIDE_FRONT;
		case 1: return SIDE_RIGHT;
		case 2: return SIDE_BACK;
		default: return SIDE_LEFT;
	}
}

static enum side repeater_input_side(const struct block_data* blk) {
	return blocks_side_opposite(repeater_output_side(blk));
}

static bool repeater_is_powered(const struct block_data* blk) {
	return blk->type == BLOCK_REPEATER_ON;
}

static uint8_t repeater_delay_ticks(const struct block_data* blk) {
	return (repeater_delay(blk) + 1) * 2;
}

static struct repeater_state* repeater_state_get(struct server_local* s,
												 w_coord_t x, w_coord_t y,
												 w_coord_t z, bool create) {
	struct repeater_state* free_slot = NULL;

	for(size_t i = 0; i < MAX_REPEATERS; i++) {
		struct repeater_state* st = &s->repeaters[i];
		if(st->pos.x == x && st->pos.y == y && st->pos.z == z)
			return st;
		if(!free_slot && st->pos.x == -1)
			free_slot = st;
	}

	if(!create || !free_slot)
		return NULL;

	free_slot->pos.x = x;
	free_slot->pos.y = y;
	free_slot->pos.z = z;
	free_slot->timer = 0;
	free_slot->target_type = BLOCK_REPEATER_OFF;
	return free_slot;
}

static void repeater_state_clear(struct server_local* s, w_coord_t x, w_coord_t y,
								 w_coord_t z) {
	struct repeater_state* st = repeater_state_get(s, x, y, z, false);
	if(!st)
		return;

	st->pos.x = -1;
	st->pos.y = -1;
	st->pos.z = -1;
	st->timer = 0;
	st->target_type = BLOCK_REPEATER_OFF;
}

static bool repeater_supports(struct server_local* s, struct block_info* this) {
	struct block_data below;

	if(!server_world_get_block(AWORLD(s), this->x, this->y - 1, this->z, &below))
		return false;
	return block_can_support_attachment(below.type);
}

static bool repeater_has_input(struct server_local* s, struct block_info* this) {
	struct block_data src;
	int ox, oy, oz;
	enum side input_side = repeater_input_side(this->block);

	blocks_side_offset(input_side, &ox, &oy, &oz);
	if(!server_world_get_block(AWORLD(s), this->x + ox, this->y + oy,
							   this->z + oz, &src))
		return false;

	if(src.type == BLOCK_REDSTONE_WIRE)
		return (src.metadata & 0x0F)
			   && block_redstone_wire_points_to_side(
				   s, this->x + ox, this->y + oy, this->z + oz,
				   blocks_side_opposite(input_side));

	return block_redstone_powers_wire(&src, blocks_side_opposite(input_side));
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	(void)this;
	if(x)
		aabb_setsize(x, 1.0F, 0.125F, 1.0F);
	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_empty();
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	(void)side;
	return tex_atlas_lookup(repeater_is_powered(this->block) ?
								TEXAT_REPEATER_ON :
								TEXAT_REPEATER_OFF);
}

static size_t repeater_drop(struct block_info* this, struct item_data* it,
							struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = BLOCK_REPEATER_OFF;
		it->durability = 0;
		it->count = 1;
	}
	return 1;
}

static uint8_t repeater_place_dir(struct server_local* s, struct block_info* where) {
	double dx = s->players[s->active_player_id].x - (where->x + 0.5);
	double dz = s->players[s->active_player_id].z - (where->z + 0.5);
	uint8_t dir;

	if(fabs(dx) > fabs(dz))
		dir = dx >= 0.0 ? 1 : 3;
	else
		dir = dz >= 0.0 ? 2 : 0;

	return (dir + 2) & 0x03;
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	struct block_data below;
	struct block_data blk;
	struct block_info blk_info = *where;
	(void)on;
	(void)on_side;

	if(!server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z, &below)
	   || !block_can_support_attachment(below.type))
		return false;

	blk = (struct block_data) {
		.type = BLOCK_REPEATER_OFF,
		.metadata = repeater_place_dir(s, where),
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
	notifyNeighbours(s, where->x, where->y, where->z);
	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	struct block_data cur = *on->block;
	(void)s;
	(void)it;
	(void)where;
	(void)on_side;

	cur.metadata = (cur.metadata & 0x03) | (((repeater_delay(&cur) + 1) & 0x03) << 2);
	server_world_set_block(s, on->x, on->y, on->z, cur);
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	struct block_data cur = *info->block;
	struct repeater_state* st;
	uint8_t target_type;

	if(!repeater_supports(s, info)) {
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
		server_local_spawn_block_drops(s, info);
		repeater_state_clear(s, info->x, info->y, info->z);
		return;
	}

	target_type = repeater_has_input(s, info) ? BLOCK_REPEATER_ON : BLOCK_REPEATER_OFF;
	st = repeater_state_get(s, info->x, info->y, info->z, true);
	if(!st)
		return;

	if(cur.type == target_type) {
		st->timer = 0;
		st->target_type = target_type;
		return;
	}

	if(st->target_type != target_type || st->timer == 0) {
		st->target_type = target_type;
		st->timer = repeater_delay_ticks(&cur);
		return;
	}

	st->timer--;
	if(st->timer == 0) {
		cur.type = target_type;
		server_world_set_block(s, info->x, info->y, info->z, cur);
		notifyNeighbours(s, info->x, info->y, info->z);
	}
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	if(repeater_supports(s, info))
		return;

	server_world_set_block(s, info->x, info->y, info->z,
						   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
	server_local_spawn_block_drops(s, info);
	repeater_state_clear(s, info->x, info->y, info->z);
}

struct block block_repeater_off = {
	.name = "Redstone Repeater",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = repeater_drop,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_repeater,
	.renderBlockAlways = render_block_repeater_torches,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 0,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 0,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_repeater_on = {
	.name = "Redstone Repeater",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = repeater_drop,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_repeater,
	.renderBlockAlways = render_block_repeater_torches,
	.luminance = 7,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 0,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.render_data.block.has_default = true,
		.render_data.block.default_metadata = 0,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
