/*
	Copyright (c) 2026
*/

#include "../network/server_local.h"
#include "../network/server_world.h"
#include "blocks.h"

#define VINE_FRONT 0x01
#define VINE_BACK 0x02
#define VINE_LEFT 0x04
#define VINE_RIGHT 0x08

static bool vine_can_attach_to(struct block_data nb) {
	if(nb.type == BLOCK_AIR)
		return false;
	if(!blocks[nb.type])
		return false;
	return !blocks[nb.type]->can_see_through;
}

static bool vine_has_any_horizontal(uint8_t meta) {
	return (meta & (VINE_FRONT | VINE_BACK | VINE_LEFT | VINE_RIGHT)) != 0;
}

static uint8_t vine_filter_supported_meta(struct server_local* s, w_coord_t x,
										 w_coord_t y, w_coord_t z,
										 uint8_t meta) {
	struct block_data nb;

	if((meta & VINE_LEFT)
	   && (!server_world_get_block(&s->world, x - 1, y, z, &nb)
		   || !vine_can_attach_to(nb)))
		meta &= ~VINE_LEFT;
	if((meta & VINE_RIGHT)
	   && (!server_world_get_block(&s->world, x + 1, y, z, &nb)
		   || !vine_can_attach_to(nb)))
		meta &= ~VINE_RIGHT;
	if((meta & VINE_FRONT)
	   && (!server_world_get_block(&s->world, x, y, z - 1, &nb)
		   || !vine_can_attach_to(nb)))
		meta &= ~VINE_FRONT;
	if((meta & VINE_BACK)
	   && (!server_world_get_block(&s->world, x, y, z + 1, &nb)
		   || !vine_can_attach_to(nb)))
		meta &= ~VINE_BACK;

	return meta;
}

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_ORGANIC;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	// No collision (entities pass through).
	if(entity)
		return 0;

	const uint8_t meta = this->block->metadata;
	size_t count = 0;
	const float t = 0.0625F;

	if(meta & VINE_LEFT) {
		if(x)
			x[count] = (struct AABB) {0.0F, 0.0F, 0.0F, t, 1.0F, 1.0F};
		count++;
	}
	if(meta & VINE_RIGHT) {
		if(x)
			x[count] = (struct AABB) {1.0F - t, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
		count++;
	}
	if(meta & VINE_FRONT) {
		if(x)
			x[count] = (struct AABB) {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, t};
		count++;
	}
	if(meta & VINE_BACK) {
		if(x)
			x[count] = (struct AABB) {0.0F, 0.0F, 1.0F - t, 1.0F, 1.0F, 1.0F};
		count++;
	}

	if(count == 0) {
		if(x)
			x[0] = (struct AABB) {0.4375F, 0.0F, 0.4375F, 0.5625F, 1.0F, 0.5625F};
		return 1;
	}

	return count;
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
	return tex_atlas_lookup(TEXAT_VINE);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	if(it) {
		it->id = BLOCK_VINE;
		it->durability = 0;
		it->count = 1;
	}

	(void)this;
	(void)g;
	(void)s;
	return 1;
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	if(!on || !on->block)
		return false;

	if(!vine_can_attach_to(*on->block))
		return false;

	uint8_t meta = 0;
	switch(on_side) {
		// like ladder: attach to the opposite face
		case SIDE_LEFT: meta = VINE_RIGHT; break;
		case SIDE_RIGHT: meta = VINE_LEFT; break;
		case SIDE_FRONT: meta = VINE_BACK; break;
		case SIDE_BACK: meta = VINE_FRONT; break;
		default: return false;
	}

	// Allow multiple vine faces on the same block position by OR-ing metadata.
	struct block_data existing;
	if(!server_world_get_block(&s->world, where->x, where->y, where->z, &existing))
		return false;

	if(existing.type == BLOCK_VINE) {
		const uint8_t new_meta = existing.metadata | meta;
		if(new_meta != existing.metadata) {
			existing.metadata = new_meta;
			server_world_set_block(s, where->x, where->y, where->z, existing);
		}
		return true;
	}

	server_world_set_block(s, where->x, where->y, where->z,
						   (struct block_data) {
							   .type = it->id,
							   .metadata = meta,
							   .sky_light = 0,
							   .torch_light = 0,
						   });
	return true;
}

static void onNeighbourBlockChange(struct server_local* s, struct block_info* info) {
	if(!info->neighbours)
		return;

	struct block_data cur = *info->block;
	uint8_t meta = cur.metadata;

	if(meta & VINE_LEFT) {
		if(!vine_can_attach_to(info->neighbours[SIDE_LEFT]))
			meta &= ~VINE_LEFT;
	}
	if(meta & VINE_RIGHT) {
		if(!vine_can_attach_to(info->neighbours[SIDE_RIGHT]))
			meta &= ~VINE_RIGHT;
	}
	if(meta & VINE_FRONT) {
		if(!vine_can_attach_to(info->neighbours[SIDE_FRONT]))
			meta &= ~VINE_FRONT;
	}
	if(meta & VINE_BACK) {
		if(!vine_can_attach_to(info->neighbours[SIDE_BACK]))
			meta &= ~VINE_BACK;
	}

	const bool supported_from_above = info->neighbours[SIDE_TOP].type == BLOCK_VINE;

	if(!vine_has_any_horizontal(meta) && !supported_from_above) {
		server_world_set_block(s, info->x, info->y, info->z,
							   (struct block_data) {.type = BLOCK_AIR});
		return;
	}

	if(meta != cur.metadata) {
		cur.metadata = meta;
		server_world_set_block(s, info->x, info->y, info->z, cur);
	}
}

