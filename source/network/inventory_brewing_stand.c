/*
	Copyright (c) 2026
*/

#include "../item/window_container.h"
#include "inventory_logic.h"
#include "server_local.h"

static struct brewing_stand_data*
brewing_stand_data_get(struct server_local* s, w_coord_t x, w_coord_t y,
					   w_coord_t z, bool create) {
	for(int i = 0; i < MAX_BREWING_STANDS; i++) {
		if(s->brewing_stands[i].pos.x == x && s->brewing_stands[i].pos.y == y
		   && s->brewing_stands[i].pos.z == z)
			return &s->brewing_stands[i];
	}

	if(!create)
		return NULL;

	for(int i = 0; i < MAX_BREWING_STANDS; i++) {
		if(s->brewing_stands[i].pos.x != -1)
			continue;

		s->brewing_stands[i].pos.x = x;
		s->brewing_stands[i].pos.y = y;
		s->brewing_stands[i].pos.z = z;
		for(int k = 0; k < BREWING_STAND_SIZE_STORAGE; k++)
			s->brewing_stands[i].items[k] = (struct item_data) {0};
		s->brewing_stands[i].brew_time = 0;
		s->brewing_stands[i].brew_total = 0;
		return &s->brewing_stands[i];
	}

	return NULL;
}

static bool inv_pre_action(struct inventory* inv, size_t slot, bool right,
						   set_inv_slot_t changes) {
	(void)inv;
	(void)slot;
	(void)right;
	(void)changes;
	return true;
}

static void inv_post_action(struct inventory* inv, size_t slot, bool right,
							bool accepted, set_inv_slot_t changes) {
	(void)inv;
	(void)slot;
	(void)right;
	(void)accepted;
	(void)changes;
}

static void inv_on_close(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	struct brewing_stand_data* stand
		= brewing_stand_data_get(s, inv->x, inv->y, inv->z, true);
	if(stand) {
		for(size_t k = 0; k < BREWING_STAND_SIZE_STORAGE; k++)
			stand->items[k] = inv->items[k];
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
		priorities[k] = k + BREWING_STAND_SLOT_HOTBAR;

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++)
		priorities[k + INVENTORY_SIZE_HOTBAR] = k + BREWING_STAND_SLOT_MAIN;

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	bool success
		= inventory_collect(inv, item, priorities,
							sizeof(priorities) / sizeof(*priorities), changes);
	server_local_send_inv_changes(s->active_player_id, changes, inv,
								  WINDOWC_BREWING_STAND);
	set_inv_slot_clear(changes);

	return success;
}

static void inv_on_create(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];
	struct brewing_stand_data* stand
		= brewing_stand_data_get(s, inv->x, inv->y, inv->z, true);

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		inv->items[k + BREWING_STAND_SLOT_HOTBAR]
			= player->inventory.items[k + INVENTORY_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + BREWING_STAND_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		inv->items[k + BREWING_STAND_SLOT_MAIN]
			= player->inventory.items[k + INVENTORY_SLOT_MAIN];
		set_inv_slot_push(changes, k + BREWING_STAND_SLOT_MAIN);
	}

	if(stand) {
		for(size_t k = 0; k < BREWING_STAND_SIZE_STORAGE; k++) {
			inv->items[k] = stand->items[k];
			set_inv_slot_push(changes, k);
		}
	}

	server_local_send_inv_changes(pid, changes, inv, WINDOWC_BREWING_STAND);
	set_inv_slot_clear(changes);
}

static bool inv_on_destroy(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		player->inventory.items[k + INVENTORY_SLOT_HOTBAR]
			= inv->items[k + BREWING_STAND_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		player->inventory.items[k + INVENTORY_SLOT_MAIN]
			= inv->items[k + BREWING_STAND_SLOT_MAIN];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_MAIN);
	}

	server_local_send_inv_changes(pid, changes, &player->inventory,
								  WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);
	return true;
}

struct inventory_logic inventory_logic_brewing_stand = {
	.pre_action = inv_pre_action,
	.post_action = inv_post_action,
	.on_collect = inv_on_collect,
	.on_create = inv_on_create,
	.on_destroy = inv_on_destroy,
	.on_close = inv_on_close,
};

