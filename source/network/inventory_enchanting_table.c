/*
	Copyright (c) 2026
*/

#include "../item/window_container.h"
#include "inventory_logic.h"
#include "server_local.h"
#include <stdlib.h>

static int enchanting_count_bookshelves(struct server_world* w, w_coord_t x,
										w_coord_t y, w_coord_t z) {
	int count = 0;
	for(int dx = -2; dx <= 2; dx++) {
		for(int dz = -2; dz <= 2; dz++) {
			if(dx == 0 && dz == 0)
				continue;
			if(abs(dx) != 2 && abs(dz) != 2)
				continue;

			struct block_data shelf;
			if(!server_world_get_block(w, x + dx, y, z + dz, &shelf))
				continue;
			if(shelf.type != BLOCK_BOOKSHELF)
				continue;

			// Require air in-between (vanilla requires a 1-block gap).
			struct block_data gap;
			int gdx = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
			int gdz = (dz == 0) ? 0 : (dz > 0 ? 1 : -1);
			if(!server_world_get_block(w, x + gdx, y, z + gdz, &gap))
				continue;
			if(gap.type != BLOCK_AIR)
				continue;

			count++;
			if(count >= 15)
				return 15;
		}
	}
	return count;
}

static void enchanting_set_options(struct inventory* inv) {
	struct server_local* s = inv->user;
	const int shelves
		= enchanting_count_bookshelves(&s->world, inv->x, inv->y, inv->z);

	// Option "buttons" represented as book items; level encodes cost.
	// (Real MC 1.7.x uses an RNG/seed + enchantability; this is a simplified stand-in.)
	const int l0 = 1 + shelves / 2;          // ~1..8
	const int l1 = 1 + (shelves * 2) / 3;    // ~1..11
	const int l2 = shelves * 2;              // ~0..30
	const uint8_t levels[3] = {
		(uint8_t)(l0 < 1 ? 1 : (l0 > 30 ? 30 : l0)),
		(uint8_t)(l1 < 1 ? 1 : (l1 > 30 ? 30 : l1)),
		(uint8_t)(l2 < 1 ? 1 : (l2 > 30 ? 30 : l2)),
	};

	for(int k = 0; k < 3; k++) {
		inv->items[ENCHANTING_TABLE_SLOT_OPTION + k]
			= (struct item_data) {.id = ITEM_BOOK, .durability = levels[k], .count = 1};
	}
}

static bool inv_pre_action(struct inventory* inv, size_t slot, bool right,
						   set_inv_slot_t changes) {
	// Option slots are GUI buttons, not real inventory slots.
	if(slot >= ENCHANTING_TABLE_SLOT_OPTION
	   && slot < ENCHANTING_TABLE_SLOT_OPTION + 3) {
		if(right)
			return false;
		// Left click handled below.
	}

	// Clicking an option applies a single enchantment (name suffix via durability nibble).
	if(slot >= ENCHANTING_TABLE_SLOT_OPTION
	   && slot < ENCHANTING_TABLE_SLOT_OPTION + 3) {
		struct item_data target;
		if(!inventory_get_slot(inv, ENCHANTING_TABLE_SLOT_ITEM, &target))
			return false;

		uint8_t opt = (uint8_t)(slot - ENCHANTING_TABLE_SLOT_OPTION);
		uint8_t lvl = inv->items[slot].durability;
		if(lvl == 0)
			lvl = 1;

		uint8_t ench_id = 0;
		uint8_t ench_lvl = 0;

		switch(target.id) {
			case ITEM_WOOD_SWORD:
			case ITEM_STONE_SWORD:
			case ITEM_IRON_SWORD:
			case ITEM_DIAMOND_SWORD:
			case ITEM_GOLD_SWORD:
				ench_id = 1; // Sharpness
				ench_lvl = (uint8_t)(1 + opt * 2);
				break;
			case ITEM_WOOD_PICKAXE:
			case ITEM_STONE_PICKAXE:
			case ITEM_IRON_PICKAXE:
			case ITEM_DIAMOND_PICKAXE:
			case ITEM_GOLD_PICKAXE:
			case ITEM_WOOD_AXE:
			case ITEM_STONE_AXE:
			case ITEM_IRON_AXE:
			case ITEM_DIAMOND_AXE:
			case ITEM_GOLD_AXE:
			case ITEM_WOOD_SHOVEL:
			case ITEM_STONE_SHOVEL:
			case ITEM_IRON_SHOVEL:
			case ITEM_DIAMOND_SHOVEL:
			case ITEM_GOLD_SHOVEL:
				ench_id = 2; // Efficiency
				ench_lvl = (uint8_t)(1 + opt * 2);
				break;
			case ITEM_LEATHER_HELMET:
			case ITEM_LEATHER_CHESTPLATE:
			case ITEM_LEATHER_LEGGINGS:
			case ITEM_LEATHER_BOOTS:
			case ITEM_CHAIN_HELMET:
			case ITEM_CHAIN_CHESTPLATE:
			case ITEM_CHAIN_LEGGINGS:
			case ITEM_CHAIN_BOOTS:
			case ITEM_IRON_HELMET:
			case ITEM_IRON_CHESTPLATE:
			case ITEM_IRON_LEGGINGS:
			case ITEM_IRON_BOOTS:
			case ITEM_DIAMOND_HELMET:
			case ITEM_DIAMOND_CHESTPLATE:
			case ITEM_DIAMOND_LEGGINGS:
			case ITEM_DIAMOND_BOOTS:
			case ITEM_GOLD_HELMET:
			case ITEM_GOLD_CHESTPLATE:
			case ITEM_GOLD_LEGGINGS:
			case ITEM_GOLD_BOOTS:
				ench_id = 3; // Protection
				ench_lvl = (uint8_t)(1 + opt);
				break;
			default:
				return false;
		}

		if(ench_lvl > 5)
			ench_lvl = 5;

		target.durability = (uint8_t)((ench_id << 4) | (ench_lvl & 0x0F));
		inventory_set_slot(inv, ENCHANTING_TABLE_SLOT_ITEM, target);
		set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_ITEM);

		// Refresh offers after enchanting.
		enchanting_set_options(inv);
		for(int k = 0; k < 3; k++)
			set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_OPTION + k);

		return false;
	}

	return true;
}

