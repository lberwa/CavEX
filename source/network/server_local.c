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

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include <stdio.h>

#include "../cglm/cglm.h"

#include "../item/window_container.h"
#include "../platform/thread.h"
#include "../daytime.h"
#ifdef SPLITSCREEN
#include "../game/game_state.h"
#endif
#include "client_interface.h"
#include "inventory_logic.h"
#include "server_interface.h"
#include "server_local.h"
#include "server_world.h"
#include "complex_block_archive.h"
#include "../entity/entity_monster.h"

#define CHUNK_DIST2(x1, x2, z1, z2)                                            \
	(((x1) - (x2)) * ((x1) - (x2)) + ((z1) - (z2)) * ((z1) - (z2)))

#define ANIMAL_SPAWN_INTERVAL_TICKS 200
#define ANIMAL_NEARBY_LIMIT 2
#define ANIMAL_NEARBY_RADIUS 24
#define ANIMAL_SPAWN_MIN_DIST 8
#define ANIMAL_SPAWN_MAX_DIST 18
#define MONSTER_MAX_TORCH_LIGHT 0
#define MONSTER_MAX_DAY_SKY_LIGHT 0

static int64_t server_local_hash_seed_from_name(const char* name) {
	uint64_t hash = 1469598103934665603ULL;
	for(const unsigned char* p = (const unsigned char*)name; p && *p; p++) {
		hash ^= *p;
		hash *= 1099511628211ULL;
	}
	return (int64_t)hash;
}

static const int server_local_spawn_animals[] = {
	ANIMAIL_PIG,
	ANIMAIL_SHEEP,
};

static const int server_local_spawn_dark_monsters[] = {
	MONSTER_CREEPER,
};

static bool server_local_is_spawn_animal(int monster_id) {
	for(size_t i = 0; i < sizeof(server_local_spawn_animals)
						 / sizeof(server_local_spawn_animals[0]);
		i++) {
		if(server_local_spawn_animals[i] == monster_id)
			return true;
	}

	return false;
}

static bool server_local_is_spawn_dark_monster(int monster_id) {
	for(size_t i = 0; i < sizeof(server_local_spawn_dark_monsters)
						 / sizeof(server_local_spawn_dark_monsters[0]);
		i++) {
		if(server_local_spawn_dark_monsters[i] == monster_id)
			return true;
	}

	return false;
}

static bool server_local_is_daytime(const struct server_local* s) {
	assert(s);

	float time = fmodf((float)s->world_time, (float)DAY_LENGTH_TICKS);
	return time >= 0.0f && time < 1300.0f;
}

static bool server_local_damage_enabled(void) {
#if defined(FAST_MOVE) || defined(FAST_MOVING)
	return !gstate.fast_moving;
#else
	return true;
#endif
}

static size_t server_local_count_nearby_animals(struct server_local* s, float px,
												float py, float pz, float radius) {
	assert(s);

	size_t count = 0;
	float radius2 = radius * radius;

	dict_entity_it_t it;
	dict_entity_it(it, s->entities);
	while(!dict_entity_end_p(it)) {
		struct entity* e = dict_entity_ref(it)->value;
		if(e && e->type == ENTITY_MONSTER
		   && server_local_is_spawn_animal(e->data.monster.id)) {
			float dx = e->pos[0] - px;
			float dy = e->pos[1] - py;
			float dz = e->pos[2] - pz;
			if(dx * dx + dy * dy + dz * dz <= radius2)
				count++;
		}
		dict_entity_next(it);
	}

	return count;
}

static int server_local_choose_spawn_animal(struct server_local* s) {
	assert(s);

	size_t count = sizeof(server_local_spawn_animals)
				   / sizeof(server_local_spawn_animals[0]);
	return server_local_spawn_animals[rand_gen_range(&s->rand_src, 0, count)];
}

static size_t server_local_count_nearby_dark_monsters(struct server_local* s,
													  float px, float py,
													  float pz, float radius) {
	assert(s);

	size_t count = 0;
	float radius2 = radius * radius;

	dict_entity_it_t it;
	dict_entity_it(it, s->entities);
	while(!dict_entity_end_p(it)) {
		struct entity* e = dict_entity_ref(it)->value;
		if(e && e->type == ENTITY_MONSTER
		   && server_local_is_spawn_dark_monster(e->data.monster.id)) {
			float dx = e->pos[0] - px;
			float dy = e->pos[1] - py;
			float dz = e->pos[2] - pz;
			if(dx * dx + dy * dy + dz * dz <= radius2)
				count++;
		}
		dict_entity_next(it);
	}

	return count;
}

static int server_local_choose_spawn_dark_monster(struct server_local* s) {
	assert(s);

	size_t count = sizeof(server_local_spawn_dark_monsters)
				   / sizeof(server_local_spawn_dark_monsters[0]);
	return server_local_spawn_dark_monsters[rand_gen_range(&s->rand_src, 0,
														   count)];
}

static bool server_local_is_dark_monster_spawn(const struct server_local* s,
												   const struct block_data* body,
												   const struct block_data* head) {
	assert(s && body && head);

	if(body->torch_light > MONSTER_MAX_TORCH_LIGHT
	   || head->torch_light > MONSTER_MAX_TORCH_LIGHT)
		return false;

	if(server_local_is_daytime(s)
	   && (body->sky_light > MONSTER_MAX_DAY_SKY_LIGHT
		   || head->sky_light > MONSTER_MAX_DAY_SKY_LIGHT))
		return false;

	return true;
}

static bool server_local_find_animal_spawn(struct server_local* s, float px,
										   float pz, vec3 out_pos) {
	assert(s && out_pos);

	for(int attempt = 0; attempt < 12; attempt++) {
		int sx = (int)floorf(px)
				 + rand_gen_range(&s->rand_src, -ANIMAL_SPAWN_MAX_DIST,
								  ANIMAL_SPAWN_MAX_DIST + 1);
		int sz = (int)floorf(pz)
				 + rand_gen_range(&s->rand_src, -ANIMAL_SPAWN_MAX_DIST,
								  ANIMAL_SPAWN_MAX_DIST + 1);
		int dx = sx - (int)floorf(px);
		int dz = sz - (int)floorf(pz);
		int dist2 = dx * dx + dz * dz;

		if(dist2 < ANIMAL_SPAWN_MIN_DIST * ANIMAL_SPAWN_MIN_DIST
		   || dist2 > ANIMAL_SPAWN_MAX_DIST * ANIMAL_SPAWN_MAX_DIST)
			continue;

		struct block_data ground, body, head;
		for(int y = WORLD_HEIGHT - 2; y >= 1; y--) {
			if(!server_world_get_block(&s->world, sx, y - 1, sz, &ground)
			   || !server_world_get_block(&s->world, sx, y, sz, &body)
			   || !server_world_get_block(&s->world, sx, y + 1, sz, &head))
				break;

			if((ground.type == BLOCK_GRASS || ground.type == BLOCK_DIRT)
			   && body.type == BLOCK_AIR && head.type == BLOCK_AIR) {
				out_pos[0] = (float)sx + 0.5f;
				out_pos[1] = (float)y;
				out_pos[2] = (float)sz + 0.5f;
				return true;
			}
		}
	}

	return false;
}

static bool server_local_find_dark_monster_spawn(struct server_local* s, float px,
												 float pz, vec3 out_pos) {
	assert(s && out_pos);

	for(int attempt = 0; attempt < 12; attempt++) {
		int sx = (int)floorf(px)
				 + rand_gen_range(&s->rand_src, -ANIMAL_SPAWN_MAX_DIST,
								  ANIMAL_SPAWN_MAX_DIST + 1);
		int sz = (int)floorf(pz)
				 + rand_gen_range(&s->rand_src, -ANIMAL_SPAWN_MAX_DIST,
								  ANIMAL_SPAWN_MAX_DIST + 1);
		int dx = sx - (int)floorf(px);
		int dz = sz - (int)floorf(pz);
		int dist2 = dx * dx + dz * dz;

		if(dist2 < ANIMAL_SPAWN_MIN_DIST * ANIMAL_SPAWN_MIN_DIST
		   || dist2 > ANIMAL_SPAWN_MAX_DIST * ANIMAL_SPAWN_MAX_DIST)
			continue;

		struct block_data ground, body, head;
		for(int y = WORLD_HEIGHT - 2; y >= 1; y--) {
			if(!server_world_get_block(&s->world, sx, y - 1, sz, &ground)
			   || !server_world_get_block(&s->world, sx, y, sz, &body)
			   || !server_world_get_block(&s->world, sx, y + 1, sz, &head))
				break;

			if((ground.type == BLOCK_GRASS || ground.type == BLOCK_DIRT)
			   && body.type == BLOCK_AIR && head.type == BLOCK_AIR
			   && server_local_is_dark_monster_spawn(s, &body, &head)) {
				out_pos[0] = (float)sx + 0.5f;
				out_pos[1] = (float)y;
				out_pos[2] = (float)sz + 0.5f;
				return true;
			}
		}
	}

	return false;
}

