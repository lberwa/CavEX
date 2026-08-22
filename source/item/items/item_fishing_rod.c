/*
	Copyright (c) 2026 ByteBit/xtreme8000

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

#include "../../entity/entity.h"
#include "../../game/game_state.h"
#include "../../graphics/render_item.h"
#include "../../item/items.h"

// Check whether the local player currently has a fishing hook in the world.
static bool local_player_has_hook(void) {
	struct entity* lp = gstate.local_player;
	if(!lp)
		return false;
	uint32_t owner_key = (uint32_t)(lp->id + 1);
	dict_entity_it_t it;
	dict_entity_it(it, gstate.entities);
	while(!dict_entity_end_p(it)) {
		struct entity* e = dict_entity_ref(it)->value;
		if(e && e->type == ENTITY_FISHING_HOOK
		   && e->data.fishing_hook.owner_id == owner_key)
			return true;
		dict_entity_next(it);
	}
	return false;
}

// Custom renderer: switch to the "cast" texture (5,5) while a hook is out.
static void render_fishing_rod(struct item* item, struct item_data* stack,
								mat4 view, bool fullbright,
								enum render_item_env env) {
	bool casting = local_player_has_hook();

	uint8_t orig_tx = item->render_data.item.texture_x;
	uint8_t orig_ty = item->render_data.item.texture_y;

	if(casting) {
		item->render_data.item.texture_x = 5;
		item->render_data.item.texture_y = 5;
	}

	render_item_flat(item, stack, view, fullbright, env);

	item->render_data.item.texture_x = orig_tx;
	item->render_data.item.texture_y = orig_ty;
}

struct item item_fishing_rod = {
	.name        = "Fishing Rod",
	.has_damage  = true,
	.max_damage  = 64,
	.max_stack   = 1,
	.fuel        = 0,
	.renderItem  = render_fishing_rod,
	.onItemPlace = NULL,
	.armor.is_armor = false,
	.tool.type   = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 5,
			.texture_y = 4,
		},
	},
};
