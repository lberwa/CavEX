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
#include "../particle.h"
#include "../game/game_state.h"

#define REDSTONE_WIRE_SOURCE_SEARCH_MAX 512

struct wire_pos {
	w_coord_t x, y, z;
};

static bool redstone_wire_pos_equal(struct wire_pos a, w_coord_t x,
									w_coord_t y, w_coord_t z) {
	return a.x == x && a.y == y && a.z == z;
}

static bool redstone_wire_seen(struct wire_pos* seen, int count, w_coord_t x,
							   w_coord_t y, w_coord_t z) {
	for(int i = 0; i < count; i++)
		if(redstone_wire_pos_equal(seen[i], x, y, z))
			return true;
	return false;
}

static bool redstone_wire_enqueue(struct server_local* s, struct wire_pos* queue,
								  int* tail, struct wire_pos* seen, int* seen_n,
								  w_coord_t x, w_coord_t y, w_coord_t z) {
	struct block_data blk;

	if(*tail >= REDSTONE_WIRE_SOURCE_SEARCH_MAX
	   || *seen_n >= REDSTONE_WIRE_SOURCE_SEARCH_MAX
	   || redstone_wire_seen(seen, *seen_n, x, y, z)
	   || !server_world_get_block(AWORLD(s), x, y, z, &blk)
	   || blk.type != BLOCK_REDSTONE_WIRE)
		return false;

	queue[(*tail)++] = (struct wire_pos) {x, y, z};
	seen[(*seen_n)++] = (struct wire_pos) {x, y, z};
	return true;
}

static bool redstone_wire_is_primary_source(const struct block_data* blk) {
	uint8_t meta = blk->metadata & 0x0F;

	switch(blk->type) {
		case BLOCK_REDSTONE_TORCH_LIT:
			return true;
		case BLOCK_LEVER:
			return (meta & 0x08) != 0;
		case BLOCK_STONE_BUTTON:
			return (meta & 0x08) != 0;
		case BLOCK_STONE_PRESSURE_PLATE:
		case BLOCK_WOOD_PRESSURE_PLATE:
			return (meta & 0x01) != 0;
		case BLOCK_DETECTOR_RAIL:
			return (meta & 0x08) != 0;
		case BLOCK_TRIPWIRE_HOOK:
			return (meta & 0x08) != 0;
		default:
			return false;
	}
}

static enum side redstone_wire_repeater_output_side(const struct block_data* blk) {
	switch(blk->metadata & 0x03) {
		case 0: return SIDE_FRONT;
		case 1: return SIDE_RIGHT;
		case 2: return SIDE_BACK;
		default: return SIDE_LEFT;
	}
}

static bool redstone_wire_repeater_is_external_source(struct server_local* s,
													  struct block_data* blk,
													  w_coord_t x, w_coord_t y,
													  w_coord_t z,
													  enum side side_to_wire) {
	(void)s;
	(void)x;
	(void)y;
	(void)z;
	return blk->type == BLOCK_REPEATER_ON
		   && redstone_wire_repeater_output_side(blk) == side_to_wire;
}

