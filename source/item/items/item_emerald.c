/*
	Copyright (c) 2026
*/

#include "../../graphics/render_item.h"
#include "../../network/server_local.h"

struct item item_emerald = {
	.name = "Emerald",
	.has_damage = false,
	.max_stack = 64,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = NULL,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 7,
			.texture_y = 10,
		},
	},
};
