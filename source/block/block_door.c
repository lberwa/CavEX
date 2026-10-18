/*
	Copyright (c) 2023 ByteBit/xtreme8000

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

//todo: fix door logic, as it currently isn't opening nicely..
// this is caused by dual opening function: open manually or toggle open/close status by redstone.
// best would be to add something that looks at the neighbour state change
//todo; fix rendering of the door in various metadata positions


#include "../network/server_local.h"
#include "../sound/sound.h"
#include "blocks.h"

static enum block_material getMaterial1(struct block_info* this) {
	return MATERIAL_WOOD;
}

static enum block_material getMaterial2(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x) {
		uint8_t state = this->block->metadata & 0x03;
		/* The upper half stores the hinge bit in bit 2, so its open/closed
		 * geometry must use the lower half's swing direction instead. */
		if((this->block->metadata & 0x08) && this->neighbours
		   && this->neighbours[SIDE_BOTTOM].type == this->block->type)
			state = this->neighbours[SIDE_BOTTOM].metadata & 0x03;
		switch(state) {
			case 0:
				aabb_setsize(x, 0.1875F, 1.0F, 1.0F);
				aabb_translate(x, -0.40625F, 0, 0);
				break;
			case 1:
				aabb_setsize(x, 1.0F, 1.0F, 0.1875F);
				aabb_translate(x, 0, 0, -0.40625F);
				break;
			case 2:
				aabb_setsize(x, 0.1875F, 1.0F, 1.0F);
				aabb_translate(x, 0.40625F, 0, 0);
				break;
			case 3:
				aabb_setsize(x, 1.0F, 1.0F, 0.1875F);
				aabb_translate(x, 0, 0, 0.40625F);
				break;
		}
	}

	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	// could be improved
	return face_occlusion_empty();
}

static uint16_t getTextureIndex1(struct block_info* this, enum side side) {
	return (this->block->metadata & 0x08) ?
		tex_atlas_lookup(TEXAT_DOOR_WOOD_TOP) :
		tex_atlas_lookup(TEXAT_DOOR_WOOD_BOTTOM);
}

static uint16_t getTextureIndex2(struct block_info* this, enum side side) {
	return (this->block->metadata & 0x08) ?
		tex_atlas_lookup(TEXAT_DOOR_IRON_TOP) :
		tex_atlas_lookup(TEXAT_DOOR_IRON_BOTTOM);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = ITEM_DOOR_WOOD;
		it->durability = 0;
		it->count = 1;
	}

	//only drop item from bottom half
	if(!(this->block->metadata & 0x08)) return 1;
	return 0;
}

static size_t getDroppedItem2(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = ITEM_DOOR_IRON;
		it->durability = 0;
		it->count = 1;
	}

	//only drop item from bottom half
	if(!(this->block->metadata & 0x08)) return 1;
	return 0;
}

