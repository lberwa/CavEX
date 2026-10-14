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
#include "../entity/entity.h"
#include "../game/game_state.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F,
					 ((this->block->metadata & 0x7) > 1
					  && (this->block->metadata & 0x7) < 6) ?
						 0.625F :
						 0.125F,
					 1.0F);
	return entity ? 0 : 1;
}

static inline bool isRail(uint8_t type) {
    return type == BLOCK_RAIL
        || type == BLOCK_POWERED_RAIL
        || type == BLOCK_DETECTOR_RAIL;
}

static void updateRailToSlope(struct server_local* s, w_coord_t x, w_coord_t y, w_coord_t z, uint8_t newMeta) {
    struct block_data bd;
    if (!server_world_get_block(AWORLD(s), x, y, z, &bd) || !isRail(bd.type)) {
        return;
    }
    uint8_t oldMeta = bd.metadata & 0xF;
    if (oldMeta >= 6 && oldMeta <= 9) {
        return;
    }
    bd.metadata = (bd.metadata & ~0xF) | (newMeta & 0xF);
    server_world_set_block(s, x, y, z, bd);
}



// compute a single metadata value 0–9:
//   2–5 = ascending slopes,
//   0–1 = straight NS/EW,
//   6–9 = curves,
//   2–5 also reused for descending slopes (mirrored)
static uint8_t calcRailMeta(struct server_local* s,
                            w_coord_t x, w_coord_t y, w_coord_t z) {

	 struct block_data bd;

	// curve when this rail sits atop a diagonal slope
	bool lower_e = server_world_get_block(AWORLD(s), x+1, y-1, z, &bd) && isRail(bd.type);
	bool lower_w = server_world_get_block(AWORLD(s), x-1, y-1, z, &bd) && isRail(bd.type);
	bool lower_n = server_world_get_block(AWORLD(s), x,   y-1, z-1, &bd) && isRail(bd.type);
	bool lower_s = server_world_get_block(AWORLD(s), x,   y-1, z+1, &bd) && isRail(bd.type);

	// same‐level horizontal neighbours
	bool n = server_world_get_block(AWORLD(s), x,   y, z-1, &bd) && isRail(bd.type);
	bool s_ =server_world_get_block(AWORLD(s), x,   y, z+1, &bd) && isRail(bd.type);
	bool w = server_world_get_block(AWORLD(s), x-1, y, z,   &bd) && isRail(bd.type);
	bool e = server_world_get_block(AWORLD(s), x+1, y, z,   &bd) && isRail(bd.type);

	// if exactly one diagonal‐below slope + exactly one H‐neighbour on the perpendicular axis => curve
	if (lower_e && (n ^ s_) && !w && !e) return n ? 9 : 6;  // NE or SE
	if (lower_w && (n ^ s_) && !w && !e) return n ? 8 : 7;  // NW or SW
	if (lower_n && (w ^ e) && !n && !s_) return e ? 9 : 8;  // NE or NW
	if (lower_s && (w ^ e) && !n && !s_) return e ? 6 : 7;  // SE or SW

    // 1) slopes
    if (server_world_get_block(AWORLD(s), x+1,y+1,z, &bd) && isRail(bd.type)) return 2;
    if (server_world_get_block(AWORLD(s), x-1,y+1,z, &bd) && isRail(bd.type)) return 3;
    if (server_world_get_block(AWORLD(s), x,y+1,z-1, &bd) && isRail(bd.type)) return 4;
    if (server_world_get_block(AWORLD(s), x,y+1,z+1, &bd) && isRail(bd.type)) return 5;


    // 2) straight & curves
    n  = server_world_get_block(AWORLD(s), x,   y, z-1, &bd) && isRail(bd.type);
    s_ = server_world_get_block(AWORLD(s), x,   y, z+1, &bd) && isRail(bd.type);
    w  = server_world_get_block(AWORLD(s), x-1, y, z,   &bd) && isRail(bd.type);
    e  = server_world_get_block(AWORLD(s), x+1, y, z,   &bd) && isRail(bd.type);

    if ((n||s_) && !(w||e))      return 0; // NS
    if ((w||e) && !(n||s_))      return 1; // EW
    if (s_ && e)                 return 6; // SE
    if (s_ && w)                 return 7; // SW
    if (n   && w)                return 8; // NW
    if (n   && e)                return 9; // NE

    return 0;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint16_t getTextureIndex1(struct block_info* this, enum side side) {
	return (this->block->metadata < 6) ? tex_atlas_lookup(TEXAT_RAIL) :
										 tex_atlas_lookup(TEXAT_RAIL_CURVED);
}

static uint16_t getTextureIndex2(struct block_info* this, enum side side) {
	return (this->block->metadata & 0x8) ?
		tex_atlas_lookup(TEXAT_RAIL_POWERED_ON) :
		tex_atlas_lookup(TEXAT_RAIL_POWERED_OFF);
}

static uint16_t getTextureIndex3(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_RAIL_DETECTOR);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
                        struct block_info* where, struct block_info* on,
                        enum side on_side) {
    struct block_data below_blk;
    if (!server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z, &below_blk)) {
        return false;
    }
    if (!blocks[below_blk.type] || blocks[below_blk.type]->can_see_through) {
        return false;
    }

    uint8_t meta = calcRailMeta(s, where->x, where->y, where->z);
    if (it->id != BLOCK_RAIL && meta > 5)
        meta = 0;
    bool lower_e = false, lower_w = false, lower_n = false, lower_s = false;
    struct block_data neighbor;
    if (server_world_get_block(AWORLD(s), where->x + 1, where->y - 1, where->z, &neighbor) && isRail(neighbor.type)) {
        lower_e = true;
    }
    if (server_world_get_block(AWORLD(s), where->x - 1, where->y - 1, where->z, &neighbor) && isRail(neighbor.type)) {
        lower_w = true;
    }
    if (server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z + 1, &neighbor) && isRail(neighbor.type)) {
        lower_s = true;
    }
    if (server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z - 1, &neighbor) && isRail(neighbor.type)) {
        lower_n = true;
    }

    if (meta < 2) {
        if ((lower_e || lower_w) && !(lower_n || lower_s)) {
            meta = 1;
        } else if ((lower_n || lower_s) && !(lower_e || lower_w)) {
            meta = 0;
        }
    }

    if (lower_e) {
        updateRailToSlope(s, where->x + 1, where->y - 1, where->z, 3);
    }
    if (lower_w) {
        updateRailToSlope(s, where->x - 1, where->y - 1, where->z, 2);
    }
    if (lower_s) {
        updateRailToSlope(s, where->x, where->y - 1, where->z + 1, 4);
    }
    if (lower_n) {
        updateRailToSlope(s, where->x, where->y - 1, where->z - 1, 5);
    }

    struct block_data new_blk = {
        .type = it->id,
        .metadata = meta,
        .sky_light = 0,
        .torch_light = 0
    };
    server_world_set_block(s, where->x, where->y, where->z, new_blk);
    return true;
}

