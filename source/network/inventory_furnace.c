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

#include "../item/recipe.h"
#include "../item/window_container.h"
#include "client_interface.h"
#include "inventory_logic.h"
#include "server_local.h"

struct furnace_data* furnace_data_get(struct server_local* s, w_coord_t x,
									  w_coord_t y, w_coord_t z, bool create) {
	for(int i = 0; i < MAX_FURNACES; i++) {
		if(s->furnaces[i].pos.x == x && s->furnaces[i].pos.y == y
		   && s->furnaces[i].pos.z == z)
			return &s->furnaces[i];
	}

	if(!create)
		return NULL;

	for(int i = 0; i < MAX_FURNACES; i++) {
		if(s->furnaces[i].pos.x != -1)
			continue;

		s->furnaces[i].pos.x = x;
		s->furnaces[i].pos.y = y;
		s->furnaces[i].pos.z = z;
		for(int k = 0; k < FURNACE_SIZE_STORAGE; k++) {
			s->furnaces[i].items[k].id = 0;
			s->furnaces[i].items[k].durability = 0;
			s->furnaces[i].items[k].count = 0;
		}
		s->furnaces[i].burn_time = 0;
		s->furnaces[i].burn_total = 0;
		s->furnaces[i].cook_time = 0;
		s->furnaces[i].cook_total = 0;
		return &s->furnaces[i];
	}

	return NULL;
}

struct item_data furnace_recipe_result(struct item_data input) {
	switch(input.id) {
		case ITEM_CLAY_BALL:
			return (struct item_data) {.id = ITEM_BRICK, .durability = 0, .count = 1};
		case BLOCK_CACTUS:
			return (struct item_data) {.id = ITEM_DYE, .durability = 2, .count = 1};
		case BLOCK_LOG:
			return (struct item_data) {.id = ITEM_COAL, .durability = 1, .count = 1};
		case BLOCK_COBBLESTONE:
			return (struct item_data) {.id = BLOCK_STONE, .durability = 0, .count = 1};
		case BLOCK_DIAMOND_ORE:
			return (struct item_data) {.id = ITEM_DIAMOND, .durability = 0, .count = 1};
		case BLOCK_SAND:
			return (struct item_data) {.id = BLOCK_GLASS, .durability = 0, .count = 1};
		case ITEM_FISH:
			return (struct item_data) {.id = ITEM_FISH_COOKED, .durability = 0, .count = 1};
		case BLOCK_GOLD_ORE:
			return (struct item_data) {.id = ITEM_GOLD, .durability = 0, .count = 1};
		case BLOCK_IRON_ORE:
			return (struct item_data) {.id = ITEM_IRON, .durability = 0, .count = 1};
		case ITEM_PORKCHOP:
			return (struct item_data) {.id = ITEM_PORKCHOP_COOKED, .durability = 0, .count = 1};
		default:
			return (struct item_data) {.id = 0, .durability = 0, .count = 0};
	}
}

void furnace_send_updates(struct server_local* s, w_coord_t x, w_coord_t y,
						  w_coord_t z, bool send_slots, bool send_state) {
	struct furnace_data* furnace = furnace_data_get(s, x, y, z, false);
	if(!furnace)
		return;

	for(uint8_t pid = 0; pid < MAX_SERVER_PLAYERS; pid++) {
		struct server_player* player = &s->players[pid];
		struct inventory* inv = player->active_inventory;
		if(!inv || inv == &player->inventory || inv->logic != &inventory_logic_furnace)
			continue;

		if(inv->x != x || inv->y != y || inv->z != z)
			continue;

		if(send_slots) {
			for(size_t slot = 0; slot < FURNACE_SIZE_STORAGE; slot++) {
				inv->items[slot] = furnace->items[slot];
				clin_rpc_send(&(struct client_rpc) {
					CRPC_PLAYER_ID(pid)
					.type = CRPC_INVENTORY_SLOT,
					.payload.inventory_slot.window = WINDOWC_FURNACE,
					.payload.inventory_slot.slot = slot,
					.payload.inventory_slot.item = furnace->items[slot],
				});
			}
		}

		if(send_state) {
			clin_rpc_send(&(struct client_rpc) {
				CRPC_PLAYER_ID(pid)
				.type = CRPC_FURNACE_STATE,
				.payload.furnace_state.window = WINDOWC_FURNACE,
				.payload.furnace_state.burn_time = furnace->burn_time,
				.payload.furnace_state.burn_total = furnace->burn_total,
				.payload.furnace_state.cook_time = furnace->cook_time,
				.payload.furnace_state.cook_total = furnace->cook_total,
			});
		}
	}
}

static bool inv_pre_action(struct inventory* inv, size_t slot, bool right,
						   set_inv_slot_t changes) {
	if(slot == FURNACE_SLOT_OUTPUT) {
		struct item_data output;
		if(!right && inventory_get_slot(inv, FURNACE_SLOT_OUTPUT, &output)) {
			bool took = false;
			bool default_action;

			struct item_data picked;
			if(inventory_get_picked_item(inv, &picked)) {
				struct item* it_type = item_get(&picked);
				if(it_type && picked.id == output.id
				   && picked.durability == output.durability
				   && picked.count + output.count <= it_type->max_stack) {
					picked.count += output.count;
					inventory_set_picked_item(inv, picked);
					set_inv_slot_push(changes, SPECIAL_SLOT_PICKED_ITEM);
					default_action = false;
					took = true;
				}
			} else {
				default_action = true;
				took = true;
			}

			if(took)
				return default_action;
		}

		if(!inventory_get_slot(inv, FURNACE_SLOT_OUTPUT, &output))
			return false;

		if(right)
			return false;

		return false;
	}

	return true;
}