static bool redstone_wire_network_has_primary_source(struct server_local* s,
													 w_coord_t x, w_coord_t y,
													 w_coord_t z) {
	static const enum side horiz[4] = {
		SIDE_RIGHT,
		SIDE_LEFT,
		SIDE_FRONT,
		SIDE_BACK,
	};
	struct wire_pos queue[REDSTONE_WIRE_SOURCE_SEARCH_MAX];
	struct wire_pos seen[REDSTONE_WIRE_SOURCE_SEARCH_MAX];
	int head = 0, tail = 0, seen_n = 0;

	redstone_wire_enqueue(s, queue, &tail, seen, &seen_n, x, y, z);

	while(head < tail) {
		struct wire_pos p = queue[head++];
		struct block_data above;
		bool above_air = server_world_get_block(AWORLD(s), p.x, p.y + 1, p.z,
												&above)
						 && above.type == BLOCK_AIR;

		for(int i = 0; i < 4; i++) {
			int ox, oy, oz;
			struct block_data nb;
			enum side side = horiz[i];
			enum side side_to_wire = blocks_side_opposite(side);
			blocks_side_offset(side, &ox, &oy, &oz);

			if(!server_world_get_block(AWORLD(s), p.x + ox, p.y + oy,
									   p.z + oz, &nb))
				continue;

			if(nb.type == BLOCK_REDSTONE_WIRE) {
				redstone_wire_enqueue(s, queue, &tail, seen, &seen_n,
									  p.x + ox, p.y, p.z + oz);
			} else if((redstone_wire_is_primary_source(&nb)
						   && block_redstone_powers_wire(&nb, side_to_wire))
						  || redstone_wire_repeater_is_external_source(
							  s, &nb, p.x + ox, p.y + oy, p.z + oz,
							  side_to_wire)) {
				return true;
			}

			/* A lever or button also powers its solid support block. */
			if(nb.type != BLOCK_REDSTONE_WIRE
			   && blocks[nb.type] && !blocks[nb.type]->can_see_through
			   && block_redstone_is_button_powered_block(
					s, p.x + ox, p.y + oy, p.z + oz))
				return true;

			if(nb.type == BLOCK_AIR)
				redstone_wire_enqueue(s, queue, &tail, seen, &seen_n,
									  p.x + ox, p.y - 1, p.z + oz);

			if(above_air && blocks[nb.type] && !blocks[nb.type]->can_see_through)
				redstone_wire_enqueue(s, queue, &tail, seen, &seen_n,
									  p.x + ox, p.y + 1, p.z + oz);
		}

		for(int i = 0; i < 2; i++) {
			enum side side = i == 0 ? SIDE_TOP : SIDE_BOTTOM;
			int ox, oy, oz;
			struct block_data nb;
			blocks_side_offset(side, &ox, &oy, &oz);
			if(!server_world_get_block(AWORLD(s), p.x + ox, p.y + oy,
									p.z + oz, &nb))
				continue;

			if(nb.type == BLOCK_REDSTONE_WIRE) {
				redstone_wire_enqueue(s, queue, &tail, seen, &seen_n,
									  p.x + ox, p.y + oy, p.z + oz);
			} else if((redstone_wire_is_primary_source(&nb)
						   && block_redstone_powers_wire(
									&nb, blocks_side_opposite(side)))
						  || redstone_wire_repeater_is_external_source(
							  s, &nb, p.x + ox, p.y + oy, p.z + oz,
							  blocks_side_opposite(side))) {
				return true;
			}

			if(nb.type != BLOCK_REDSTONE_WIRE
			   && blocks[nb.type] && !blocks[nb.type]->can_see_through
			   && block_redstone_is_button_powered_block(
					s, p.x + ox, p.y + oy, p.z + oz))
				return true;
		}
	}

	return false;
}


static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
                             struct AABB* x) {
    if (x) {
        aabb_setsize(x,
                     1.0F,
                     0.125F,
                     1.0F);
    }
    return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	uint8_t lvl = this->block->metadata & 0x0F;
	if (lvl == 0) {
		return tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF);
	}
	return tex_atlas_lookup(TEXAT_REDSTONE_WIRE_L1 + (lvl - 1));
}

