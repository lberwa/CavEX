/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "blocks.h"

#define TRIPWIRE_HOOK_ATTACHED 0x04
#define TRIPWIRE_HOOK_POWERED 0x08

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_STONE;
}

static enum side hook_side(const struct block_data* blk) {
	switch(blk->metadata & 0x03) {
		case 0: return SIDE_FRONT;
		case 1: return SIDE_RIGHT;
		case 2: return SIDE_BACK;
		default: return SIDE_LEFT;
	}
}

static bool hook_has_support(struct server_local* s, struct block_info* this) {
	int ox, oy, oz;
	struct block_data support;

	blocks_side_offset(blocks_side_opposite(hook_side(this->block)), &ox, &oy, &oz);
	if(!server_world_get_block(AWORLD(s), this->x + ox, this->y + oy,
							   this->z + oz, &support))
		return false;
	return blocks[support.type] && !blocks[support.type]->can_see_through;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	float depth = 0.375F;
	float width = 0.625F;
	float height = 0.625F;

	if(x) {
		switch(hook_side(this->block)) {
			case SIDE_FRONT:
				aabb_setsize_centered_offset(x, width, height, depth,
											 0.5F, 0.5F, depth * 0.5F);
				break;
			case SIDE_BACK:
				aabb_setsize_centered_offset(x, width, height, depth,
											 0.5F, 0.5F, 1.0F - depth * 0.5F);
				break;
			case SIDE_LEFT:
				aabb_setsize_centered_offset(x, depth, height, width,
											 depth * 0.5F, 0.5F, 0.5F);
				break;
			default:
				aabb_setsize_centered_offset(x, depth, height, width,
											 1.0F - depth * 0.5F, 0.5F, 0.5F);
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

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	return side == hook_side(this->block) ?
		tex_atlas_lookup(TEXAT_TRIPWIRE_RING) :
		tex_atlas_lookup(TEXAT_TRIPWIRE_REST);
}

static void hook_sync_line(struct server_local* s, struct block_info* info) {
	int ox, oy, oz;
	bool attached = false;
	bool powered = false;
	bool found_end = false;
	w_coord_t line_x[40], line_y[40], line_z[40];
	size_t line_len = 0;
	struct block_data end_blk;
	w_coord_t end_x = 0, end_y = 0, end_z = 0;

	blocks_side_offset(hook_side(info->block), &ox, &oy, &oz);

	for(int step = 1; step <= 40; step++) {
		struct block_data blk;
		w_coord_t x = info->x + ox * step;
		w_coord_t y = info->y + oy * step;
		w_coord_t z = info->z + oz * step;

		if(!server_world_get_block(AWORLD(s), x, y, z, &blk))
			break;

		if(blk.type == BLOCK_TRIPWIRE) {
			if(line_len < 40) {
				line_x[line_len] = x;
				line_y[line_len] = y;
				line_z[line_len] = z;
				line_len++;
			}
			if(blk.metadata & 0x01)
				powered = true;
			continue;
		}

		if(blk.type == BLOCK_TRIPWIRE_HOOK
		   && hook_side(&blk) == blocks_side_opposite(hook_side(info->block))
		   && line_len > 0) {
			found_end = true;
			end_blk = blk;
			end_x = x;
			end_y = y;
			end_z = z;
			attached = true;
		}
		break;
	}

	if((info->block->metadata & TRIPWIRE_HOOK_ATTACHED) != (attached ? TRIPWIRE_HOOK_ATTACHED : 0)
	   || (info->block->metadata & TRIPWIRE_HOOK_POWERED) != (powered ? TRIPWIRE_HOOK_POWERED : 0)) {
		struct block_data cur = *info->block;
		cur.metadata = (cur.metadata & 0x03)
					   | (attached ? TRIPWIRE_HOOK_ATTACHED : 0)
					   | (powered ? TRIPWIRE_HOOK_POWERED : 0);
		server_world_set_block(s, info->x, info->y, info->z, cur);
		notifyNeighbours(s, info->x, info->y, info->z);
	}

	for(size_t i = 0; i < line_len; i++) {
		struct block_data blk;
		if(!server_world_get_block(AWORLD(s), line_x[i], line_y[i], line_z[i], &blk)
		   || blk.type != BLOCK_TRIPWIRE)
			continue;
		if((blk.metadata & 0x04) == (attached ? 0x04 : 0x00))
			continue;
		blk.metadata = (blk.metadata & ~0x04) | (attached ? 0x04 : 0x00);
		server_world_set_block(s, line_x[i], line_y[i], line_z[i], blk);
	}

	if(found_end
	   && ((end_blk.metadata & TRIPWIRE_HOOK_ATTACHED) != (attached ? TRIPWIRE_HOOK_ATTACHED : 0)
		   || (end_blk.metadata & TRIPWIRE_HOOK_POWERED) != (powered ? TRIPWIRE_HOOK_POWERED : 0))) {
		end_blk.metadata = (end_blk.metadata & 0x03)
						   | (attached ? TRIPWIRE_HOOK_ATTACHED : 0)
						   | (powered ? TRIPWIRE_HOOK_POWERED : 0);
		server_world_set_block(s, end_x, end_y, end_z, end_blk);
		notifyNeighbours(s, end_x, end_y, end_z);
	}
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
		case SIDE_FRONT: facing = 0; break;
		case SIDE_RIGHT: facing = 1; break;
		case SIDE_BACK: facing = 2; break;
		case SIDE_LEFT: facing = 3; break;
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

static void onWorldTick(struct server_local* s, struct block_info* info) {
	if(!hook_has_support(s, info)) {
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
		server_local_spawn_block_drops(s, info);
		return;
	}

	hook_sync_line(s, info);
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	if(hook_has_support(s, info))
		return;

	server_world_set_block(s, info->x, info->y, info->z,
						   (struct block_data) {.type = BLOCK_AIR, .metadata = 0});
	server_local_spawn_block_drops(s, info);
}

struct block block_tripwire_hook = {
	.name = "Tripwire Hook",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = onWorldTick,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.transparent = false,
	.renderBlock = render_block_tripwire_hook,
	.renderBlockAlways = NULL,
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