static void onRandomTick(struct server_local* s, struct block_info* this) {
	// 25% chance per random tick
	if((rand() % 4) != 0)
		return;

	// Choose one horizontal direction (N/E/S/W)
	static const enum side dirs[4] = {SIDE_FRONT, SIDE_RIGHT, SIDE_BACK, SIDE_LEFT};
	const enum side dir = dirs[rand() % 4];

	int dx = 0, dy = 0, dz = 0;
	blocks_side_offset(dir, &dx, &dy, &dz);

	struct block_data cur = *this->block;
	const uint8_t dir_bit =
		dir == SIDE_FRONT ? VINE_FRONT :
		dir == SIDE_BACK ? VINE_BACK :
		dir == SIDE_LEFT ? VINE_LEFT :
		dir == SIDE_RIGHT ? VINE_RIGHT :
		0;

	// Try to grow down (most common): if below is air, 50% chance.
	struct block_data below;
	if(server_world_get_block(&s->world, this->x, this->y - 1, this->z, &below)
	   && below.type == BLOCK_AIR) {
		if((rand() & 1) == 0) {
			// Special case requested: when growing downward, the new vine may
			// hang purely from the vine above (no wall needed). Therefore we
			// keep the parent's orientation bits.
			const uint8_t down_meta = cur.metadata;
			server_world_set_block(s, this->x, this->y - 1, this->z,
								   (struct block_data) {
									   .type = BLOCK_VINE,
									   .metadata = down_meta,
									   .sky_light = 0,
									   .torch_light = 0,
								   });
			return;
		}
	}

	// Try to grow to the side in the chosen direction (attach to neighbour block).
	struct block_data target;
	if(server_world_get_block(&s->world, this->x + dx, this->y, this->z + dz, &target)
	   && (target.type == BLOCK_AIR || target.type == BLOCK_VINE)) {
		// Prefer growing "along the wall": keep the same attachment direction(s)
		// as the vine that caused the growth.
		// Important: keep EXACT orientation bits from the source vine,
		// but only if they are actually supported at the new position.
		const uint8_t side_meta = vine_filter_supported_meta(
			s, this->x + dx, this->y, this->z + dz, cur.metadata);
		if(vine_has_any_horizontal(side_meta)) {
			const uint8_t merged_meta =
				(target.type == BLOCK_VINE) ? (target.metadata | side_meta) : side_meta;
			server_world_set_block(s, this->x + dx, this->y, this->z + dz,
								   (struct block_data) {
									   .type = BLOCK_VINE,
									   .metadata = merged_meta,
									   .sky_light = 0,
									   .torch_light = 0,
								   });
			return;
		}
		// If we can't keep the same orientation, we do NOT grow sideways here.
		// (This avoids creating a vine that "switches wall" unexpectedly.)
	}

	// Try to spread "around the corner" from an existing attachment.
	// If we already have vines on this chosen side, try to copy that side onto
	// a perpendicular wall.
	if(dir_bit && (cur.metadata & dir_bit)) {
		const enum side perp_a = (dir == SIDE_FRONT || dir == SIDE_BACK) ? SIDE_LEFT : SIDE_FRONT;
		const enum side perp_b = (dir == SIDE_FRONT || dir == SIDE_BACK) ? SIDE_RIGHT : SIDE_BACK;
		const enum side perps[2] = {perp_a, perp_b};

		for(int i = 0; i < 2; i++) {
			int pdx = 0, pdy = 0, pdz = 0;
			blocks_side_offset(perps[i], &pdx, &pdy, &pdz);
			struct block_data corner_air;
			struct block_data corner_support;
			if(!server_world_get_block(&s->world, this->x + pdx, this->y, this->z + pdz, &corner_air))
				continue;
			if(corner_air.type != BLOCK_AIR)
				continue;
			if(!server_world_get_block(&s->world, this->x + pdx + dx, this->y, this->z + pdz + dz, &corner_support))
				continue;
			if(!vine_can_attach_to(corner_support))
				continue;

			const uint8_t corner_bit =
				perps[i] == SIDE_FRONT ? VINE_FRONT :
				perps[i] == SIDE_BACK ? VINE_BACK :
				perps[i] == SIDE_LEFT ? VINE_LEFT :
				perps[i] == SIDE_RIGHT ? VINE_RIGHT :
				0;
			if(!corner_bit)
				continue;

			server_world_set_block(s, this->x + pdx, this->y, this->z + pdz,
								   (struct block_data) {
									   .type = BLOCK_VINE,
									   .metadata = corner_bit,
									   .sky_light = 0,
									   .torch_light = 0,
								   });
			return;
		}
	}

	// Try to grow up only if there is a solid support on the chosen side above.
	struct block_data above;
	struct block_data above_support;
	if(server_world_get_block(&s->world, this->x, this->y + 1, this->z, &above)
	   && above.type == BLOCK_AIR
	   && server_world_get_block(&s->world, this->x + dx, this->y + 1, this->z + dz, &above_support)
	   && vine_can_attach_to(above_support)
	   && dir_bit) {
		server_world_set_block(s, this->x, this->y + 1, this->z,
							   (struct block_data) {
								   .type = BLOCK_VINE,
								   .metadata = dir_bit,
								   .sky_light = 0,
								   .torch_light = 0,
							   });
	}
}

struct block block_vine = {
	.name = "Vine",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = onRandomTick,
	.onRightClick = NULL,
	.onWorldTick = NULL,
	.onNeighbourBlockChange = onNeighbourBlockChange,
	.onDay = NULL,
	.onNight = NULL,
	.transparent = false,
	.renderBlock = render_block_vine,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = true,
	.place_ignore = false,
	.digging.hardness = 200,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.fuel = 1,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