static struct redstone_openable_state*
door_power_state_get(struct server_local* s, w_coord_t x, w_coord_t y,
					 w_coord_t z, bool create, bool initial_powered) {
	struct redstone_openable_state* free_slot = NULL;

	for(size_t i = 0; i < MAX_REDSTONE_OPENABLES; i++) {
		struct redstone_openable_state* st = &s->redstone_openables[i];
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
	free_slot->powered = initial_powered;
	free_slot->hinge_left = false;
	free_slot->closed_dir = 0;
	/* Hinge state is also encoded in the upper half, so it survives a
	 * save/load cycle before the server-side state table is rebuilt. */
	struct block_data upper;
	if(server_world_get_block(AWORLD(s), x, y + 1, z, &upper)
	   && upper.type != BLOCK_AIR && (upper.metadata & 0x08))
		free_slot->hinge_left = (upper.metadata & 0x04) != 0;
	return free_slot;
}

void block_door_set_hinge(struct server_local* s, w_coord_t x, w_coord_t y,
						  w_coord_t z, bool hinge_left, uint8_t closed_dir) {
	struct redstone_openable_state* st = door_power_state_get(
		s, x, y, z, true, false);
	if(!st)
		return;
	st->hinge_left = hinge_left;
	st->closed_dir = closed_dir & 0x03;
}

static void setDoorOpen(struct server_local* s, w_coord_t x, w_coord_t y,
						w_coord_t z, uint8_t doorType, bool open) {
	struct block_data bd;
	if(!server_world_get_block(AWORLD(s), x, y, z, &bd))
		return;

	struct redstone_openable_state* st = door_power_state_get(
		s, x, y, z, true, false);
	if(st && !(bd.metadata & 0x04))
		st->closed_dir = bd.metadata & 0x03;
	uint8_t dir = st ? st->closed_dir : (bd.metadata & 0x03);
	/* The stored hinge side is viewed from the player side.  The door's
	 * world-facing swing is the opposite rotation around that hinge. */
	uint8_t swing = (uint8_t)((dir + (st && st->hinge_left ? 3 : 1)) % 4);
	uint8_t newMeta = open ? (uint8_t)(0x04 | swing) : dir;

	server_world_set_block(s, x, y, z,
						   (struct block_data) {.type = doorType,
												.metadata = newMeta});
	server_world_set_block(s, x, y + 1, z,
						   (struct block_data) {.type = doorType,
													.metadata = (uint8_t)((newMeta & 0x03)
														| (st && st->hinge_left ? 0x04 : 0)
														| 0x08)});
}

static void toggleDoor(struct server_local* s,
                       w_coord_t x, w_coord_t y, w_coord_t z,
                       uint8_t doorType)
{
    struct block_data bd;
    if (!server_world_get_block(AWORLD(s), x, y, z, &bd)) return;

	bool opening = (bd.metadata & 0x04) == 0;
	struct redstone_openable_state* st = door_power_state_get(
		s, x, y, z, true, false);
	if(st && !(bd.metadata & 0x04))
		st->closed_dir = bd.metadata & 0x03;
	uint8_t dir = st ? st->closed_dir : (bd.metadata & 0x03);
	uint8_t swing = (uint8_t)((dir + (st && st->hinge_left ? 3 : 1)) % 4);
	uint8_t newMeta = opening ? (uint8_t)(0x04 | swing) : dir;

    // ——— NUDGE PLAYER IF THE DOOR SWINGS INTO THEM ———
    // handle both opening (0→1) and closing (1→0)
    if ((bd.metadata & 0x01) != (newMeta & 0x01)) {
		const uint8_t pid = s->active_player_id;
        struct block_data test = { .type = doorType, .metadata = newMeta };
        struct block_info di = {
            .x          = x,
            .y          = y,
            .z          = z,
            .block      = &test,
            .neighbours = NULL
        };
        if (entity_local_player_block_collide(
(vec3){s->players[pid].x, s->players[pid].y, s->players[pid].z}, &di))
        {
double dx = s->players[pid].x - (x + 0.5);
double dz = s->players[pid].z - (z + 0.5);
            if (fabs(dx) > fabs(dz)) {
                // push out along X
                double push = (dx > 0.0) ? +0.6 : -0.6;
s->players[pid].x = x + 0.5 + push;
            } else {
                // push out along Z
                double push = (dz > 0.0) ? +0.6 : -0.6;
s->players[pid].z = z + 0.5 + push;
            }
        }
    }
    // ————————————————————————————————————————————

    bool was_open = (bd.metadata & 0x04) != 0;
    server_world_set_block(s, x,   y,   z,
        (struct block_data){ .type = doorType, .metadata = newMeta });
	server_world_set_block(s, x, y+1, z,
		(struct block_data){ .type = doorType,
			.metadata = (uint8_t)((newMeta & 0x03)
				| (st && st->hinge_left ? 0x04 : 0) | 0x08) });
    sound_play_at_vol(was_open ? pcm_door_close : pcm_door_open,
                      x + 0.5f, y + 0.5f, z + 0.5f, 1.0f);
}

static void onRightClick(struct server_local* s,
                              struct item_data* it,
                              struct block_info* where,
                              struct block_info* on,
                              enum side on_side)
{
	if(blocks[on->block->type] == &block_iron_door)
		return;

    struct block_data cur = *on->block;
	bool topHalf = (cur.metadata & 0x08) != 0;
    w_coord_t bx = on->x;
    w_coord_t by = on->y - (topHalf ? 1 : 0);
    w_coord_t bz = on->z;

	bool powered = block_redstone_is_powered(s, bx, by, bz)
		|| block_redstone_is_powered(s, bx, by + 1, bz);
	door_power_state_get(s, bx, by, bz, true, powered)->powered = powered;

    toggleDoor(s, bx, by, bz, cur.type);
}


static void onNeighbourBlockChange(struct server_local* s,
                                   struct block_info* info)
{
    struct block_data cur = *info->block;
    w_coord_t partner_y = info->y + ((cur.metadata & 0x08) ? -1 : 1);
    struct block_data partner;
    bool valid = server_world_get_block(AWORLD(s), info->x, partner_y,
                                        info->z, &partner)
        && partner.type == cur.type
		&& ((partner.metadata & 0x08) != (cur.metadata & 0x08));
    if(!valid) {
        server_world_set_block(s, info->x, info->y, info->z,
                               (struct block_data) {.type = BLOCK_AIR});
        return;
    }

	if (cur.metadata & 0x08) return;

    bool powered = block_redstone_is_powered(s, info->x, info->y, info->z)
		|| block_redstone_is_powered(s, info->x, info->y + 1, info->z);

	struct redstone_openable_state* st = door_power_state_get(
		s, info->x, info->y, info->z, powered, false);
	if(!st)
		return;

	if(st->powered != powered) {
		st->powered = powered;
		setDoorOpen(s, info->x, info->y, info->z, cur.type, powered);
	}
}



struct block block_wooden_door = {
	.name = "Wooden Door",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial1,
	.getTextureIndex = getTextureIndex1,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onWorldTick = NULL,
    .onNeighbourBlockChange  = onNeighbourBlockChange,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_door,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 1,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 4500,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_iron_door = {
	.name = "Iron Door",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial2,
	.getTextureIndex = getTextureIndex2,
	.getDroppedItem = getDroppedItem2,
	.onRandomTick = NULL,
    .onNeighbourBlockChange  = onNeighbourBlockChange,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_door,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 1,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 7500,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