static void server_local_try_spawn_nearby_animal(struct server_local* s, float px,
												 float py, float pz) {
	assert(s);

	if(!s->world.generator.finisher_animals)
		return;

	if(!server_local_is_daytime(s))
		return;

	if(s->world_time % ANIMAL_SPAWN_INTERVAL_TICKS != 0)
		return;

	if(server_local_count_nearby_animals(s, px, py, pz, ANIMAL_NEARBY_RADIUS)
	   >= ANIMAL_NEARBY_LIMIT)
		return;

	vec3 spawn_pos;
	if(server_local_find_animal_spawn(s, px, pz, spawn_pos))
		server_local_spawn_monster(spawn_pos,
								   server_local_choose_spawn_animal(s), s);
}

static void server_local_try_spawn_nearby_dark_monster(struct server_local* s,
													   float px, float py,
													   float pz) {
	assert(s);

	if(s->world_time % ANIMAL_SPAWN_INTERVAL_TICKS != 0)
		return;

	if(server_local_count_nearby_dark_monsters(
		   s, px, py, pz, ANIMAL_NEARBY_RADIUS)
	   >= ANIMAL_NEARBY_LIMIT)
		return;

	vec3 spawn_pos;
	if(server_local_find_dark_monster_spawn(s, px, pz, spawn_pos))
		server_local_spawn_monster(
			spawn_pos, server_local_choose_spawn_dark_monster(s), s);
}


struct entity* server_local_spawn_minecart(vec3 pos, struct server_local* s) {
    uint32_t entity_id = entity_gen_id(s->entities);
    struct entity** e_ptr = dict_entity_safe_get(s->entities, entity_id);
    *e_ptr = malloc(sizeof(struct entity));
    struct entity* e = *e_ptr;
    assert(e);

    entity_minecart(entity_id, e, true, &s->world);
    e->teleport(e, pos);

    glm_vec3_copy(
        (vec3){ rand_gen_flt(&s->rand_src) - 0.5f,
               rand_gen_flt(&s->rand_src) - 0.5f,
               rand_gen_flt(&s->rand_src) - 0.5f },
        e->vel
    );
    glm_vec3_normalize(e->vel);
    glm_vec3_scale(
        e->vel,
        (2.0f * rand_gen_flt(&s->rand_src) + 0.5f) * 0.1f,
        e->vel
    );

    clin_rpc_send(&(struct client_rpc) {
        .type = CRPC_SPAWN_MINECART,
        .payload.spawn_minecart.entity_id = e->id,
        .payload.spawn_minecart.pos       = { pos[0], pos[1], pos[2] },
    });

    return e;
}



struct entity* server_local_spawn_item(vec3 pos, struct item_data* it,
									   bool throw, struct server_local* s) {
	uint32_t entity_id = entity_gen_id(s->entities);
	struct entity** e_ptr = dict_entity_safe_get(s->entities, entity_id);
	*e_ptr = malloc(sizeof(struct entity));
	struct entity* e = *e_ptr;
	assert(e);

	entity_item(entity_id, e, true, &s->world, *it);
	e->teleport(e, pos);

	if(throw) {
#ifdef SPLITSCREEN
		uint8_t pid = s->active_player_id;
		float rx = glm_rad(-s->players[pid].rx
						   + (rand_gen_flt(&s->rand_src) - 0.5F) * 22.5F);
		float ry = glm_rad(s->players[pid].ry + 90.0F
						   + (rand_gen_flt(&s->rand_src) - 0.5F) * 22.5F);
#else
		float rx = glm_rad(-s->player.rx
						   + (rand_gen_flt(&s->rand_src) - 0.5F) * 22.5F);
		float ry = glm_rad(s->player.ry + 90.0F
						   + (rand_gen_flt(&s->rand_src) - 0.5F) * 22.5F);
#endif
		e->vel[0] = sinf(rx) * sinf(ry) * 0.25F;
		e->vel[1] = cosf(ry) * 0.25F;
		e->vel[2] = cosf(rx) * sinf(ry) * 0.25F;
	} else {
		glm_vec3_copy((vec3) {rand_gen_flt(&s->rand_src) - 0.5F,
							  rand_gen_flt(&s->rand_src) - 0.5F,
							  rand_gen_flt(&s->rand_src) - 0.5F},
					  e->vel);
		glm_vec3_normalize(e->vel);
		glm_vec3_scale(
			e->vel, (2.0F * rand_gen_flt(&s->rand_src) + 0.5F) * 0.1F, e->vel);
	}

	clin_rpc_send(&(struct client_rpc) {
		.type = CRPC_SPAWN_ITEM,
		.payload.spawn_item.entity_id = e->id,
		.payload.spawn_item.item = e->data.item.item,
		.payload.spawn_item.pos = {e->pos[0], e->pos[1], e->pos[2]},
		.payload.spawn_item.vel = {e->vel[0], e->vel[1], e->vel[2]},
	});

	return e;
}

struct entity* server_local_spawn_monster(vec3 pos, int monster_id,
									   struct server_local* s) {
	uint32_t entity_id = entity_gen_id(s->entities);

	struct entity** e_ptr = dict_entity_safe_get(s->entities, entity_id);
	*e_ptr = malloc(sizeof(struct entity));
	struct entity* e = *e_ptr;
	assert(e);


	entity_monster(entity_id, e, true, &s->world, monster_id);

	pos[0] = floorf(pos[0]) + 0.5f;
	pos[2] = floorf(pos[2]) + 0.5f;
	//pos[1] = pos[1] + 1.0f;
	e->teleport(e, pos);

	glm_vec3_copy((vec3) {rand_gen_flt(&s->rand_src) - 0.5F,
							rand_gen_flt(&s->rand_src) - 0.5F,
							rand_gen_flt(&s->rand_src) - 0.5F},
					e->vel);
	glm_vec3_normalize(e->vel);
	glm_vec3_scale(
		e->vel, (2.0F * rand_gen_flt(&s->rand_src) + 0.5F) * 0.1F, e->vel);

	clin_rpc_send(&(struct client_rpc) {
		.type = CRPC_SPAWN_MONSTER,
		.payload.spawn_monster.entity_id = e->id,
		.payload.spawn_monster.monster_id = monster_id,
		.payload.spawn_monster.pos = {e->pos[0], e->pos[1], e->pos[2]},
	});

	return e;
}

void server_local_spawn_block_drops(struct server_local* s,
									struct block_info* blk_info) {
	assert(s && blk_info);

	if(!blocks[blk_info->block->type])
		return;

	struct random_gen tmp = s->rand_src;
	size_t count
		= blocks[blk_info->block->type]->getDroppedItem(blk_info, NULL, &tmp, s);

	if(count > 0) {
		struct item_data items[count];
		blocks[blk_info->block->type]->getDroppedItem(blk_info, items,
													  &s->rand_src, s);

		for(size_t k = 0; k < count; k++)
			server_local_spawn_item((vec3) {blk_info->x + 0.5F,
											blk_info->y + 0.5F,
											blk_info->z + 0.5F},
									items + k, false, s);
	}
}

void server_local_send_inv_changes(uint8_t player_id, set_inv_slot_t changes,
								   struct inventory* inv, uint8_t window) {
	assert(changes && inv);

	set_inv_slot_it_t it;
	set_inv_slot_it(it, changes);

	while(!set_inv_slot_end_p(it)) {
		size_t slot = *set_inv_slot_ref(it);

		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(player_id)
			.type = CRPC_INVENTORY_SLOT,
			.payload.inventory_slot.window = window,
			.payload.inventory_slot.slot = slot,
			.payload.inventory_slot.item = (slot == SPECIAL_SLOT_PICKED_ITEM) ?
				inv->picked_item :
				inv->items[slot],
		});

		set_inv_slot_next(it);
	}
}

void server_local_set_player_health(struct server_local* s, int player_id, short new_health) {
#ifdef SPLITSCREEN
	if(player_id < 0 || player_id >= MAX_SERVER_PLAYERS)
		player_id = 0;
#else
	(void)player_id;
	player_id = 0;
#endif
#ifdef SPLITSCREEN
	struct server_player* player = &s->players[player_id];
#else
	struct server_player* player = &s->player;
#endif
	if(player->creative && new_health < player->health)
		return;
	player->health = new_health;
	if (player->health > MAX_PLAYER_HEALTH) player->health = MAX_PLAYER_HEALTH;
	if (player->health <= 0) {
		//player dead, drop all items and move to spawn position
		for (int i = 0; i < INVENTORY_SIZE; i++) {
			struct item_data item;
			inventory_get_slot(&player->inventory, i, &item);

			if (item.id != 0) {
				inventory_clear_slot(&player->inventory, i);
				clin_rpc_send(&(struct client_rpc) {
					CRPC_PLAYER_ID(player_id)
					.type = CRPC_INVENTORY_SLOT,
					.payload.inventory_slot.window = WINDOWC_INVENTORY,
					.payload.inventory_slot.slot = i,
					.payload.inventory_slot.item = player->inventory.items[i]
				});

				server_local_spawn_item(
					(vec3) {player->x, player->y, player->z}, &item, false, s);
			}
		}

		//respawn with full health
		player->health = MAX_PLAYER_HEALTH;
		player->x = player->spawn_x;
		player->y = player->spawn_y;
		player->z = player->spawn_z;
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(player_id)
			.type = CRPC_PLAYER_POS,
			.payload.player_pos.position = {player->x, player->y, player->z},
			.payload.player_pos.rotation = {0, 0}
		});
	}

	//send updated health to client
	clin_rpc_send(&(struct client_rpc) {
		CRPC_PLAYER_ID(player_id)
		.type = CRPC_PLAYER_SET_HEALTH,
		.payload.player_set_health.health = player->health
	});
}

