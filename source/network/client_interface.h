/*
	Copyright (c) 2022 ByteBit/xtreme8000

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

#ifndef CLIENT_INTERFACE_H
#define CLIENT_INTERFACE_H

#include "../entity/entity.h"
#include "../item/items.h"
#include "../item/window_container.h"
#include "../world.h"

#include "../cglm/cglm.h"
#include <stdint.h>

enum client_rpc_type {
	CRPC_CHUNK,
	CRPC_UNLOAD_CHUNK,
	CRPC_INVENTORY_SLOT,
	CRPC_PLAYER_POS,
	CRPC_TIME_SET,
	CRPC_WORLD_RESET,
	CRPC_SET_BLOCK,
	CRPC_WINDOW_TRANSACTION,
	CRPC_SPAWN_ITEM,
	CRPC_PICKUP_ITEM,
	CRPC_ENTITY_DESTROY,
	CRPC_ENTITY_MOVE,
	CRPC_OPEN_WINDOW,
	CRPC_FURNACE_STATE,
	CRPC_PLAYER_SET_HEALTH,
	CRPC_SPAWN_MONSTER,
	CRPC_SPAWN_MINECART,
	CRPC_SPAWN_BOAT,
	CRPC_SPAWN_FISHING_HOOK,
	CRPC_FISHING_BITE,   /* server notifies client that a fish bit the hook */
	CRPC_SPAWN_POINT,    /* world/player spawn coordinates for compass */
	CRPC_WORLD_LOADED, /* spawn area chunks loaded + hotbar sent -> start game */
	CRPC_GAMEMODE,
	CRPC_PORTAL_LOADING, /* Ladescreen für diesen Spieler zeigen (Portal-Wechsel) */
};

#ifdef SPLITSCREEN
#define CRPC_PLAYER_ID(pid) .player_id = (uint8_t)(pid),
#else
#define CRPC_PLAYER_ID(pid)
#endif

struct client_rpc {
	enum client_rpc_type type;
#ifdef SPLITSCREEN
	uint8_t player_id;
#endif
	union {
		struct {
			w_coord_t x, y, z;
			w_coord_t sx, sy, sz;
			uint8_t* ids;
			uint8_t* metadata;
			uint8_t* lighting_sky;
			uint8_t* lighting_torch;
			enum world_dim dimension;
		} chunk;
		struct {
			w_coord_t x, z;
			enum world_dim dimension;
		} unload_chunk;
		struct {
			uint8_t window;
			uint8_t slot;
			struct item_data item;
		} inventory_slot;
		struct {
			vec3 position;
			vec2 rotation;
			enum world_dim dimension;
			bool teleport; /* true nur bei Dimensions-Wechsel */
		} player_pos;
		uint64_t time_set;
		struct {
			enum world_dim dimension;
			uint32_t local_entity;
		} world_reset;
		struct {
			w_coord_t x, y, z;
			struct block_data block;
			enum world_dim dimension;
		} set_block;
		struct {
			uint8_t window;
			uint16_t action_id;
			bool accepted;
		} window_transaction;
		struct {
			uint8_t window;
			enum window_type type;
			uint8_t slot_count;
		} window_open;
		struct {
			uint8_t window;
			uint16_t burn_time;
			uint16_t burn_total;
			uint16_t cook_time;
			uint16_t cook_total;
		} furnace_state;
		struct {
			uint32_t entity_id;
			struct item_data item;
			vec3 pos;
			vec3 vel;
			enum world_dim dimension;
		} spawn_item;
		struct {
			uint32_t entity_id;
			int monster_id;
			vec3 pos;
			enum world_dim dimension;
		} spawn_monster;
		struct {
		    uint32_t entity_id;
		    vec3    pos;
		} spawn_minecart;
		struct {
		    uint32_t entity_id;
		    vec3    pos;
		    float   yaw;
		} spawn_boat;
		struct {
			uint32_t entity_id;
			uint32_t owner_id;
			vec3     pos;
			float    vel_x, vel_y, vel_z;
		} spawn_fishing_hook;
		struct {
			uint32_t entity_id;
		} fishing_bite;
		struct {
			int x, z;
		} spawn_point;
		struct {
			uint32_t entity_id;
			uint32_t collector_id;
		} pickup_item;
		struct {
			uint32_t entity_id;
		} entity_destroy;
		struct {
			uint32_t entity_id;
			vec3 pos;
		} entity_move;
		struct {
			int16_t health;
		} player_set_health;
		struct {
			bool creative;
		} gamemode;
	} payload;
};

void clin_init(void);
void clin_update(void);
void clin_rpc_send(struct client_rpc* call);

/* Chunk-Transfer-Puffer-Pool (siehe client_interface.c). Der Server nimmt einen
 * 80K-Block (ids|metadata|sky|torch), der Client gibt ihn nach dem Import via
 * ids-Zeiger zurueck. take() liefert NULL, wenn der Pool gerade leer ist. */
void clin_chunk_pool_init(void);
uint8_t* clin_chunk_buf_take(void);
void clin_chunk_buf_return(uint8_t* b);

#endif