static void inv_post_action(struct inventory* inv, size_t slot, bool right,
							bool accepted, set_inv_slot_t changes) {
	struct server_local* s = inv->user;
	struct furnace_data* furnace
		= furnace_data_get(s, inv->x, inv->y, inv->z, true);
	if(!furnace)
		return;

	if(slot < FURNACE_SIZE_STORAGE) {
		set_inv_slot_push(changes, slot);
		set_inv_slot_push(changes, SPECIAL_SLOT_PICKED_ITEM);
	}

	if(slot == FURNACE_SLOT_OUTPUT && !right && accepted) {
		inventory_clear_slot(inv, FURNACE_SLOT_OUTPUT);
		set_inv_slot_push(changes, FURNACE_SLOT_OUTPUT);
		set_inv_slot_push(changes, SPECIAL_SLOT_PICKED_ITEM);
	}

	for(size_t k = 0; k < FURNACE_SIZE_STORAGE; k++)
		furnace->items[k] = inv->items[k];

	struct item_data result = furnace_recipe_result(
		furnace->items[FURNACE_SLOT_INPUT]);
	if(result.id == 0) {
		inventory_clear_slot(inv, FURNACE_SLOT_OUTPUT);
		furnace->items[FURNACE_SLOT_OUTPUT].id = 0;
		furnace->items[FURNACE_SLOT_OUTPUT].durability = 0;
		furnace->items[FURNACE_SLOT_OUTPUT].count = 0;
		furnace->cook_time = 0;
	} else {
		inventory_set_slot(inv, FURNACE_SLOT_OUTPUT, furnace->items[FURNACE_SLOT_OUTPUT]);
	}

	furnace_send_updates(s, inv->x, inv->y, inv->z, true, true);
}

static void inv_on_close(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	struct furnace_data* furnace
		= furnace_data_get(s, inv->x, inv->y, inv->z, true);

	if(furnace) {
		for(size_t k = 0; k < FURNACE_SIZE_STORAGE; k++)
			furnace->items[k] = inv->items[k];
	}

	struct item_data picked_item;
	if(inventory_get_picked_item(inv, &picked_item)) {
		inventory_clear_picked_item(inv);
		server_local_spawn_item((vec3) {player->x, player->y, player->z},
								&picked_item, true, s);
	}

	inventory_destroy(inv);
}

static bool inv_on_collect(struct inventory* inv, struct item_data* item) {
	struct server_local* s = inv->user;
	uint8_t priorities[INVENTORY_SIZE_HOTBAR + INVENTORY_SIZE_MAIN];

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++)
		priorities[k] = k + FURNACE_SLOT_HOTBAR;

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++)
		priorities[k + INVENTORY_SIZE_HOTBAR] = k + FURNACE_SLOT_MAIN;

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	bool success
		= inventory_collect(inv, item, priorities,
							sizeof(priorities) / sizeof(*priorities), changes);
	server_local_send_inv_changes(s->active_player_id, changes, inv,
								  WINDOWC_FURNACE);
	set_inv_slot_clear(changes);

	return success;
}

static void inv_on_create(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	struct furnace_data* furnace
		= furnace_data_get(s, inv->x, inv->y, inv->z, true);

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		inv->items[k + FURNACE_SLOT_HOTBAR]
			= player->inventory.items[k + INVENTORY_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + FURNACE_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		inv->items[k + FURNACE_SLOT_MAIN]
			= player->inventory.items[k + INVENTORY_SLOT_MAIN];
		set_inv_slot_push(changes, k + FURNACE_SLOT_MAIN);
	}

	if(furnace) {
		for(size_t k = 0; k < FURNACE_SIZE_STORAGE; k++) {
			inv->items[k] = furnace->items[k];
			set_inv_slot_push(changes, k);
		}
	}

	server_local_send_inv_changes(pid, changes, inv, WINDOWC_FURNACE);
	set_inv_slot_clear(changes);

	if(furnace) {
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(pid)
			.type = CRPC_FURNACE_STATE,
			.payload.furnace_state.window = WINDOWC_FURNACE,
			.payload.furnace_state.burn_time = furnace->burn_time,
			.payload.furnace_state.burn_total = furnace->burn_total,
			.payload.furnace_state.cook_time = furnace->cook_time,
			.payload.furnace_state.cook_total = furnace->cook_total,
		});
	}
}

static bool inv_on_destroy(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		player->inventory.items[k + INVENTORY_SLOT_HOTBAR]
			= inv->items[k + FURNACE_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		player->inventory.items[k + INVENTORY_SLOT_MAIN]
			= inv->items[k + FURNACE_SLOT_MAIN];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_MAIN);
	}

	server_local_send_inv_changes(pid, changes, &player->inventory,
								  WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);
	return true;
}

struct inventory_logic inventory_logic_furnace = {
	.pre_action = inv_pre_action,
	.post_action = inv_post_action,
	.on_collect = inv_on_collect,
	.on_create = inv_on_create,
	.on_destroy = inv_on_destroy,
	.on_close = inv_on_close,
};