static void onNeighbourBlockChange(struct server_local* s,
                                   struct block_info* info) {
    struct block_data cur;
    server_world_get_block(AWORLD(s),
                           info->x, info->y, info->z,
                           &cur);

    // Regular rails use bits 0–3 for shape (0–9, incl. curves).
    // Powered/detector rails use only bits 0–2 for shape (0–5, no curves);
    // bit 3 is the powered/activated flag and must not be overwritten here.
    bool is_regular = (cur.type == BLOCK_RAIL);
    uint8_t shape_mask = is_regular ? 0x0F : 0x07;

    uint8_t old_shape = cur.metadata & shape_mask;
    uint8_t new_shape = calcRailMeta(s, info->x, info->y, info->z);

    if (!is_regular && new_shape > 5)
        new_shape = old_shape;

    new_shape &= shape_mask;
    if (new_shape == old_shape) return;

    cur.metadata = (cur.metadata & ~shape_mask) | new_shape;
    server_world_set_block(s, info->x, info->y, info->z, cur);
}

struct powered_rail_node {
    w_coord_t x, y, z;
    uint8_t distance;
};

static bool powered_rail_chain_has_source(struct server_local* s,
                                          w_coord_t x, w_coord_t y,
                                          w_coord_t z) {
    struct powered_rail_node queue[32];
    struct powered_rail_node seen[32];
    int head = 0;
    int tail = 0;
    int seen_count = 0;

    queue[tail++] = (struct powered_rail_node) {x, y, z, 0};
    seen[seen_count++] = queue[0];

    while(head < tail) {
        struct powered_rail_node node = queue[head++];
        struct block_data rail;

        if(!server_world_get_block(AWORLD(s), node.x, node.y, node.z,
                                    &rail)
           || rail.type != BLOCK_POWERED_RAIL)
            continue;

        /* Only a non-rail redstone source can start the chain. */
        if(block_redstone_is_powered(s, node.x, node.y, node.z))
            return true;

        if(node.distance >= 8)
            continue;

        // Only propagate along the rail's own axis (EW: shape 1/2/3, NS: shape 0/4/5).
        // Perpendicular neighbours (parallel tracks) must not receive power.
        uint8_t shape = rail.metadata & 0x07;
        bool is_ew = (shape == 1 || shape == 2 || shape == 3);

        for(int side = 0; side < SIDE_MAX; side++) {
            int ox, oy, oz;
            blocks_side_offset((enum side)side, &ox, &oy, &oz);
            if (is_ew ? (ox == 0) : (oz == 0)) continue;
            w_coord_t nx = node.x + ox;
            w_coord_t ny = node.y + oy;
            w_coord_t nz = node.z + oz;
            struct block_data neighbour;

            if(!server_world_get_block(AWORLD(s), nx, ny, nz, &neighbour)
               || neighbour.type != BLOCK_POWERED_RAIL)
                continue;

            bool already_seen = false;
            for(int i = 0; i < seen_count; i++) {
                if(seen[i].x == nx && seen[i].y == ny && seen[i].z == nz) {
                    already_seen = true;
                    break;
                }
            }
            if(already_seen || tail >= (int)(sizeof(queue) / sizeof(queue[0])))
                continue;

            struct powered_rail_node next = {
                nx, ny, nz, (uint8_t)(node.distance + 1)
            };
            queue[tail++] = next;
            seen[seen_count++] = next;
        }
    }

    return false;
}