void server_local_queue_fluid_change(struct server_local* s, w_coord_t x,
									 w_coord_t y, w_coord_t z,
									 struct block_data blk) {
	if(s->fluid_change_count >= MAX_FLUID_CHANGES)
		return; // buffer full: the surplus is handled on the next fluid step
	s->fluid_changes[s->fluid_change_count++] = (struct fluid_change) {
		.x = x, .y = y, .z = z, .blk = blk,
	};
}

void server_local_flush_fluid_changes(struct server_local* s) {
	for(int i = 0; i < s->fluid_change_count; i++) {
		struct fluid_change* fc = &s->fluid_changes[i];
		server_world_set_block(s, fc->x, fc->y, fc->z, fc->blk);
	}
	s->fluid_change_count = 0;
}

void server_local_schedule_fluid(struct server_local* s, w_coord_t x,
								 w_coord_t y, w_coord_t z) {
	// dedup via open addressing: the same cell is woken by many neighbours each
	// round; without this the buffer overflows with duplicates and real cells
	// get dropped (and then never dry up).
	uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u)
		^ ((uint32_t)z * 83492791u);
	uint32_t slot = h & (FLUID_HASH_SIZE - 1);
	while(s->fluid_hash[slot]) {
		struct fluid_pos* p = &s->fluid_sched[s->fluid_hash[slot] - 1];
		if(p->x == x && p->y == y && p->z == z)
			return; // already scheduled this round
		slot = (slot + 1) & (FLUID_HASH_SIZE - 1);
	}
	if(s->fluid_sched_count >= MAX_FLUID_UPDATES)
		return; // wavefront buffer full: the rest is handled once it drains
	s->fluid_sched[s->fluid_sched_count] = (struct fluid_pos) {
		.x = x, .y = y, .z = z,
	};
	s->fluid_hash[slot] = s->fluid_sched_count + 1;
	s->fluid_sched_count++;
}

void server_local_tick_fluids(struct server_local* s) {
	int n = s->fluid_sched_count;
	if(n == 0)
		return;

	// Snapshot this round's schedule; cells woken while we process (via
	// server_world_set_block -> schedule_fluid during the flush below) accumulate
	// fresh in fluid_sched and are handled next round -> one ring per round.
	static struct fluid_pos work[MAX_FLUID_UPDATES];
	memcpy(work, s->fluid_sched, (size_t)n * sizeof(struct fluid_pos));
	s->fluid_sched_count = 0;
	// clear the dedup table so next round's wakes start fresh
	memset(s->fluid_hash, 0, sizeof(s->fluid_hash));
	s->fluid_change_count = 0;

	for(int i = 0; i < n; i++) {
		struct block_data bd;
		if(!server_world_get_block(&s->world, work[i].x, work[i].y, work[i].z,
								   &bd))
			continue;
		if(bd.type != BLOCK_WATER_STILL && bd.type != BLOCK_WATER_FLOW)
			continue; // block changed to non-water since it was scheduled
		block_water_flow_update(s, &(struct block_info) {
			.block = &bd,
			.neighbours = NULL,
			.x = work[i].x,
			.y = work[i].y,
			.z = work[i].z,
		});
	}

	server_local_flush_fluid_changes(s);
}


bool place_block = false;

static void server_local_process(struct server_rpc* call, void* user) {
	assert(call && user);

	struct server_local* s = user;
#ifdef SPLITSCREEN
	uint8_t pid = call->player_id;
	if(pid >= MAX_SERVER_PLAYERS)
		pid = 0;
	struct server_player* player = &s->players[pid];
	s->active_player_id = pid;
#else
	struct server_player* player = &s->player;
#endif

	switch(call->type) {
		case SRPC_TOGGLE_PAUSE:
			s->paused = !s->paused;
			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_TIME_SET,
				.payload.time_set = s->world_time,
			});
			break;
		case SRPC_PLAYER_POS:
			// Accept position updates as soon as the player slot exists.
			player->x = call->payload.player_pos.x;
			player->y = call->payload.player_pos.y;
			player->z = call->payload.player_pos.z;
			player->rx = call->payload.player_pos.rx;
			player->ry = call->payload.player_pos.ry;
			player->old_vel_y = player->vel_y;
			player->vel_y = call->payload.player_pos.vel_y;
			player->has_pos = true;
			break;
		case SRPC_HOTBAR_SLOT:
			if(player->has_pos
			   && call->payload.hotbar_slot.slot < INVENTORY_SIZE_HOTBAR)
				inventory_set_hotbar(&player->inventory,
									 call->payload.hotbar_slot.slot);
			break;
		case SRPC_WINDOW_CLICK: {
			set_inv_slot_t changes;
			set_inv_slot_init(changes);

			bool accept = inventory_action(
				player->active_inventory, call->payload.window_click.slot,
				call->payload.window_click.right_click, changes);

			clin_rpc_send(&(struct client_rpc) {
				CRPC_PLAYER_ID(pid)
				.type = CRPC_WINDOW_TRANSACTION,
				.payload.window_transaction.accepted = accept,
				.payload.window_transaction.action_id
				= call->payload.window_click.action_id,
				.payload.window_transaction.window
				= call->payload.window_click.window,
			});

			server_local_send_inv_changes(pid, changes,
										  player->active_inventory,
										  call->payload.window_click.window);
			set_inv_slot_clear(changes);
			break;
		}
		case SRPC_WINDOW_CLOSE: {
			if(player->active_inventory->logic
			   && player->active_inventory->logic->on_close)
				player->active_inventory->logic->on_close(
					player->active_inventory);

			player->active_inventory = &player->inventory;
			break;
		}
		case SRPC_SET_GAMEMODE:
			if(player->has_pos) {
				if(call->payload.set_gamemode.toggle)
					player->creative = !player->creative;
				clin_rpc_send(&(struct client_rpc) {
					CRPC_PLAYER_ID(pid)
					.type = CRPC_GAMEMODE,
					.payload.gamemode.creative = player->creative,
				});
			}
			break;
		case SRPC_CREATIVE_PICK_BLOCK: {
			uint16_t id = call->payload.creative_pick_block.block_id;
			if(player->has_pos && player->creative && id > 0 && id < 256
			   && blocks[id] && blocks[id]->block_item.renderItem) {
				size_t slot = inventory_get_hotbar(&player->inventory)
					+ INVENTORY_SLOT_HOTBAR;
				struct item_data stack = {
					.id = id,
					.durability = 0,
					.count = blocks[id]->block_item.max_stack,
				};
				inventory_set_slot(&player->inventory, slot, stack);
				clin_rpc_send(&(struct client_rpc) {
					CRPC_PLAYER_ID(pid)
					.type = CRPC_INVENTORY_SLOT,
					.payload.inventory_slot.window = WINDOWC_INVENTORY,
					.payload.inventory_slot.slot = slot,
					.payload.inventory_slot.item = stack,
				});
			}
			break;
		}
		case SRPC_CREATIVE_SET_PICKED: {
			if(player->has_pos && player->creative) {
				uint16_t id = call->payload.creative_set_picked.item_id;
				struct item_data picked = {.id = 0, .durability = 0, .count = 0};
				if(id > 0 && id < ITEMS_MAX && items[id] && items[id]->renderItem) {
					picked.id = id;
					picked.count = items[id]->max_stack;
				}
				inventory_set_picked_item(&player->inventory, picked);
				clin_rpc_send(&(struct client_rpc) {
					CRPC_PLAYER_ID(pid)
					.type = CRPC_INVENTORY_SLOT,
					.payload.inventory_slot.window = WINDOWC_INVENTORY,
					.payload.inventory_slot.slot = SPECIAL_SLOT_PICKED_ITEM,
					.payload.inventory_slot.item = picked,
				});
			}
			break;
		}
		case SRPC_BLOCK_DIG:
			if(player->has_pos && call->payload.block_dig.y >= 0
			   && call->payload.block_dig.y < WORLD_HEIGHT
			   && call->payload.block_dig.finished) {
				struct block_data blk;
				if(server_world_get_block(&s->world, call->payload.block_dig.x,
										  call->payload.block_dig.y,
										  call->payload.block_dig.z, &blk)) {
					server_world_set_block(s, call->payload.block_dig.x,
										   call->payload.block_dig.y,
										   call->payload.block_dig.z,
										   (struct block_data) {
											   .type = BLOCK_AIR,
											   .metadata = 0,
										   });

					struct item_data it_data;
					bool has_tool = inventory_get_hotbar_item(
						&player->inventory, &it_data);
					struct item* it = has_tool ? item_get(&it_data) : NULL;

					if(!player->creative && blocks[blk.type]
					   && ((it
							&& it->tool.type == blocks[blk.type]->digging.tool
							&& it->tool.tier >= blocks[blk.type]->digging.min)
						   || blocks[blk.type]->digging.min == TOOL_TIER_ANY
						   || blocks[blk.type]->digging.tool == TOOL_TYPE_ANY))
						server_local_spawn_block_drops(
							s,
							&(struct block_info) {
								.block = &blk,
								.neighbours = NULL,
								.x = call->payload.block_dig.x,
								.y = call->payload.block_dig.y,
								.z = call->payload.block_dig.z,
							});
				}
			}
			break;
		case SRPC_BLOCK_PLACE:
			//printf("srpc_block_place");
			if(player->has_pos && call->payload.block_place.y >= 0
			   && call->payload.block_place.y < WORLD_HEIGHT) {
				int x, y, z;
				blocks_side_offset(call->payload.block_place.side, &x, &y, &z);

				struct block_data blk_where, blk_on;
				if(server_world_get_block(
					   &s->world, call->payload.block_place.x + x,
					   call->payload.block_place.y + y,
					   call->payload.block_place.z + z, &blk_where)
				   && server_world_get_block(
					   &s->world, call->payload.block_place.x,
					   call->payload.block_place.y, call->payload.block_place.z,
					   &blk_on)) {
					//printf("true 1");
					struct block_info where = (struct block_info) {
						.block = &blk_where,
						.neighbours = NULL,
						.x = call->payload.block_place.x + x,
						.y = call->payload.block_place.y + y,
						.z = call->payload.block_place.z + z,
					};

					struct block_info on = (struct block_info) {
						.block = &blk_on,
						.neighbours = NULL,
						.x = call->payload.block_place.x,
						.y = call->payload.block_place.y,
						.z = call->payload.block_place.z,
					};

					struct item_data it_data;
					inventory_get_hotbar_item(&player->inventory, &it_data);
					struct item* it = item_get(&it_data);
					bool placed = false;

					if(blocks[blk_on.type]
					   && blocks[blk_on.type]->onRightClick) {
						//printf("true 2");
						blocks[blk_on.type]->onRightClick(
							s, &it_data, &where, &on,
							call->payload.block_place.side);
						bool do_place = place_block;
						place_block = false;
						if (do_place && it && it->onItemPlace)
							placed = it->onItemPlace(
								s, &it_data, &where, &on,
								call->payload.block_place.side);
					} else if((!blocks[blk_where.type]
							   || blocks[blk_where.type]->place_ignore)
							  && it && it->onItemPlace
							  && (placed = it->onItemPlace(
								  s, &it_data, &where, &on,
								  call->payload.block_place.side))) {
						//printf("false2");
					}

					if(placed) {
						size_t slot
							= inventory_get_hotbar(&player->inventory);
						if(!player->creative)
							inventory_consume(&player->inventory,
											  slot + INVENTORY_SLOT_HOTBAR);

						clin_rpc_send(&(struct client_rpc) {
							CRPC_PLAYER_ID(pid)
							.type = CRPC_INVENTORY_SLOT,
							.payload.inventory_slot.window = WINDOWC_INVENTORY,
							.payload.inventory_slot.slot
							= slot + INVENTORY_SLOT_HOTBAR,
							.payload.inventory_slot.item
							= player->inventory
								  .items[slot + INVENTORY_SLOT_HOTBAR],
						});
					}
				}
			}
			break;
			case SRPC_UNLOAD_WORLD:
			// Splitscreen can send duplicate unload/save requests (e.g. from
			// multiple local players). If the world is already unloaded, ignore.
			if(string_get_cstr(s->level_name)[0] == '\0')
				break;
			if(!s->world_initialized)
				break;

			// save chunks here, then destroy all
			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_WORLD_RESET,
				.payload.world_reset.dimension = player->dimension,
				.payload.world_reset.local_entity = 0,
			});

			level_archive_write_player(
				&s->level, (vec3) {player->x, player->y, player->z},
				(vec2) {player->rx, player->ry}, NULL, player->dimension);

			level_archive_write_inventory(&s->level, &player->inventory);
			level_archive_write(&s->level, LEVEL_TIME, &s->world_time);
			{
				int32_t gm = player->creative ? 1 : 0;
				level_archive_write(&s->level, LEVEL_PLAYER_GAMEMODE, &gm);
			}

			level_archive_write(&s->level, LEVEL_PLAYER_HEALTH, &player->health);

			chest_archive_write(s->chest_pos, s->chest_items[0], s->level_name);
			furnace_archive_write(s->furnaces, s->level_name);
			sign_archive_write(s->sign_pos, s->sign_texts[0], s->level_name);

			dict_entity_it_t it;
			dict_entity_it(it, s->entities);

			while(!dict_entity_end_p(it)) {
				free(dict_entity_ref(it)->value);
				dict_entity_next(it);
			}
			dict_entity_reset(s->entities);
			server_world_destroy(&s->world);
			s->world_initialized = false;
			level_archive_destroy(&s->level);

			// In splitscreen, a single "save/unload world" must stop *all*
			// server players, otherwise the server tick continues and touches
			// a destroyed `s->world` (chunks dict no longer initialized).
