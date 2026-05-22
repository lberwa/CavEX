/*
	Copyright (c) 2026
*/

#include "../../network/server_local.h"
#include "../../network/client_interface.h"
#include "../../network/server_interface.h"

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	const uint8_t pid = s->active_player_id;
	(void)it;
	(void)where;
	(void)on;
	(void)on_side;

	if(s->players[pid].health >= MAX_PLAYER_HEALTH)
		return false;

	server_local_set_player_health(
		s, pid, s->players[pid].health + 2 * HEALTH_PER_HEART);
	return true;
}

struct item item_melon = {
	.name = "Melon Slice",
	.has_damage = false,
	.max_stack = 64,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 13,
			.texture_y = 7,
		},
	},
};
