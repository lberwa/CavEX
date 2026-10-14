/* Inventory logic for the six-slot dispenser screen. */
#include "inventory_logic.h"
#include "server_local.h"
#include "../item/window_container.h"

static bool inv_pre_action(struct inventory* inv, size_t slot, bool right,
						   set_inv_slot_t changes) {
	return true;
}

static void inv_post_action(struct inventory* inv, size_t slot, bool right,
							bool accepted, set_inv_slot_t changes) {
}

static void inv_on_close(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	for(int i = 0; i < MAX_DISPENSERS; i++) {
		if(s->dispenser_pos[i].x == inv->x && s->dispenser_pos[i].y == inv->y
		   && s->dispenser_pos[i].z == inv->z) {
			for(size_t k = 0; k < DISPENSER_SIZE_STORAGE; k++)
				s->dispenser_items[i][k] = inv->items[DISPENSER_SLOT_STORAGE + k];
			break;
		}
	}

	struct item_data picked;
	if(inventory_get_picked_item(inv, &picked)) {
		inventory_clear_picked_item(inv);
		set_inv_slot_push(changes, SPECIAL_SLOT_PICKED_ITEM);
		server_local_spawn_item((vec3) {player->x, player->y, player->z},
							&picked, true, s);
	}

	server_local_send_inv_changes(pid, changes, inv, WINDOWC_DISPENSER);
	set_inv_slot_clear(changes);
	inventory_destroy(inv);
}

static bool inv_on_collect(struct inventory* inv, struct item_data* item) {
	struct server_local* s = inv->user;
	uint8_t priorities[INVENTORY_SIZE_HOTBAR + INVENTORY_SIZE_MAIN];
	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++)
		priorities[k] = k + DISPENSER_SLOT_HOTBAR;
	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++)
		priorities[k + INVENTORY_SIZE_HOTBAR] = k + DISPENSER_SLOT_MAIN;
	set_inv_slot_t changes;
	set_inv_slot_init(changes);
	bool success = inventory_collect(inv, item, priorities,
								 sizeof(priorities) / sizeof(*priorities), changes);
	server_local_send_inv_changes(s->active_player_id, changes, inv,
								  WINDOWC_DISPENSER);
	set_inv_slot_clear(changes);
	return success;
}

static void inv_on_create(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	set_inv_slot_t changes;
	set_inv_slot_init(changes);
	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		inv->items[k + DISPENSER_SLOT_HOTBAR] =
			player->inventory.items[k + INVENTORY_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + DISPENSER_SLOT_HOTBAR);
	}
	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		inv->items[k + DISPENSER_SLOT_MAIN] =
			player->inventory.items[k + INVENTORY_SLOT_MAIN];
		set_inv_slot_push(changes, k + DISPENSER_SLOT_MAIN);
	}
	for(int i = 0; i < MAX_DISPENSERS; i++) {
		if(s->dispenser_pos[i].x == inv->x && s->dispenser_pos[i].y == inv->y
		   && s->dispenser_pos[i].z == inv->z) {
			for(size_t k = 0; k < DISPENSER_SIZE_STORAGE; k++) {
				inv->items[DISPENSER_SLOT_STORAGE + k] = s->dispenser_items[i][k];
				set_inv_slot_push(changes, DISPENSER_SLOT_STORAGE + k);
			}
			break;
		}
	}
	server_local_send_inv_changes(pid, changes, inv, WINDOWC_DISPENSER);
	set_inv_slot_clear(changes);
}

static bool inv_on_destroy(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	set_inv_slot_t changes;
	set_inv_slot_init(changes);
	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		player->inventory.items[k + INVENTORY_SLOT_HOTBAR] =
			inv->items[k + DISPENSER_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_HOTBAR);
	}
	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		player->inventory.items[k + INVENTORY_SLOT_MAIN] =
			inv->items[k + DISPENSER_SLOT_MAIN];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_MAIN);
	}
	server_local_send_inv_changes(pid, changes, &player->inventory,
								  WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);
	return true;
}

struct inventory_logic inventory_logic_dispenser = {
	.pre_action = inv_pre_action,
	.post_action = inv_post_action,
	.on_collect = inv_on_collect,
	.on_create = inv_on_create,
	.on_destroy = inv_on_destroy,
	.on_close = inv_on_close,
};
