/*
	Copyright (c) 2022 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../network/server_local.h"
#include "blocks.h"
#include "../sound/sound.h"
#include <math.h>

static struct noteblock_state* noteblock_state_get(struct server_local* s,
																		 w_coord_t x, w_coord_t y,
																		 w_coord_t z, bool create) {
	struct noteblock_state* free_slot = NULL;
	for(int i = 0; i < MAX_NOTEBLOCKS; i++) {
		struct noteblock_state* st = &s->noteblocks[i];
		if(st->pos.x == x && st->pos.y == y && st->pos.z == z)
			return st;
		if(!free_slot && st->pos.x == -1)
			free_slot = st;
	}
	if(!create || !free_slot)
		return NULL;
	free_slot->pos = (struct complex_block_pos) {.x = x, .y = y, .z = z};
	free_slot->powered = false;
	return free_slot;
}

static enum pcm_sound noteblock_instrument(struct server_local* s,
																	 struct block_info* info) {
	struct block_data below;
	if(!server_world_get_block(AWORLD(s), info->x, info->y - 1, info->z,
										&below))
		return pcm_note_harp;

	if(below.type == BLOCK_GLASS || below.type == BLOCK_GLASS_PANE)
		return pcm_note_pling;
	if(below.type == BLOCK_STONE || below.type == BLOCK_COBBLESTONE
	   || below.type == BLOCK_BRICKS || below.type == BLOCK_STONE_BRICKS)
		return pcm_note_bass;
	return pcm_note_harp;
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	struct noteblock_state* st = noteblock_state_get(
		s, info->x, info->y, info->z, true);
	if(!st)
		return;

	bool powered = block_redstone_is_powered(s, info->x, info->y, info->z);
	if(powered && !st->powered) {
		sound_play_at_pitch(noteblock_instrument(s, info),
													 info->x + 0.5F, info->y + 0.5F,
													 info->z + 0.5F, 1.0F,
													 powf(2.0F, info->block->metadata / 12.0F));
	}
	st->powered = powered;
}

static void onRightClick(struct server_local* s, struct item_data* it,
							 struct block_info* where, struct block_info* on,
							 enum side on_side) {
	(void)s;
	(void)it;
	(void)where;
	(void)on_side;
	struct block_data block = *on->block;
	block.metadata = (block.metadata + 1) % 25;
	server_world_set_block(s, on->x, on->y, on->z, block);
	}

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_WOOD;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_full();
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_JUKBEBOX_SIDE);
}

struct block block_noteblock = {
	.name = "Note Block",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 1200,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
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
