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

#include "../../network/server_local.h"
#include "../../network/client_interface.h"
#include "../../network/server_interface.h"
#include "../../block/blocks.h"

// "Drink" the milk on right-click: swap the held stack for an empty bucket.
// Modelled on item_bucket_water.c's slot swap, but places nothing into the
// world. Returns false so the engine does NOT additionally consume the item.
static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)where;
	(void)on;
	(void)on_side;
	if(!s || !it)
		return false;

	// Swap the actually-held hotbar slot to an empty bucket.
	const uint8_t pid = s->active_player_id;
	struct inventory* inv = &s->players[pid].inventory;
	const size_t hotbar_rel = inventory_get_hotbar(inv);          // 0..8
	const size_t slot_abs = INVENTORY_SLOT_HOTBAR + hotbar_rel;   // absolute index

	const struct item_data new_it
		= {.id = ITEM_BUCKET, .count = 1, .durability = 0};
	inventory_set_slot(inv, slot_abs, new_it);

	// Notify client for this slot.
	set_inv_slot_t changes;
	set_inv_slot_init(changes);
	set_inv_slot_push(changes, slot_abs);
	server_local_send_inv_changes(pid, changes, inv, WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);

	// Mirror local copy.
	*it = new_it;

	return false;
}

struct item item_milk_bucket = {
	.name = "Milk",
	.has_damage = false,
	.max_damage = 0,
	.max_stack = 1,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 13,
			.texture_y = 4,
		},
	},
};