static void inv_post_action(struct inventory* inv, size_t slot, bool right,
							bool accepted, set_inv_slot_t changes) {
	(void)right;
	(void)accepted;

	if(slot == ENCHANTING_TABLE_SLOT_ITEM) {
		enchanting_set_options(inv);
		for(int k = 0; k < 3; k++)
			set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_OPTION + k);
	}
}

static void inv_on_close(struct inventory* inv) {
	struct server_local* s = inv->user;
	uint8_t pid = s->active_player_id;
	struct server_player* player = &s->players[pid];

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	// Don't persist items (like crafting table): drop input item on close.
	struct item_data item;
	if(inventory_get_slot(inv, ENCHANTING_TABLE_SLOT_ITEM, &item)) {
		inventory_clear_slot(inv, ENCHANTING_TABLE_SLOT_ITEM);
		set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_ITEM);
		server_local_spawn_item((vec3) {player->x, player->y, player->z},
								&item, true, s);
	}

	for(int k = 0; k < 3; k++) {
		inventory_clear_slot(inv, ENCHANTING_TABLE_SLOT_OPTION + k);
		set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_OPTION + k);
	}

	struct item_data picked_item;
	if(inventory_get_picked_item(inv, &picked_item)) {
		inventory_clear_picked_item(inv);
		set_inv_slot_push(changes, SPECIAL_SLOT_PICKED_ITEM);
		server_local_spawn_item((vec3) {player->x, player->y, player->z},
								&picked_item, true, s);
	}

	server_local_send_inv_changes(pid, changes, inv, WINDOWC_ENCHANTING_TABLE);
	set_inv_slot_clear(changes);

	inventory_destroy(inv);
}

static bool inv_on_collect(struct inventory* inv, struct item_data* item) {
	struct server_local* s = inv->user;
	uint8_t priorities[INVENTORY_SIZE_HOTBAR + INVENTORY_SIZE_MAIN];

	for(size_t k = 0; k < INVENTORY_SIZE_HOTBAR; k++)
		priorities[k] = k + ENCHANTING_TABLE_SLOT_HOTBAR;

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++)
		priorities[k + INVENTORY_SIZE_HOTBAR] = k + ENCHANTING_TABLE_SLOT_MAIN;

	set_inv_slot_t changes;
	set_inv_slot_init(changes);

	bool success
		= inventory_collect(inv, item, priorities,
							sizeof(priorities) / sizeof(*priorities), changes);
	server_local_send_inv_changes(s->active_player_id, changes, inv,
								  WINDOWC_ENCHANTING_TABLE);
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
		inv->items[k + ENCHANTING_TABLE_SLOT_HOTBAR]
			= player->inventory.items[k + INVENTORY_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + ENCHANTING_TABLE_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		inv->items[k + ENCHANTING_TABLE_SLOT_MAIN]
			= player->inventory.items[k + INVENTORY_SLOT_MAIN];
		set_inv_slot_push(changes, k + ENCHANTING_TABLE_SLOT_MAIN);
	}

	// Ensure storage slots start empty and publish option "buttons".
	inv->items[ENCHANTING_TABLE_SLOT_ITEM] = (struct item_data) {0};
	set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_ITEM);
	enchanting_set_options(inv);
	for(int k = 0; k < 3; k++)
		set_inv_slot_push(changes, ENCHANTING_TABLE_SLOT_OPTION + k);

	server_local_send_inv_changes(pid, changes, inv, WINDOWC_ENCHANTING_TABLE);
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
			= inv->items[k + ENCHANTING_TABLE_SLOT_HOTBAR];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_HOTBAR);
	}

	for(size_t k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		player->inventory.items[k + INVENTORY_SLOT_MAIN]
			= inv->items[k + ENCHANTING_TABLE_SLOT_MAIN];
		set_inv_slot_push(changes, k + INVENTORY_SLOT_MAIN);
	}

	server_local_send_inv_changes(pid, changes, &player->inventory,
								  WINDOWC_INVENTORY);
	set_inv_slot_clear(changes);
	return true;
}

struct inventory_logic inventory_logic_enchanting_table = {
	.pre_action = inv_pre_action,
	.post_action = inv_post_action,
	.on_collect = inv_on_collect,
	.on_create = inv_on_create,
	.on_destroy = inv_on_destroy,
	.on_close = inv_on_close,
};