static bool detector_rail_has_minecart(struct server_local* s,
                                       struct block_info* info) {
    struct AABB rail_box;
    aabb_setsize_centered(&rail_box, 1.0F, 0.125F, 1.0F);
    aabb_translate(&rail_box, info->x + 0.5F, info->y + 0.0625F,
                   info->z + 0.5F);

    // Client entities have current positions (client runs minecart physics).
    // Server entities stay at their spawn position, so s->entities is useless here.
    dict_entity_it_t it;
    dict_entity_it(it, gstate.entities);
    while(!dict_entity_end_p(it)) {
        struct entity* e = dict_entity_ref(it)->value;
        dict_entity_next(it);

        if(!e || e->type != ENTITY_MINECART || !e->getBoundingBox)
            continue;

        struct AABB entity_box;
        if(e->getBoundingBox(e, &entity_box) != 0
           && aabb_intersection(&rail_box, &entity_box))
            return true;
    }
    return false;
}

static void powered_rail_onWorldTick(struct server_local* s,
                                     struct block_info* info) {
    struct block_data cur = *info->block;
    bool powered = powered_rail_chain_has_source(s, info->x, info->y,
                                                 info->z);
    uint8_t state = powered ? 0x08 : 0;

    if((cur.metadata & 0x08) == state)
        return;

    cur.metadata = (cur.metadata & 0x07) | state;
    server_world_set_block(s, info->x, info->y, info->z, cur);
    notifyNeighbours(s, info->x, info->y, info->z);
}

static void detector_rail_onWorldTick(struct server_local* s,
                                      struct block_info* info) {
    struct block_data cur = *info->block;
    bool occupied = detector_rail_has_minecart(s, info);
    uint8_t state = occupied ? 0x08 : 0;

    if((cur.metadata & 0x08) == state)
        return;

    cur.metadata = (cur.metadata & 0x07) | state;
    server_world_set_block(s, info->x, info->y, info->z, cur);
    notifyNeighbours(s, info->x, info->y, info->z);
}


struct block block_rail = {
	.name = "Rail",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex1,
	.getDroppedItem = block_drop_default,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onRandomTick = NULL,
	.onWorldTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_rail,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.render_block_data.rail_curved_possible = true,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 1050,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_powered_rail = {
	.name = "Powered rail",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex2,
	.getDroppedItem = block_drop_default,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onRandomTick = NULL,
	.onWorldTick = powered_rail_onWorldTick,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_rail,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.render_block_data.rail_curved_possible = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 1050,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};

struct block block_detector_rail = {
	.name = "Detector rail",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex3,
	.getDroppedItem = block_drop_default,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onRandomTick = NULL,
	.onWorldTick = detector_rail_onWorldTick,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_rail,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.render_block_data.rail_curved_possible = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 1050,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.fuel = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
