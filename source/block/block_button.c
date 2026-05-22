/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "blocks.h"

#define BUTTON_PRESSED 0x08
#define BUTTON_TICKS 20

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static uint8_t button_facing(const struct block_data* blk) {
	return blk->metadata & 0x07;
}

static bool button_pressed(const struct block_data* blk) {
	return (blk->metadata & BUTTON_PRESSED) != 0;
}

static struct button_state* button_state_get(struct server_local* s,
											 w_coord_t x, w_coord_t y,
											 w_coord_t z, bool create) {
	struct button_state* free_slot = NULL;

	for(size_t i = 0; i < MAX_BUTTONS; i++) {
		struct button_state* st = &s->buttons[i];
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
	return free_slot;
}

static void button_state_clear(struct server_local* s, w_coord_t x, w_coord_t y,
							   w_coord_t z) {
	struct button_state* st = button_state_get(s, x, y, z, false);
	if(!st)
		return;

	st->pos.x = -1;
	st->pos.y = -1;
	st->pos.z = -1;
	st->timer = 0;
}

static bool button_support_pos(struct block_info* this, w_coord_t* sx,
							   w_coord_t* sy, w_coord_t* sz) {
	*sx = this->x;
	*sy = this->y;
	*sz = this->z;

	switch(button_facing(this->block)) {
		case 1: (*sx)--; return true;
		case 2: (*sx)++; return true;
		case 3: (*sz)--; return true;
		case 4: (*sz)++; return true;
		case 5: (*sy)--; return true;
		case 6: (*sy)++; return true;
		default: return false;
	}
}

static bool button_has_support(struct server_local* s, struct block_info* this) {
	struct block_data support;
	w_coord_t sx, sy, sz;

	if(!button_support_pos(this, &sx, &sy, &sz))
		return false;
	if(!server_world_get_block(&s->world, sx, sy, sz, &support))
		return false;
	return blocks[support.type] && !blocks[support.type]->can_see_through;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	float depth = button_pressed(this->block) ? 0.0625F : 0.125F;
	float half_w = 0.1875F;
	float half_h = 0.125F;

	if(x) {
		switch(button_facing(this->block)) {
			case 1:
				aabb_setsize_centered_offset(x, depth, half_h * 2.0F, half_w * 2.0F,
											 depth * 0.5F, 0.5F, 0.5F);
				break;
			case 2:
				aabb_setsize_centered_offset(x, depth, half_h * 2.0F, half_w * 2.0F,
											 1.0F - depth * 0.5F, 0.5F, 0.5F);
				break;
			case 3:
				aabb_setsize_centered_offset(x, half_w * 2.0F, half_h * 2.0F, depth,
											 0.5F, 0.5F, depth * 0.5F);
				break;
			case 4:
				aabb_setsize_centered_offset(x, half_w * 2.0F, half_h * 2.0F, depth,
											 0.5F, 0.5F, 1.0F - depth * 0.5F);
				break;
			case 5:
				aabb_setsize_centered_offset(x, half_w * 2.0F, depth, half_w * 2.0F,
											 0.5F, depth * 0.5F, 0.5F);
				break;
			default:
				aabb_setsize_centered_offset(x, half_w * 2.0F, depth, half_w * 2.0F,
											 0.5F, 1.0F - depth * 0.5F, 0.5F);
				break;
		}
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
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_STONE_BUTTON);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	uint8_t facing;
	struct block_data blk;
	struct block_info blk_info = *where;

	if(!blocks[on->block->type] || blocks[on->block->type]->can_see_through)
		return false;

	switch(on_side) {
		case SIDE_RIGHT: facing = 1; break;
		case SIDE_LEFT: facing = 2; break;
		case SIDE_BACK: facing = 3; break;
		case SIDE_FRONT: facing = 4; break;
		case SIDE_TOP: facing = 5; break;
		case SIDE_BOTTOM: facing = 6; break;
		default: return false;
	}

	blk = (struct block_data) {
		.type = it->id,
		.metadata = facing,
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
	struct button_state* st;
	(void)it;
	(void)where;
	(void)on_side;

	if(button_pressed(&cur))
		return;

	cur.metadata |= BUTTON_PRESSED;
	server_world_set_block(s, on->x, on->y, on->z, cur);
	notifyNeighbours(s, on->x, on->y, on->z);

	st = button_state_get(s, on->x, on->y, on->z, true);
	if(st)
		st->timer = BUTTON_TICKS;
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	struct button_state* st;
	struct block_data cur = *info->block;

	if(!button_pressed(&cur))
		return;

	st = button_state_get(s, info->x, info->y, info->z, true);
	if(!st || st->timer == 0) {
		cur.metadata &= ~BUTTON_PRESSED;
		server_world_set_block(s, info->x, info->y, info->z, cur);
		notifyNeighbours(s, info->x, info->y, info->z);
		button_state_clear(s, info->x, info->y, info->z);
		return;
	}

	st->timer--;
	if(st->timer == 0) {
		cur.metadata &= ~BUTTON_PRESSED;
		server_world_set_block(s, info->x, info->y, info->z, cur);
		notifyNeighbours(s, info->x, info->y, info->z);
		button_state_clear(s, info->x, info->y, info->z);
	}
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	if(button_has_support(s, info))
		return;

	server_world_set_block(s, info->x, info->y, info->z,
						   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
	server_local_spawn_block_drops(s, info);
	button_state_clear(s, info->x, info->y, info->z);
}

struct block block_stone_button = {
	.name = "Stone Button",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_button,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 250,
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
		.render_data.block.default_metadata = 1,
		.render_data.block.default_rotation = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