#ifdef SPLITSCREEN
			for(int i = 0; i < MAX_SERVER_PLAYERS; i++) {
				s->players[i].has_pos = false;
				s->players[i].finished_loading = false;
			}
#else
			player->has_pos = false;
			player->finished_loading = false;
#endif
				string_reset(s->level_name);
				// Ensure we don't stay paused across worlds.
				s->paused = false;
				break;

		case SRPC_ENTITY_ATTACK:
		  uint32_t id = call->payload.entity_attack.entity_id;
		  struct entity **ptr = dict_entity_get(s->entities, id);
		  if (ptr && *ptr) {
		    struct entity *e = *ptr;
		    e->health -= 5;    // damage per hit
		    if (e->health <= 0) {
		      if (e->type == ENTITY_MINECART) {
		        vec3 pos = { e->pos[0], e->pos[1], e->pos[2] };
		        server_local_spawn_item(pos, &e->data.minecart.item, true, s);
		        e->delay_destroy = 0;
		      } else if (e->type == ENTITY_MONSTER) {
		        e->data.monster.fuse = 30;
		        e->ai_state = AI_FUSE;
		      }
		    }
		  }
		  break;
		case SRPC_PLAYER_ATTACK:
#ifdef SPLITSCREEN
		  if(call->payload.player_attack.target_player_id < splitscreen_player_count()
		     && call->payload.player_attack.target_player_id != pid) {
			uint8_t target_pid = call->payload.player_attack.target_player_id;
			struct server_player* target = &s->players[target_pid];
			if(target->has_pos) {
				server_local_set_player_health(
					s, target_pid, target->health - HEALTH_PER_HEART);
			}
		  }
#endif
		  break;
		case SRPC_LOAD_WORLD:
			#ifdef SPLITSCREEN
			{
				int player_count = splitscreen_player_count();
				for(int i = 0; i < player_count; i++) {
					s->players[i].has_pos = false;
					s->players[i].finished_loading = false;
					s->players[i].active_inventory = &s->players[i].inventory;
					s->players[i].oxygen = MAX_OXYGEN;
					s->players[i].health = MAX_PLAYER_HEALTH;
					inventory_clear(&s->players[i].inventory);
				}
			}
			#else
			assert(!player->has_pos);
			player->finished_loading = false;
			#endif

			string_set(s->level_name, call->payload.load_world.name);
			string_clear(call->payload.load_world.name);
			s->find_spawn = call->payload.load_world.find_spawn;
#ifdef SRPC_LOAD_WORLD_DEBUG
			printf("[DEBUG server_local SRPC_LOAD_WORLD] name='%s'\n", string_get_cstr(s->level_name));
