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

#include "blocks.h"
#include "../graphics/gfx_settings.h"
#include "../game/game_state.h"
#include "../network/server_local.h"
#include "../network/server_world.h"

/*
	Water flow, modelled after Minecraft's fluid mechanics.

	Metadata encoding (matches the fancy-liquid renderer in getSideMask):
	  BLOCK_WATER_STILL, meta 0        -> source block (permanent)
	  BLOCK_WATER_FLOW,  meta 1..7     -> flowing, "distance from source"
	                                      (1 = tallest/closest, 7 = shortest/farthest)
	  BLOCK_WATER_FLOW,  meta & 0x8    -> falling water (full height, fed from above)

	Driven by a scheduled-tick system: server_local wakes only the water cells
	next to a recent block change (see server_local_schedule_fluid) and calls
	block_water_flow_update() on them. Static/generated oceans are never woken,
	so they stay dormant instead of re-meshing every tick. To behave like a
	cellular automaton, this function only READS the current world and queues
	its changes via server_local_queue_fluid_change(); server_local applies them
	all afterwards, avoiding same-tick multi-block cascades.
*/

#define WATER_MAX_LEVEL 7     // furthest a flow reaches horizontally from a source
#define FLOW_SEARCH 5         // how far to look for a downward hole (Minecraft: up to 5)
#define WATER_COST_NONE 0x7fffffff

// horizontal neighbour offsets: +x, -x, +z, -z (opp[i] = index of opposite dir)
static const int hx[4] = {1, -1, 0, 0};
static const int hz[4] = {0, 0, 1, -1};
static const int opp[4] = {1, 0, 3, 2};

static bool water_is_water(uint8_t type) {
	return type == BLOCK_WATER_STILL || type == BLOCK_WATER_FLOW;
}

// Can water flow into / replace this block? Air and non-fluid "place_ignore"
// blocks (plants, torches, ...) get washed away; solids and fluids block it.
static bool water_replaceable(uint8_t type) {
	if(type == BLOCK_AIR)
		return true;
	if(water_is_water(type) || type == BLOCK_LAVA_STILL || type == BLOCK_LAVA_FLOW)
		return false;
	const struct block* b = blocks[type];
	return b && b->place_ignore;
}

// Does the block *below* a cell let water descend there, i.e. count that cell as
// a "hole" for flow-direction pathfinding? Like Minecraft: anything that isn't a
// solid floor -- air, plants AND water (a lower/adjacent pool). This is why an
// abyss with water at the bottom must NOT be treated like stone: water still
// streams toward it and pours in.
static bool water_below_is_hole(uint8_t below_type) {
	return water_replaceable(below_type) || water_is_water(below_type);
}

// Can the flow-direction search travel through this cell? Air/plants and, like
// Minecraft, *flowing* water -- the search follows an existing stream to find
// the hole beyond it, so once the stream to a ravine exists the source keeps
// feeding it instead of flooding sideways. Solids and full source blocks stop
// the search.
static bool water_search_passable(uint8_t type) {
	return water_replaceable(type) || type == BLOCK_WATER_FLOW;
}

// Queue a change only if it actually differs from the current cell. Preserves
// the existing light values (server_world_set_block recomputes lighting anyway).
static void water_try_set(struct server_local* s, w_coord_t x, w_coord_t y,
						  w_coord_t z, uint8_t type, uint8_t meta) {
	struct block_data cur;
	if(!server_world_get_block(AWORLD(s), x, y, z, &cur))
		return; // chunk not loaded: skip
	if(cur.type == type && cur.metadata == meta)
		return; // already in the desired state
	server_local_queue_fluid_change(s, x, y, z,
		(struct block_data) {
			.type = type,
			.metadata = meta,
			.sky_light = cur.sky_light,
			.torch_light = cur.torch_light,
		});
}

// Recursively look for the nearest cell (within `remaining` steps) that sits
// over a hole (see water_below_is_hole), i.e. a spot water could pour down.
// Returns the step distance to it, or WATER_COST_NONE if none is reachable.
// `came_from` is the direction index we entered from, so we never backtrack.
static int water_drop_search(struct server_local* s, w_coord_t x, w_coord_t y,
							 w_coord_t z, int remaining, int came_from) {
	int best = WATER_COST_NONE;
	for(int i = 0; i < 4; i++) {
		if(came_from >= 0 && i == opp[came_from])
			continue;
		w_coord_t nx = x + hx[i], nz = z + hz[i];
		struct block_data n;
		if(!server_world_get_block(AWORLD(s), nx, y, nz, &n))
			continue;
		if(!water_search_passable(n.type))
			continue; // stop at solids and full source blocks
		struct block_data nb;
		if(server_world_get_block(AWORLD(s), nx, y - 1, nz, &nb)
		   && water_below_is_hole(nb.type)) {
			if(1 < best)
				best = 1; // hole one step further away
			continue;
		}
		if(remaining > 1) {
			int c = water_drop_search(s, nx, y, nz, remaining - 1, i);
			if(c != WATER_COST_NONE && c + 1 < best)
				best = c + 1;
		}
	}
	return best;
}

