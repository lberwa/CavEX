/*
	Copyright (c) 2026
*/

#include <math.h>

#include "../network/server_local.h"
#include "../network/server_world.h"
#include "blocks.h"

#define GATE_FACING_MASK 0x03
#define GATE_OPEN 0x04

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_WOOD;
}

static bool fence_gate_axis_is_z(uint8_t meta) {
	const uint8_t facing = meta & GATE_FACING_MASK;
	return facing == 0 || facing == 2;
}

static size_t fence_gate_boxes(struct block_info* this, struct AABB* x,
							   bool open, bool axis_z) {
	(void)this;
	const float post_t = 2.0F / 16.0F;
	const float post_z0 = 6.0F / 16.0F;
	const float post_z1 = 10.0F / 16.0F;
	const float leaf_t = 2.0F / 16.0F;
	// Match fence rail heights (see render_block_fence_always):
	// 6/16..9/16 and 12/16..15/16
	const float leaf_y0a = 6.0F / 16.0F;
	const float leaf_y1a = 9.0F / 16.0F;
	const float leaf_y0b = 12.0F / 16.0F;
	const float leaf_y1b = 15.0F / 16.0F;
	// Match fence posts: full block height (render is still within 0..1).
	const float post_y0 = 0.0F;
	const float post_y1 = 1.0F;

	size_t count = 0;

	// Posts are always present.
	if(axis_z) {
		if(x) {
			x[count++] = (struct AABB) {0.0F, post_y0, post_z0, post_t, post_y1, post_z1};
			x[count++] = (struct AABB) {1.0F - post_t, post_y0, post_z0, 1.0F, post_y1, post_z1};
		} else {
			count += 2;
		}
	} else {
		if(x) {
			x[count++] = (struct AABB) {post_z0, post_y0, 0.0F, post_z1, post_y1, post_t};
			x[count++] = (struct AABB) {post_z0, post_y0, 1.0F - post_t, post_z1, post_y1, 1.0F};
		} else {
			count += 2;
		}
	}

	// Leaf (two bars). When "open" we just rotate the leaf 90° (visual-only).
	const bool leaf_axis_z = open ? !axis_z : axis_z;
	const float mid_y0 = leaf_y1a;
	const float mid_y1 = leaf_y0b;
	const float mid0 = 124.0F / 256.0F;
	const float mid1 = 132.0F / 256.0F;

	if(leaf_axis_z) {
		const float z0 = 7.0F / 16.0F;
		const float z1 = 9.0F / 16.0F;
		if(x) {
			x[count++] = (struct AABB) {post_t, leaf_y0a, z0, 1.0F - post_t,
										leaf_y1a, z1};
			x[count++] = (struct AABB) {post_t, leaf_y0b, z0, 1.0F - post_t,
										leaf_y1b, z1};
		} else {
			count += 2;
		}
	} else {
		const float x0 = 7.0F / 16.0F;
		const float x1 = 9.0F / 16.0F;
		if(x) {
			x[count++] = (struct AABB) {x0, leaf_y0a, post_t, x1, leaf_y1a,
										1.0F - post_t};
			x[count++] = (struct AABB) {x0, leaf_y0b, post_t, x1, leaf_y1b,
										1.0F - post_t};
		} else {
			count += 2;
		}
	}

	// Center piece only when closed: between the two rails.
	if(!open) {
		if(axis_z) {
			if(x)
				x[count] = (struct AABB) {mid0, mid_y0, 7.0F / 16.0F, mid1, mid_y1, 9.0F / 16.0F};
			count++;
		} else {
			if(x)
				x[count] = (struct AABB) {7.0F / 16.0F, mid_y0, mid0, 9.0F / 16.0F, mid_y1, mid1};
			count++;
		}
	}

	return count;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	const uint8_t meta = this->block->metadata;
	const bool open = (meta & GATE_OPEN) != 0;

	// When open: no collision, but keep selection.
	if(entity && open)
		return 0;

	// When closed: match fence collision height so you can't jump over it.
	if(entity) {
		if(x)
			aabb_setsize(x, 1.0F, 1.5F, 1.0F);
		return 1;
	}

	const bool axis_z = fence_gate_axis_is_z(meta);
	return fence_gate_boxes(this, x, open, axis_z);
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
	// requested: build from (4,0) like fence (planks)
	return tex_atlas_lookup(TEXAT_PLANKS);
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	(void)it;
	(void)where;
	(void)on_side;

	struct block_data cur = *on->block;
	cur.metadata ^= GATE_OPEN;
	server_world_set_block(s, on->x, on->y, on->z, cur);
	notifyNeighbours(s, on->x, on->y, on->z);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)on;
	(void)on_side;

	int metadata = 0;
	const uint8_t pid = s->active_player_id;
	const double dx = s->players[pid].x - (where->x + 0.5);
	const double dz = s->players[pid].z - (where->z + 0.5);

	if(fabs(dx) > fabs(dz)) {
		metadata = (dx >= 0) ? 3 : 1;
	} else {
		metadata = (dz >= 0) ? 0 : 2;
	}

	struct block_data blk = (struct block_data) {
		.type = it->id,
		.metadata = metadata & GATE_FACING_MASK,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk_info = *where;
	blk_info.block = &blk;

	if(entity_local_player_block_collide(
		   (vec3) {s->players[pid].x, s->players[pid].y, s->players[pid].z},
		   &blk_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk);
	return true;
}

struct block block_fence_gate = {
	.name = "Fence Gate",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = NULL,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_fence_gate,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = true,
	.place_ignore = false,
	.digging.hardness = 3000,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.fuel = 1,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