#endif
			if(level_archive_create(&s->level, s->level_name)) {
				vec3 base_pos = {0.0f, 80.0f, 0.0f};
				vec2 base_rot = {0.0f, 0.0f};
				enum world_dim dim = WORLD_DIM_OVERWORLD;

				// Load base player state (single-player format). If missing, keep defaults.
				level_archive_read_player(&s->level, base_pos, base_rot, NULL, &dim);

				server_world_create(&s->world, s->level_name, dim);
				{
					int64_t world_seed = 0;
					if(!level_archive_read(&s->level, LEVEL_RANDOM_SEED, &world_seed, 0)
					   || world_seed == 0) {
						world_seed = server_local_hash_seed_from_name(
							string_get_cstr(s->level_name));
						level_archive_write(&s->level, LEVEL_RANDOM_SEED, &world_seed);
					}
					server_world_set_seed(&s->world, world_seed);
				}
				s->world_initialized = true;

				level_archive_read(&s->level, LEVEL_TIME, &s->world_time, 0);

				{
					int32_t gm = 0;
					level_archive_read(&s->level, LEVEL_PLAYER_GAMEMODE, &gm, 0);
					s->players[0].creative = (gm == 1);
				}

				// Read health/spawn if present (old `level.dat` may not have it).
				{
					short health = MAX_PLAYER_HEALTH;
					if(level_archive_read(&s->level, LEVEL_PLAYER_HEALTH, &health, 0)) {
						if(health > MAX_PLAYER_HEALTH)
							health = MAX_PLAYER_HEALTH;
						if(health < 0)
							health = MAX_PLAYER_HEALTH;
					}

					int spawn_x = (int)floorf(base_pos[0]);
					int spawn_y = (int)floorf(base_pos[1]);
					int spawn_z = (int)floorf(base_pos[2]);
					level_archive_read(&s->level, LEVEL_PLAYER_SPAWNX, &spawn_x, 0);
					level_archive_read(&s->level, LEVEL_PLAYER_SPAWNY, &spawn_y, 0);
					level_archive_read(&s->level, LEVEL_PLAYER_SPAWNZ, &spawn_z, 0);

#ifdef SPLITSCREEN
						int player_count = splitscreen_player_count();
						for(int i = 0; i < player_count; i++) {
							struct server_player* sp = &s->players[i];
						sp->dimension = dim;
						sp->x = base_pos[0] + (float)(i * 2);
						sp->y = base_pos[1];
						sp->z = base_pos[2] + (float)(i * 2);
						sp->rx = base_rot[0];
						sp->ry = base_rot[1];
						sp->fall_distance = 0.0f;
						sp->old_vel_y = 0;
						sp->vel_y = 0;
						sp->has_pos = true;
						sp->health = health;
						sp->spawn_x = spawn_x;
						sp->spawn_y = spawn_y;
							sp->spawn_z = spawn_z;
							sp->active_inventory = &sp->inventory;
							sp->finished_loading = false;
						}

					// Load only the primary inventory from `level.dat` (vanilla format).
					level_archive_read_inventory(&s->level, &s->players[0].inventory);
#else
						player->dimension = dim;
					player->x = base_pos[0];
					player->y = base_pos[1];
					player->z = base_pos[2];
					player->rx = base_rot[0];
					player->ry = base_rot[1];
					player->fall_distance = 0.0f;
					player->old_vel_y = 0;
					player->vel_y = 0;
					player->has_pos = true;
					player->health = health;
					player->spawn_x = spawn_x;
					player->spawn_y = spawn_y;
						player->spawn_z = spawn_z;
						player->active_inventory = &player->inventory;
						player->finished_loading = false;

						level_archive_read_inventory(&s->level, &player->inventory);
#endif
					}

				chest_archive_read(s->chest_pos, s->chest_items[0], s->level_name);
				furnace_archive_read(s->furnaces, s->level_name);
				sign_archive_read(s->sign_pos, s->sign_texts[0], s->level_name);

				dict_entity_reset(s->entities);

				clin_rpc_send(&(struct client_rpc) {
					.type = CRPC_WORLD_RESET,
					.payload.world_reset.dimension = dim,
					.payload.world_reset.local_entity = 0,
				});

				// Send initial player positions immediately so the client can
				// start rendering while chunks stream in.
#ifdef SPLITSCREEN
				for(int i = 0; i < splitscreen_player_count(); i++) {
					struct server_player* sp = &s->players[i];
					clin_rpc_send(&(struct client_rpc) {
						CRPC_PLAYER_ID(i)
						.type = CRPC_PLAYER_POS,
						.payload.player_pos.position
							= {sp->x, sp->y, sp->z},
						.payload.player_pos.rotation
							= {sp->rx, sp->ry},
					});
				}
#else
				clin_rpc_send(&(struct client_rpc) {
					.type = CRPC_PLAYER_POS,
					.payload.player_pos.position
						= {player->x, player->y, player->z},
					.payload.player_pos.rotation
						= {player->rx, player->ry},
				});
#endif

				clin_rpc_send(&(struct client_rpc) {
					.type = CRPC_TIME_SET,
					.payload.time_set = s->world_time,
				});

#ifdef SPLITSCREEN
				for(int i = 0; i < splitscreen_player_count(); i++) {
					clin_rpc_send(&(struct client_rpc) {
						CRPC_PLAYER_ID(i)
						.type = CRPC_PLAYER_SET_HEALTH,
						.payload.player_set_health.health = s->players[i].health
					});
				}
#else
				clin_rpc_send(&(struct client_rpc) {
					.type = CRPC_PLAYER_SET_HEALTH,
					.payload.player_set_health.health = player->health
				});
#endif
			}
			break;
	}
}

static void server_local_update(struct server_local* s) {
	assert(s);

	/* Start of this server tick. In game we generate chunks with whatever time
	 * is left in the ~50ms tick after the game logic ran, so we measure from
	 * here (see the chunk loop below). */
	ptime_t tick_begin = time_get();

	// print TPS
	#ifdef PRINT_TPS
	ptime_t this_tick = time_get();
	float dt = time_diff_s(s->last_tick, this_tick);
	float tps = 1.0F / dt;
	s->last_tick = this_tick;
	printf("%f\n", tps);
	#endif

	svin_process_messages(server_local_process, s, false);

	#ifdef SPLITSCREEN
		int max_players = 4;
		bool any_active = false;
		for(int i = 0; i < max_players; i++) {
			if(s->players[i].has_pos) any_active = true;
		}
		if(!any_active || s->paused)
			return;
	#else
		if(!s->player.has_pos || s->paused)
			return;
	#endif

	// World might not be loaded yet (or just got unloaded). Avoid touching the
	// server_world / chunk dict in that case (m-dict iterators assert when the
	// dict isn't initialized).
	if(!s->world_initialized || !s->world.initialized
	   || s->world.chunks->index == NULL)
		return;

	s->world_time++;

	/* Entity-Keys vor dem Tick in ein Array snapshotten. tick_server() kann
	 * dict_entity_safe_get() aufrufen (z.B. via server_local_spawn_item),
	 * was einen Rehash ausloesen und den Iterator invalidieren wuerde. Durch
	 * das Snapshot-Muster wird das Dict waehrend des Ticks nicht iteriert. */
#define ENTITY_TICK_CAP 512
	uint32_t tick_keys[ENTITY_TICK_CAP];
	int tick_count = 0;
	{
		dict_entity_it_t snap;
		dict_entity_it(snap, s->entities);
		while(!dict_entity_end_p(snap)) {
			if(dict_entity_ref(snap)->value->tick_server
			   && tick_count < ENTITY_TICK_CAP)
				tick_keys[tick_count++] = dict_entity_ref(snap)->key;
			dict_entity_next(snap);
		}
	}

	for(int _i = 0; _i < tick_count; _i++) {
		uint32_t key = tick_keys[_i];
		struct entity** ep = dict_entity_get(s->entities, key);
		if(!ep) continue; /* wurde von einem frueheren Tick in diesem Frame entfernt */
		struct entity* e = *ep;

		bool remove = (e->delay_destroy == 0) || e->tick_server(e, s);

		if(remove) {
			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_ENTITY_DESTROY,
				.payload.entity_destroy.entity_id = key,
			});

			free(e);
			dict_entity_erase(s->entities, key);
		} else if(e->delay_destroy < 0) {
			// TODO: find a more optimized way of moving entities on both client and server
			/*
			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_ENTITY_MOVE,
				.payload.entity_move.entity_id = key,
				.payload.entity_move.pos
				= {e->pos[0], e->pos[1], e->pos[2]},
			});
			*/
		}
	}
#undef ENTITY_TICK_CAP

#ifdef SPLITSCREEN
		w_coord_t px = WCOORD_CHUNK_OFFSET(floor(s->players[0].x));
		w_coord_t pz = WCOORD_CHUNK_OFFSET(floor(s->players[0].z));
		float player_x = (float)s->players[0].x;
		float player_y = (float)s->players[0].y;
		float player_z = (float)s->players[0].z;
#else
		w_coord_t px = WCOORD_CHUNK_OFFSET(floor(s->player.x));
		w_coord_t pz = WCOORD_CHUNK_OFFSET(floor(s->player.z));
		float player_x = (float)s->player.x;
		float player_y = (float)s->player.y;
		float player_z = (float)s->player.z;
#endif
#ifdef SPLITSCREEN
	bool p1_active = false;
	w_coord_t px1 = px;
	w_coord_t pz1 = pz;
	if(s->players[1].has_pos && gstate.num_players > 1) {
		px1 = WCOORD_CHUNK_OFFSET(floor(s->players[1].x));
		pz1 = WCOORD_CHUNK_OFFSET(floor(s->players[1].z));
		p1_active = true;
	}
#endif

	server_world_random_tick(&s->world, &s->rand_src, s, px, pz,
							 MAX_VIEW_DISTANCE - 2);
	server_world_tick(&s->world, s);
	/* Water flow: every 5th tick, process the cells woken by recent block
	 * changes (Minecraft's fluid tick rate). Static/generated water that nobody
	 * disturbed is never scheduled, so it stays put and doesn't churn. */
	if(s->world_time % 5 == 0)
		server_local_tick_fluids(s);
	server_local_try_spawn_nearby_animal(s, player_x, player_y, player_z);
	server_local_try_spawn_nearby_dark_monster(s, player_x, player_y,
												player_z);

	w_coord_t cx, cz;
	/* O(1): m-lib keeps the element count, so don't walk the whole dictionary
	 * every tick (that made each tick O(N) and the whole generation O(N^2) --
	 * the more chunks were loaded, the slower generation got). */
	size_t loaded_chunks = dict_server_chunks_size(s->world.chunks);