static void onWorldTick(struct server_local* s, struct block_info* blk) {
    if (!blk->neighbours) return;
    struct block_data cur = *blk->block;
    if (cur.type != BLOCK_REDSTONE_WIRE) return;

    struct block_data btmp;
    #define GET_BLOCK_AT(xx,yy,zz, out) server_world_get_block(AWORLD(s), (xx), (yy), (zz), (out))

    uint8_t strong = 0;
	for (int side = 0; side < SIDE_MAX; ++side) {
		if (!blk->neighbours) continue;

		enum side sdir = (enum side)side;
		uint8_t ntype = blk->neighbours[side].type;
		if (ntype == BLOCK_LEVER
			&& (blk->neighbours[side].metadata & 0x08) != 0) {
			strong = 15;
			break;
		}
		int ox, oy, oz;
        blocks_side_offset(sdir, &ox, &oy, &oz);

        if (ntype != BLOCK_REDSTONE_WIRE
			&& block_redstone_powers_wire(&blk->neighbours[side],
										  blocks_side_opposite(sdir))) {
            strong = 15;
            break;
        }
        if (blocks[ntype] && !blocks[ntype]->can_see_through
			&& block_redstone_is_button_powered_block(
				s, blk->x + ox, blk->y + oy, blk->z + oz)) {
            strong = 15;
            break;
        }
    }

    const enum side horiz[4] = {
            SIDE_RIGHT,  // +X
            SIDE_LEFT,   // -X
            SIDE_FRONT,  // +Z
            SIDE_BACK    // -Z
    };
    uint8_t maxWire = 0;
    bool above_air = false;
    if (GET_BLOCK_AT(blk->x, blk->y + 1, blk->z, &btmp) && btmp.type == BLOCK_AIR) {
        above_air = true;
    }

	for (int i = 0; i < 4; ++i) {
        if (!blk->neighbours) continue;

        enum side sdir = horiz[i];
        uint8_t ntype = blk->neighbours[sdir].type;
        uint8_t nmeta = blk->neighbours[sdir].metadata & 0x0F;
        int ox, oy, oz;
        blocks_side_offset(sdir, &ox, &oy, &oz);

        if (ntype == BLOCK_REDSTONE_WIRE) {
            if (block_redstone_wire_points_to_side(
					s, blk->x + ox, blk->y, blk->z + oz,
					blocks_side_opposite(sdir))
				&& nmeta > maxWire)
				maxWire = nmeta;
        }

        // down-diagonal only if side is air (unchanged)
        if (ntype == BLOCK_AIR) {
            if (GET_BLOCK_AT(blk->x + ox, blk->y - 1, blk->z + oz, &btmp)) {
                if (btmp.type == BLOCK_REDSTONE_WIRE) {
                    uint8_t p = btmp.metadata & 0x0F;
                    if (block_redstone_wire_points_to_side(
							s, blk->x + ox, blk->y - 1, blk->z + oz,
							blocks_side_opposite(sdir))
						&& p > maxWire)
						maxWire = p;
                }
            }
        }

        // Upward dust only climbs along the side of a solid neighbouring block.
        // Without that block face, lower dust must not implicitly connect to
        // redstone one block higher.
        if (above_air && blocks[ntype] && !blocks[ntype]->can_see_through) {
            if (GET_BLOCK_AT(blk->x + ox, blk->y + 1, blk->z + oz, &btmp)) {
                if (btmp.type == BLOCK_REDSTONE_WIRE) {
                    uint8_t p = btmp.metadata & 0x0F;
                    if (block_redstone_wire_points_to_side(
							s, blk->x + ox, blk->y + 1, blk->z + oz,
							blocks_side_opposite(sdir))
						&& p > maxWire)
						maxWire = p;
                }
            }
	    }
	}

	/* Redstone wire also propagates directly between vertical neighbours. */
	const enum side vertical[2] = { SIDE_TOP, SIDE_BOTTOM };
	for (int i = 0; i < 2; ++i) {
		enum side sdir = vertical[i];
		int ox, oy, oz;
		struct block_data nb;
		blocks_side_offset(sdir, &ox, &oy, &oz);

		if(!GET_BLOCK_AT(blk->x + ox, blk->y + oy, blk->z + oz, &nb)
		   || nb.type != BLOCK_REDSTONE_WIRE)
			continue;

		uint8_t p = nb.metadata & 0x0F;
		if(p > maxWire
		   && block_redstone_wire_points_to_side(
				s, blk->x + ox, blk->y + oy, blk->z + oz,
				blocks_side_opposite(sdir)))
			maxWire = p;
	}

    uint8_t desired = strong
                     ? 15
                     : (maxWire ? maxWire - 1 : 0);
	if(desired > 0 && strong == 0
	   && !redstone_wire_network_has_primary_source(s, blk->x, blk->y, blk->z))
		desired = 0;

    vec3 c = { blk->x + 0.5f, blk->y, blk->z + 0.5f };
    particle_generate_redstone_wire(c, desired);

    if ((cur.metadata & 0x0F) != desired) {
        server_world_set_block(s,
                               blk->x, blk->y, blk->z,
                               (struct block_data){
                                   .type     = BLOCK_REDSTONE_WIRE,
                                   .metadata = desired
                               });
        notifyNeighbours(s, blk->x, blk->y, blk->z);

    }
}

static size_t getDroppedItem(struct block_info* this,
                             struct item_data* it,
                             struct random_gen* g,
                             struct server_local* s)
{
    if (it) {
        it->id = ITEM_REDSTONE;
        it->durability = 0;
        it->count = 1;
    }
    return 1;
}

struct block block_redstone_wire = {
	.name = "Redstone wire",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = render_block_redstone_wire,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
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
		.onItemPlace = block_place_default,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
