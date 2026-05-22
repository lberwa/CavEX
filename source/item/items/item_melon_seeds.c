/*
	Copyright (c) 2026
*/

#include "../../block/blocks.h"
#include "../../network/server_local.h"
#include "../../network/server_world.h"

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)it;
	(void)where;

	if(!on || !on->block)
		return false;
	if(on->block->type != BLOCK_FARMLAND || on_side != SIDE_TOP)
		return false;

	const int tx = on->x;
	const int ty = on->y + 1;
	const int tz = on->z;
	struct block_data blk;

	if(!server_world_get_block(&s->world, tx, ty, tz, &blk))
		return false;
	if(blk.type != BLOCK_AIR)
		return false;

	server_world_set_block(s, tx, ty, tz, (struct block_data) {
		.type = BLOCK_MELON_STEM,
		.metadata = 0,
		.sky_light = 0,
		.torch_light = 0,
	});
	return true;
}

struct item item_melon_seeds = {
	.name = "Melon Seeds",
	.has_damage = false,
	.max_stack = 64,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 3,
			.texture_y = 7,
		},
	},
};