#ifdef SPLITSCREEN
	{
		size_t max_allowed = (size_t)(2 * MAX_VIEW_DISTANCE + 1)
			* (size_t)(2 * MAX_VIEW_DISTANCE + 1);
		if(p1_active)
			max_allowed *= 2;

		if(loaded_chunks <= max_allowed) {
			/* Avoid unload thrash when we're already under the target budget. */
			goto unload_done;
		}

		bool unload_found = false;
		w_coord_t unload_x = 0;
		w_coord_t unload_z = 0;
			w_coord_t unload_dist2 = 0;

			dict_server_chunks_it_t u_it;
			dict_server_chunks_it(u_it, s->world.chunks);
			while(!dict_server_chunks_end_p(u_it)) {
				int64_t id = dict_server_chunks_ref(u_it)->key;
				w_coord_t ucx = S_CHUNK_X(id);
				w_coord_t ucz = S_CHUNK_Z(id);
			w_coord_t d0 = CHUNK_DIST2(px, ucx, pz, ucz);
			w_coord_t d = d0;
			if(p1_active) {
				w_coord_t d1 = CHUNK_DIST2(px1, ucx, pz1, ucz);
				if(d1 < d)
					d = d1;
			}
			if(d > MAX_VIEW_DISTANCE * MAX_VIEW_DISTANCE
			   && (!unload_found || d > unload_dist2)) {
				unload_found = true;
				unload_dist2 = d;
				unload_x = ucx;
				unload_z = ucz;
			}

			dict_server_chunks_next(u_it);
		}

		if(unload_found) {
			// unload just one chunk
#ifdef SRPC_LOAD_WORLD_DEBUG
			printf("[server_local] unloading chunk %d,%d (player chunk %d,%d) loaded_chunks=%zu\n",
				   (int)unload_x, (int)unload_z, (int)px, (int)pz, loaded_chunks);
#endif
			server_world_save_chunk(&s->world, true, unload_x, unload_z);
			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_UNLOAD_CHUNK,
				.payload.unload_chunk.x = unload_x,
				.payload.unload_chunk.z = unload_z,
			});
		}
	}
unload_done:
#else
	if(server_world_furthest_chunk(&s->world, MAX_VIEW_DISTANCE, px, pz, &cx,
								   &cz)) {
#ifdef SRPC_LOAD_WORLD_DEBUG
		// unload just one chunk
		printf("[server_local] unloading chunk %d,%d (player chunk %d,%d) loaded_chunks=%zu\n",
			   (int)cx, (int)cz, (int)px, (int)pz, loaded_chunks);
#endif
		server_world_save_chunk(&s->world, true, cx, cz);
		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_UNLOAD_CHUNK,
			.payload.unload_chunk.x = cx,
			.payload.unload_chunk.z = cz,
		});
	}
#endif

	// iterate over all chunks that should be loaded
	bool c_nearest = false;
	w_coord_t c_nearest_x, c_nearest_z;
	w_coord_t c_nearest_dist2;
	w_coord_t min_px = px;
	w_coord_t max_px = px;
	w_coord_t min_pz = pz;
	w_coord_t max_pz = pz;
#ifdef SPLITSCREEN
	if(p1_active) {
		if(px1 < min_px) min_px = px1;
		if(px1 > max_px) max_px = px1;
		if(pz1 < min_pz) min_pz = pz1;
		if(pz1 > max_pz) max_pz = pz1;
	}
#endif
	for(w_coord_t z = min_pz - MAX_VIEW_DISTANCE;
		z <= max_pz + MAX_VIEW_DISTANCE; z++) {
		for(w_coord_t x = min_px - MAX_VIEW_DISTANCE;
			x <= max_px + MAX_VIEW_DISTANCE; x++) {
			w_coord_t d = CHUNK_DIST2(px, x, pz, z);
#ifdef SPLITSCREEN
			if(p1_active) {
				w_coord_t d1 = CHUNK_DIST2(px1, x, pz1, z);
				if(d1 < d)
					d = d1;
			}
#endif
			if(d > MAX_VIEW_DISTANCE * MAX_VIEW_DISTANCE)
				continue;
			if(!server_world_is_chunk_loaded(&s->world, x, z)
			   && (d < c_nearest_dist2 || !c_nearest)) {
				c_nearest_dist2 = d;
				c_nearest_x = x;
				c_nearest_z = z;
				c_nearest = true;
			}
		}
	}

#ifdef SPLITSCREEN
	int num_players = splitscreen_player_count();
	bool finished_loading = true;
	for (int i = 0; i < num_players; i++) {
		if (!s->players[i].finished_loading) {
			finished_loading = false;
			break;
		}
	}
#else
	bool finished_loading = s->player.finished_loading;
#endif

	/* Chunk build speed is configurable via gstate.settings (applies both while
	 * loading AND while streaming in game):
	 *  - chunk_build_per_tick: work units per server tick. While generating a new
	 *    world it counts *whole chunks* finished per tick; otherwise it counts
	 *    generation/load steps per tick.
	 *  - chunk_build_budget_ms: time budget per tick in milliseconds. NOTE: in
	 *    game the chunk loop shares the 50ms tick with game logic, so a very
	 *    large budget can make gameplay run in slow motion while it generates. */
	bool generating = s->find_spawn;

	int chunk_build = gstate.settings.chunk_build_per_tick;
	if(chunk_build < 1)
		chunk_build = 1;

	int load_per_tick;
	int chunk_budget_ms;
	(void)tick_begin;
	if(finished_loading) {
		/* IN GAME: generation speed is controlled by the user settings
		 * (chunk_build_*). Thanks to the mesher running at a HIGHER thread
		 * priority than this server thread, meshing always preempts generation,
		 * so rendering/block edits stay responsive even at a large budget --
		 * raising the budget just lets generation use more of each tick.
		 *  - chunk_build_budget_ms = milliseconds of each ~50ms tick spent
		 *    generating (the main knob). Going much above ~40ms eats the whole
		 *    tick, so the game clock (movement etc.) starts to run slow.
		 *  - chunk_build_per_tick = hard cap on generation steps per tick (set
		 *    high to let the time budget be the only limit). */
		chunk_budget_ms = gstate.settings.chunk_build_budget_ms;
		if(chunk_budget_ms < 1)
			chunk_budget_ms = 1;
		load_per_tick = chunk_build;
	} else {
		/* LOADING SCREEN: always run at full speed, independent of the in-game
		 * settings (the server spins back-to-back here, nothing to keep at 50ms). */
#ifdef PLATFORM_WII
		chunk_budget_ms = 60;
#else
		chunk_budget_ms = 80;
#endif
		load_per_tick = 1000000; /* time is the only cap */
	}

	/* While loading/generating (loading screen, no game logic that must stay at
	 * 50ms) the server thread ticks back-to-back; in game it keeps a steady
	 * ~50ms tick (handled in server_local_thread). */
	s->loading = !finished_loading;

	int loaded_this_tick = 0;
	/* true once there is no candidate chunk left to load/generate in range */
	bool all_loaded = false;
	ptime_t chunk_load_start = time_get();
	for(int load_i = 0;; load_i++) {
		if(time_diff_ms(chunk_load_start, time_get()) >= chunk_budget_ms)
			break;
		if(generating) {
			/* new-world spawn generation happens on the loading screen -> full
			 * speed, bounded only by the loading time budget above (NOT by the
			 * in-game chunk_build_per_tick setting). */
			if(loaded_this_tick >= 1000000) /* whole chunks per tick */
				break;
		} else {
			if(load_i >= load_per_tick) /* steps per tick */
				break;
		}

		/* recompute nearest disk candidate each iteration */
		bool c_found = false;
		w_coord_t cand_x = 0, cand_z = 0;
		w_coord_t cand_dist2 = 0;
		if(server_world_pending_chunk(&s->world, &cand_x, &cand_z)) {
			c_found = true;
		}
		if(!c_found) {
		for(w_coord_t z = min_pz - MAX_VIEW_DISTANCE;
			z <= max_pz + MAX_VIEW_DISTANCE; z++) {
			for(w_coord_t x = min_px - MAX_VIEW_DISTANCE;
				x <= max_px + MAX_VIEW_DISTANCE; x++) {
				w_coord_t d = CHUNK_DIST2(px, x, pz, z);
#ifdef SPLITSCREEN
				if(p1_active) {
					w_coord_t d1 = CHUNK_DIST2(px1, x, pz1, z);
					if(d1 < d)
						d = d1;
				}
#endif
				if(d > MAX_VIEW_DISTANCE * MAX_VIEW_DISTANCE)
					continue;
				if(!server_world_is_chunk_loaded(&s->world, x, z)
				   && (d < cand_dist2 || !c_found)) {
					cand_dist2 = d;
					cand_x = x;
					cand_z = z;
					c_found = true;
				}
			}
		}
		}

		if(!c_found) {
			all_loaded = true; /* nothing left in range -> done */
			break;
		}

		struct server_chunk* sc;
		if(server_world_load_chunk(&s->world, cand_x, cand_z, &sc)) {
			size_t sz = CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
			void* ids = malloc(sz);
			void* metadata = malloc(sz / 2);
			void* lighting_sky = malloc(sz / 2);
			void* lighting_torch = malloc(sz / 2);

			memcpy(ids, sc->ids, sz);
			memcpy(metadata, sc->metadata, sz / 2);
			memcpy(lighting_sky, sc->lighting_sky, sz / 2);
			memcpy(lighting_torch, sc->lighting_torch, sz / 2);

			clin_rpc_send(&(struct client_rpc) {
				.type = CRPC_CHUNK,
				.payload.chunk.x = cand_x * CHUNK_SIZE,
				.payload.chunk.y = 0,
				.payload.chunk.z = cand_z * CHUNK_SIZE,
				.payload.chunk.sx = CHUNK_SIZE,
				.payload.chunk.sy = WORLD_HEIGHT,
				.payload.chunk.sz = CHUNK_SIZE,
				.payload.chunk.ids = ids,
				.payload.chunk.metadata = metadata,
				.payload.chunk.lighting_sky = lighting_sky,
				.payload.chunk.lighting_torch = lighting_torch,
			});

			loaded_this_tick++;
		} else {
#ifdef SRPC_LOAD_WORLD_DEBUG
			/* failed to load this candidate, try next iteration */
			printf("[server_local] server_world_load_chunk failed for %d,%d\n",
				   (int)cand_x, (int)cand_z);
#endif
		}
	}

	/* debug overlay: report current chunk generation progress */
	{
		int prog = server_world_pending_progress(&s->world);
		if(prog >= 0) {
			w_coord_t pcx = 0, pcz = 0;
			server_world_pending_chunk(&s->world, &pcx, &pcz);
			gstate.gen_debug.active = true;
			gstate.gen_debug.percent = prog;
			gstate.gen_debug.chunk_x = (int)pcx;
			gstate.gen_debug.chunk_z = (int)pcz;
		} else {
			gstate.gen_debug.active = false;
			gstate.gen_debug.percent = 100;
		}
		gstate.gen_debug.built += (unsigned long)loaded_this_tick;
	}

	/* When is loading "done"?
	 *  - new world (find_spawn): only once the whole spawn area is generated
	 *    (all_loaded), so the player never starts in ungenerated terrain.
	 *  - existing world: as before -> as soon as a tick loads no more chunks
	 *    from disk (loaded_this_tick == 0). Missing chunks are generated later,
	 *    in-game, while walking. */
	bool load_done = s->find_spawn ? all_loaded : (loaded_this_tick == 0);