// One cellular-automaton step for a single water cell. Called by the fluid
// scheduler in server_local (only for water that was woken by a nearby change),
// NOT via the generic per-block world tick -- that keeps static oceans dormant.
void block_water_flow_update(struct server_local* s, struct block_info* info) {
	w_coord_t x = info->x, y = info->y, z = info->z;
	uint8_t type = info->block->type;
	uint8_t meta = info->block->metadata;
	bool is_source = (type == BLOCK_WATER_STILL);
	bool is_falling = !is_source && (meta & 0x8);
	// set when a flowing cell's level rises this tick, i.e. its feeder weakened
	// and it is receding. Receding water must NOT spread (Minecraft doesn't), or
	// it keeps re-wetting just-dried cells and leaves random puddles behind.
	bool receding = false;

	struct block_data below, above;
	bool has_below = server_world_get_block(AWORLD(s), x, y - 1, z, &below);
	bool has_above = server_world_get_block(AWORLD(s), x, y + 1, z, &above);
	bool water_above = has_above && water_is_water(above.type);

	// --- infinite source formation ---------------------------------------
	// A flowing block flanked by >=2 source blocks with solid ground (or a
	// source) below turns into a new source block (the "2x2 infinity pool").
	if(type == BLOCK_WATER_FLOW) {
		int src_neighbors = 0;
		for(int i = 0; i < 4; i++) {
			struct block_data n;
			if(server_world_get_block(AWORLD(s), x + hx[i], y, z + hz[i], &n)
			   && n.type == BLOCK_WATER_STILL)
				src_neighbors++;
		}
		bool support_below = has_below
			&& ((!water_is_water(below.type) && !water_replaceable(below.type))
				|| below.type == BLOCK_WATER_STILL);
		if(src_neighbors >= 2 && support_below) {
			water_try_set(s, x, y, z, BLOCK_WATER_STILL, 0);
			return;
		}
	}

	// --- validate flowing blocks: they must have a feeder ----------------
	if(!is_source) {
		if(water_above) {
			// fed from directly above -> this is a falling column
			if(meta != 0x8)
				water_try_set(s, x, y, z, BLOCK_WATER_FLOW, 0x8);
			meta = 0x8;
			is_falling = true;
		} else {
			// the strongest horizontal feeder gives the lowest resulting level
			int best = 8; // 8 == out of range / no feeder
			for(int i = 0; i < 4; i++) {
				struct block_data n;
				if(!server_world_get_block(AWORLD(s), x + hx[i], y, z + hz[i], &n))
					continue;
				if(n.type == BLOCK_WATER_STILL) {
					if(1 < best)
						best = 1; // a source feeds level 1
				} else if(n.type == BLOCK_WATER_FLOW) {
					// a falling neighbour acts as a source on the ground
					int nl = (n.metadata & 0x8) ? 1 : (n.metadata + 1);
					if(nl < best)
						best = nl;
				}
			}

			if(best > WATER_MAX_LEVEL) {
				// no feeder within range anymore: dry up this tick
				water_try_set(s, x, y, z, BLOCK_AIR, 0);
				return;
			}

			if((uint8_t)best != meta)
				water_try_set(s, x, y, z, BLOCK_WATER_FLOW, (uint8_t)best);
			receding = ((uint8_t)best > meta); // level rose -> feeder weakened
			meta = (uint8_t)best;
			is_falling = false;
		}
	}

	// A receding cell only settles/dries; it must not create new water, else the
	// retreat re-wets already-dried cells and leaves stray puddles behind.
	if(receding)
		return;

	// --- spreading -------------------------------------------------------
	uint8_t out_level = (is_source || is_falling) ? 1 : (uint8_t)(meta + 1);

	// downward flow has priority: fall into air below (falling water column)
	bool below_air = has_below && water_replaceable(below.type);
	if(below_air)
		water_try_set(s, x, y - 1, z, BLOCK_WATER_FLOW, 0x8);

	// Horizontal spread only happens on SOLID ground. Over air the water falls;
	// over water (a lake) it must go straight down into it like a hole, NOT keep
	// stair-stepping across the surface -- water is not a solid floor.
	bool below_solid = has_below && !below_air && !water_is_water(below.type);
	if(!below_solid || out_level > WATER_MAX_LEVEL)
		return;

	// Flow-weight pathfinding: prefer directions that lead to the nearest hole
	// within FLOW_SEARCH steps; if none is in range, flow to every open side.
	// Costs are also evaluated THROUGH flowing water (an existing stream), so a
	// direction whose air neighbour has been filled still counts and the source
	// keeps feeding the ravine instead of flooding sideways.
	int cost[4];
	int min_cost = WATER_COST_NONE;
	struct block_data nb_data[4];
	bool nb_ok[4];
	for(int i = 0; i < 4; i++) {
		cost[i] = WATER_COST_NONE;
		nb_ok[i] = server_world_get_block(AWORLD(s), x + hx[i], y, z + hz[i],
										  &nb_data[i]);
		if(!nb_ok[i] || !water_search_passable(nb_data[i].type))
			continue;
		struct block_data nb;
		if(server_world_get_block(AWORLD(s), x + hx[i], y - 1, z + hz[i], &nb)
		   && water_below_is_hole(nb.type)) {
			cost[i] = 0; // hole directly below this neighbour
		} else {
			cost[i] = water_drop_search(s, x + hx[i], y, z + hz[i],
										FLOW_SEARCH - 1, i);
		}
		if(cost[i] < min_cost)
			min_cost = cost[i];
	}

	for(int i = 0; i < 4; i++) {
		if(!nb_ok[i])
			continue;
		if(water_replaceable(nb_data[i].type)) {
			// place only into open (air) cells, and only toward the nearest
			// hole; if no hole is in range anywhere, flow to every open side
			if(min_cost == WATER_COST_NONE || cost[i] == min_cost)
				water_try_set(s, x + hx[i], y, z + hz[i], BLOCK_WATER_FLOW,
							  out_level);
		} else if(nb_data[i].type == BLOCK_WATER_FLOW
				  && !(nb_data[i].metadata & 0x8)
				  && nb_data[i].metadata > out_level) {
			// a weaker existing flow gets pulled up to our (stronger) level
			water_try_set(s, x + hx[i], y, z + hz[i], BLOCK_WATER_FLOW,
						  out_level);
		}
	}
}

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	return 0;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	#ifdef GFX_FANCY_LIQUIDS
	int block_height = (this->block->metadata & 0x8) ?
		16 :
		(8 - this->block->metadata) * 2 * 7 / 8;
	switch(side) {
		case SIDE_TOP:
			return (it->block->type == this->block->type) ?
				face_occlusion_full() :
				face_occlusion_empty();
		case SIDE_BOTTOM: return face_occlusion_full();
		default: return face_occlusion_rect(block_height);
	}
	#else
	if (gstate.in_water) {
		return (it->block->type == this->block->type) ?
			face_occlusion_full() :
			face_occlusion_empty();
	} else return face_occlusion_full();
	#endif
}

static uint16_t getTextureIndex1(struct block_info* this, enum side side) {
	#ifdef GFX_FANCY_LIQUIDS
	return TEXTURE_INDEX(1, 0);
	#else
	return tex_atlas_lookup(TEXAT_WATER_STATIC);
	#endif
}

static uint16_t getTextureIndex2(struct block_info* this, enum side side) {
	#ifdef GFX_FANCY_LIQUIDS
	return TEXTURE_INDEX(5, 0);
	#else
	return tex_atlas_lookup(TEXAT_WATER_STATIC);
	#endif
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	return 0;
}

struct block block_water_still = {
	.name = "Water",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex1,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	#ifdef GFX_FANCY_LIQUIDS
	.transparent = true,
	.renderBlock = render_block_fluid,
	#else
	.transparent = false,
	.renderBlock = render_block_full,
	#endif
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 3,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = true,
	.digging.hardness = 150000,
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

struct block block_water_flowing = {
	.name = "Water",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex2,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	#ifdef GFX_FANCY_LIQUIDS
	.transparent = true,
	.renderBlock = render_block_fluid,
	#else
	.transparent = false,
	.renderBlock = render_block_full,
	#endif
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 3,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = true,
	.digging.hardness = 150000,
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