#ifdef SPLITSCREEN
	if(load_done && !finished_loading) {
		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_TIME_SET,
			.payload.time_set = s->world_time,
		});

		for(int i = 0; i < num_players; i++) {
			struct server_player* player = &s->players[i];
			if(player->finished_loading)
				continue;

			/* new world: drop the spawn onto real ground near the centre */
			if(s->find_spawn) {
				w_coord_t sx = (w_coord_t)floor(player->x);
				w_coord_t sz = (w_coord_t)floor(player->z);
				w_coord_t fx, fz;
				int fy;
				if(server_world_find_spawn(&s->world, sx, sz,
										   (MAX_VIEW_DISTANCE - 1) * CHUNK_SIZE,
										   &fx, &fy, &fz)) {
					/* player Y is the eye position (feet + ~1.62) */
					player->x = fx + 0.5;
					player->y = (double)fy + 1.62;
					player->z = fz + 0.5;
					player->spawn_x = fx;
					player->spawn_y = fy + 2;
					player->spawn_z = fz;
				}
			}

			clin_rpc_send(&(struct client_rpc) {
				CRPC_PLAYER_ID(i)
				.type = CRPC_PLAYER_POS,
				.payload.player_pos.position = {player->x, player->y, player->z},
				.payload.player_pos.rotation = {player->rx, player->ry},
			});

#ifdef SRPC_LOAD_WORLD_DEBUG
			printf("[server_local] finished loading for player %d near %d,%d (loaded_chunks=%zu)\n",
				   i, (int)WCOORD_CHUNK_OFFSET(floor(player->x)),
				   (int)WCOORD_CHUNK_OFFSET(floor(player->z)), loaded_chunks);
#endif

			for(size_t k = 0; k < INVENTORY_SIZE; k++) {
				if(player->inventory.items[k].id > 0) {
					clin_rpc_send(&(struct client_rpc) {
						CRPC_PLAYER_ID(i)
						.type = CRPC_INVENTORY_SLOT,
						.payload.inventory_slot.window = WINDOWC_INVENTORY,
						.payload.inventory_slot.slot = k,
						.payload.inventory_slot.item
						= player->inventory.items[k],
					});
				}
			}

			player->finished_loading = true;
		}

		s->find_spawn = false;

		/* spawn area is loaded and the hotbars were sent -> start the game */
		clin_rpc_send(&(struct client_rpc) {
			CRPC_PLAYER_ID(0)
			.type = CRPC_GAMEMODE,
			.payload.gamemode.creative = s->players[0].creative,
		});
		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_WORLD_LOADED,
		});
	}
#else
	if(load_done && !s->player.finished_loading) {
		struct client_rpc pos;
		pos.type = CRPC_PLAYER_POS;
		if(s->find_spawn) {
			/* new world: drop the spawn onto real ground near the centre */
			w_coord_t sx = (w_coord_t)floor(s->player.x);
			w_coord_t sz = (w_coord_t)floor(s->player.z);
			w_coord_t fx, fz;
			int fy;
			if(server_world_find_spawn(&s->world, sx, sz,
									   (MAX_VIEW_DISTANCE - 1) * CHUNK_SIZE, &fx,
									   &fy, &fz)) {
				/* player Y is the eye position (feet + ~1.62) */
				s->player.x = fx + 0.5;
				s->player.y = (double)fy + 1.62;
				s->player.z = fz + 0.5;
				s->player.spawn_x = fx;
				s->player.spawn_y = fy + 2;
				s->player.spawn_z = fz;
			}
			s->find_spawn = false;

			pos.payload.player_pos.position[0] = s->player.x;
			pos.payload.player_pos.position[1] = s->player.y;
			pos.payload.player_pos.position[2] = s->player.z;
			pos.payload.player_pos.rotation[0] = s->player.rx;
			pos.payload.player_pos.rotation[1] = s->player.ry;
			clin_rpc_send(&pos);
		} else if(level_archive_read_player(&s->level,
										   pos.payload.player_pos.position,
										   pos.payload.player_pos.rotation, NULL,
										   NULL)) {
			clin_rpc_send(&pos);
		}

		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_TIME_SET,
			.payload.time_set = s->world_time,
		});
#ifdef SRPC_LOAD_WORLD_DEBUG
		printf("[server_local] no disk chunk candidate found near player %d,%d (loaded_chunks=%zu)\n",
			   (int)px, (int)pz, loaded_chunks);
#endif

		if(level_archive_read_inventory(&s->level, &s->player.inventory)) {
			for(size_t k = 0; k < INVENTORY_SIZE; k++) {
				if(s->player.inventory.items[k].id > 0) {
					clin_rpc_send(&(struct client_rpc) {
						.type = CRPC_INVENTORY_SLOT,
						.payload.inventory_slot.window = WINDOWC_INVENTORY,
						.payload.inventory_slot.slot = k,
						.payload.inventory_slot.item
						= s->player.inventory.items[k],
					});
				}
			}
		}

		s->player.finished_loading = true;

		/* spawn area is loaded and the hotbar was sent -> start the game */
		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_WORLD_LOADED,
		});
	}
#endif

#ifdef SPLITSCREEN


for (int i = 0; i < 4; i++) {
			struct server_player* player = &s->players[i];




		// check if player is underwater
		// server side X off by one?
		struct block_data blk;
		server_world_get_block(&s->world, player->x-1, player->y, player->z, &blk);
		bool in_water = (blk.type == BLOCK_WATER_STILL || blk.type == BLOCK_WATER_FLOW);
		bool in_lava = (blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW);
		int feet_y = (int)floor(player->y - 1.62);
		struct block_data blk_climb, blk_climb2;
		server_world_get_block(&s->world, player->x, feet_y,     player->z, &blk_climb);
		server_world_get_block(&s->world, player->x, feet_y + 1, player->z, &blk_climb2);
		bool on_climbable = (blk_climb.type  == BLOCK_LADDER || blk_climb.type  == BLOCK_VINE
		                  || blk_climb2.type == BLOCK_LADDER || blk_climb2.type == BLOCK_VINE);
		if(player->y != 0) {
			server_world_get_block(&s->world, player->x-1, player->y-1, player->z, &blk);
			if(blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW) in_lava = true;
		}

		// landing check first, before fall_distance gets reset
		bool falling = player->vel_y < -0.079f;
		if(player->old_vel_y < -0.079f && player->vel_y >= -0.079f) {
			struct block_data blk_below;
			server_world_get_block(&s->world, player->x-1, player->y-1, player->z, &blk_below);
			bool landed_in_water = in_water
				|| blk_below.type == BLOCK_WATER_STILL
				|| blk_below.type == BLOCK_WATER_FLOW;
			int fall_blocks = (int)player->fall_distance;

#ifdef FALL_HEALTH_DEBUG
			if(i == 0) printf("[LAND p%d] fall_dist=%.2f (%d Bloecke) wasser=%s -> schaden=%d Herzen\n",
				i, player->fall_distance, fall_blocks,
				landed_in_water ? "JA" : "nein",
				(fall_blocks >= 4 && !landed_in_water) ? fall_blocks-3 : 0);
#endif

			if(fall_blocks >= 4 && server_local_damage_enabled() && !landed_in_water) {
				server_local_set_player_health(s, i, player->health-HEALTH_PER_HEART*(fall_blocks-3));
			}
			player->fall_distance = 0.0f;
		}

		// accumulate fall distance
		if(in_water || on_climbable) {
			player->fall_distance = 0.0f;
		} else if(falling) {
			player->fall_distance -= player->vel_y;
		} else {
			player->fall_distance = 0.0f;
		}

#ifdef FALL_HEALTH_DEBUG
		if(i == 0) printf("[FALL p%d] pos=(%.2f,%.2f,%.2f) feet_y=%d blk=%d/%d vel_y=%.3f fall_dist=%.2f | falle=%s leiter=%s wasser=%s\n",
			i, player->x, player->y, player->z,
			feet_y, blk_climb.type, blk_climb2.type,
			player->vel_y, player->fall_distance,
			falling ? "JA" : "nein",
			on_climbable ? "JA" : "nein",
			in_water ? "JA" : "nein");
#endif

		if(in_lava) {
			// damage player in lava every 8 ticks
			if((player->oxygen & 7) == 0 && server_local_damage_enabled()) {
				server_local_set_player_health(s, i, player->health-HEALTH_PER_HEART*2);
			}
			player->oxygen--;
		} else if(in_water) {
			// damage drowning player every 32 ticks
			if(player->oxygen <= OXYGEN_THRESHOLD && (player->oxygen&31) == 0
			   && server_local_damage_enabled()) {
				server_local_set_player_health(s, i, player->health-HEALTH_PER_HEART);
			}
			player->oxygen--;
		} else player->oxygen = MAX_OXYGEN;
	}
#else
	// check if player is underwater
	// server side X off by one?
	struct block_data blk;
	server_world_get_block(&s->world, s->player.x-1, s->player.y, s->player.z, &blk);
	bool in_water = (blk.type == BLOCK_WATER_STILL || blk.type == BLOCK_WATER_FLOW);
	bool in_lava = (blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW);
	int feet_y = (int)floor(s->player.y - 1.62);
	struct block_data blk_climb, blk_climb2;
	server_world_get_block(&s->world, s->player.x, feet_y,     s->player.z, &blk_climb);
	server_world_get_block(&s->world, s->player.x, feet_y + 1, s->player.z, &blk_climb2);
	bool on_climbable = (blk_climb.type  == BLOCK_LADDER || blk_climb.type  == BLOCK_VINE
	                  || blk_climb2.type == BLOCK_LADDER || blk_climb2.type == BLOCK_VINE);
	if(s->player.y != 0) {
		server_world_get_block(&s->world, s->player.x-1, s->player.y-1, s->player.z, &blk);
		if(blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW) in_lava = true;
	}

	// landing check first, before fall_distance gets reset
	bool falling = s->player.vel_y < -0.079f;
	if(s->player.old_vel_y < -0.079f && s->player.vel_y >= -0.079f) {
		struct block_data blk_below;
		server_world_get_block(&s->world, s->player.x-1, s->player.y-1, s->player.z, &blk_below);
		bool landed_in_water = in_water
			|| blk_below.type == BLOCK_WATER_STILL
			|| blk_below.type == BLOCK_WATER_FLOW;
		int fall_blocks = (int)s->player.fall_distance;

#ifdef FALL_HEALTH_DEBUG
		printf("[LAND] fall_dist=%.2f (%d Bloecke) wasser=%s -> schaden=%d Herzen\n",
			s->player.fall_distance, fall_blocks,
			landed_in_water ? "JA" : "nein",
			(fall_blocks >= 4 && !landed_in_water) ? fall_blocks-3 : 0);
#endif

		if(fall_blocks >= 4 && server_local_damage_enabled() && !landed_in_water) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART*(fall_blocks-3));
		}
		s->player.fall_distance = 0.0f;
	}

	// accumulate fall distance
	if(in_water || on_climbable) {
		s->player.fall_distance = 0.0f;
	} else if(falling) {
		s->player.fall_distance -= s->player.vel_y;
	} else {
		s->player.fall_distance = 0.0f;
	}

#ifdef FALL_HEALTH_DEBUG
	printf("[FALL] pos=(%.2f,%.2f,%.2f) feet_y=%d blk=%d/%d vel_y=%.3f fall_dist=%.2f | falle=%s leiter=%s wasser=%s\n",
		s->player.x, s->player.y, s->player.z,
		feet_y, blk_climb.type, blk_climb2.type,
		s->player.vel_y, s->player.fall_distance,
		falling ? "JA" : "nein",
		on_climbable ? "JA" : "nein",
		in_water ? "JA" : "nein");
#endif

	if(in_lava) {
		// damage player in lava every 8 ticks
		if((s->player.oxygen & 7) == 0 && server_local_damage_enabled()) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART*2);
		}
		s->player.oxygen--;
	} else if(in_water) {
		// damage drowning player every 32 ticks
		if(s->player.oxygen <= OXYGEN_THRESHOLD && (s->player.oxygen&31) == 0
		   && server_local_damage_enabled()) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART);
		}
		s->player.oxygen--;
	} else s->player.oxygen = MAX_OXYGEN;
#endif
}

static void* server_local_thread(void* user) {
	struct server_local* s = user;
	ptime_t last_start = time_get();
	gstate.stats.server_tps = 20.0f; /* sensible initial reading */
	while(1) {
		ptime_t tick_start = time_get();

		/* effective tick period incl. sleep -> real ticks/second. Lightly
		 * smoothed so the debug number is readable. A value clearly below 20
		 * means the server thread can't keep up and the game runs slow. */
		int32_t period_ms = time_diff_ms(last_start, tick_start);
		last_start = tick_start;
		if(period_ms > 0) {
			float inst_tps = 1000.0f / (float)period_ms;
			gstate.stats.server_tps
				= gstate.stats.server_tps * 0.9f + inst_tps * 0.1f;
		}

		server_local_update(s);

		/* pure work time of one tick (world tick + fluids + chunk gen). If this
		 * exceeds 50ms the 20 TPS target can't be met -> water/mobs slow down. */
		gstate.stats.server_tick_ms
			= (float)time_diff_ms(tick_start, time_get());

		if(s->loading) {
			/* loading screen: no game logic to pace, tick back-to-back */
			thread_msleep(1);
		} else {
			/* keep a steady ~50ms game tick, but only sleep the part of it that
			 * was NOT already spent generating chunks -> the formerly idle time
			 * is used for generation without slowing the game tick. */
			int sleep_ms = 50 - (int)time_diff_ms(tick_start, time_get());
			thread_msleep(sleep_ms < 1 ? 1 : sleep_ms);
		}
	}
	return NULL;
}

void server_local_create(struct server_local* s) {
	assert(s);
	// Ensure `s->world` starts in a known safe state. Some m-lib dict iterators
	// assert when used on uninitialized dicts; we guard with `world.initialized`
	// but also zero-init to avoid garbage state.
	memset(&s->world, 0, sizeof(s->world));
	rand_gen_seed(&s->rand_src);
	s->paused = false;
	s->world_time = 0;
	s->active_player_id = 0;
	s->world_initialized = false;
	s->loading = false;
	s->find_spawn = false;
	s->last_tick = time_get();
	s->fluid_change_count = 0;
	s->fluid_sched_count = 0;
	memset(s->fluid_hash, 0, sizeof(s->fluid_hash));

	string_init(s->level_name);

	dict_entity_init(s->entities);
	memset(s->chest_pos, -1, MAX_CHESTS * 3 * sizeof(int));
	memset(s->buttons, -1, sizeof(s->buttons));
	memset(s->repeaters, -1, sizeof(s->repeaters));
	memset(s->sign_pos, -1, MAX_SIGNS * 3 * sizeof(int));
	memset(s->brewing_stands, 0, sizeof(s->brewing_stands));
	memset(s->enchanting_tables, 0, sizeof(s->enchanting_tables));
	for(int i = 0; i < MAX_BREWING_STANDS; i++) {
		s->brewing_stands[i].pos.x = -1;
		s->brewing_stands[i].pos.y = -1;
		s->brewing_stands[i].pos.z = -1;
		s->brewing_stands[i].brew_time = 0;
		s->brewing_stands[i].brew_total = 0;
	}
	for(int i = 0; i < MAX_ENCHANTING_TABLES; i++) {
		s->enchanting_tables[i].pos.x = -1;
		s->enchanting_tables[i].pos.y = -1;
		s->enchanting_tables[i].pos.z = -1;
	}

#ifdef SPLITSCREEN
	for(int i = 0; i < MAX_SERVER_PLAYERS; i++) {
		s->players[i].has_pos = false;
		s->players[i].finished_loading = false;
		s->players[i].dimension = WORLD_DIM_OVERWORLD;
		s->players[i].rx = 0.0f;
		s->players[i].ry = 0.0f;
		s->players[i].x = 0.0;
		s->players[i].y = 0.0;
		s->players[i].z = 0.0;
		s->players[i].vel_y = 0.0f;
		s->players[i].old_vel_y = 0.0f;
		s->players[i].fall_distance = 0.0f;
		s->players[i].oxygen = MAX_OXYGEN;
		s->players[i].health = MAX_PLAYER_HEALTH;
		s->players[i].spawn_x = 0;
		s->players[i].spawn_y = 80;
		s->players[i].spawn_z = 0;
		memset(s->players[i].ender_chest_items, 0,
			   sizeof(s->players[i].ender_chest_items));

		inventory_create(&s->players[i].inventory, &inventory_logic_player, s,
						 INVENTORY_SIZE, 0, 0, 0);
		s->players[i].active_inventory = &s->players[i].inventory;
	}
#endif

	struct thread t;
	thread_create(&t, server_local_thread, s, 8);
}
