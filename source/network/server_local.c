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
#ifdef SPLITSCREEN
#include "../game/game_state.h"
#endif
#include "client_interface.h"
#include "inventory_logic.h"
#include "server_interface.h"
#include "server_local.h"
#include "server_world.h"
#include "complex_block_archive.h"

#define CHUNK_DIST2(x1, x2, z1, z2)                                            \
	(((x1) - (x2)) * ((x1) - (x2)) + ((z1) - (z2)) * ((z1) - (z2)))


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

		//respawn with half health
		player->health = MAX_PLAYER_HEALTH/2;
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

					if(blocks[blk.type]
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

			level_archive_write(&s->level, LEVEL_PLAYER_HEALTH, &player->health);

			chest_archive_write(s->chest_pos, s->chest_items[0], s->level_name);
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

			printf("[DEBUG server_local SRPC_LOAD_WORLD] name='%s'\n", string_get_cstr(s->level_name));
			if(level_archive_create(&s->level, s->level_name)) {
				vec3 base_pos = {0.0f, 80.0f, 0.0f};
				vec2 base_rot = {0.0f, 0.0f};
				enum world_dim dim = WORLD_DIM_OVERWORLD;

				// Load base player state (single-player format). If missing, keep defaults.
				level_archive_read_player(&s->level, base_pos, base_rot, NULL, &dim);

				server_world_create(&s->world, s->level_name, dim);
				s->world_initialized = true;

				level_archive_read(&s->level, LEVEL_TIME, &s->world_time, 0);

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
						sp->fall_y = (int)sp->y;
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
					player->fall_y = player->y;
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

	dict_entity_it_t it;
	dict_entity_it(it, s->entities);

	while(!dict_entity_end_p(it)) {
		uint32_t key = dict_entity_ref(it)->key;
		struct entity* e = dict_entity_ref(it)->value;

		if(e->tick_server) {
			bool remove = (e->delay_destroy == 0) || e->tick_server(e, s);
			dict_entity_next(it);

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
		} else {
			dict_entity_next(it);
		}
	}

#ifdef SPLITSCREEN
	w_coord_t px = WCOORD_CHUNK_OFFSET(floor(s->players[0].x));
	w_coord_t pz = WCOORD_CHUNK_OFFSET(floor(s->players[0].z));
#else
	w_coord_t px = WCOORD_CHUNK_OFFSET(floor(s->player.x));
	w_coord_t pz = WCOORD_CHUNK_OFFSET(floor(s->player.z));
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

	w_coord_t cx, cz;
	/* Debug: count loaded chunks */
	size_t loaded_chunks = 0;
	dict_server_chunks_it_t cit;
	dict_server_chunks_it(cit, s->world.chunks);
	while(!dict_server_chunks_end_p(cit)) {
		loaded_chunks++;
		dict_server_chunks_next(cit);
	}
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
			printf("[server_local] unloading chunk %d,%d (player chunk %d,%d) loaded_chunks=%zu\n",
				   (int)unload_x, (int)unload_z, (int)px, (int)pz, loaded_chunks);
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
		// unload just one chunk
		printf("[server_local] unloading chunk %d,%d (player chunk %d,%d) loaded_chunks=%zu\n",
			   (int)cx, (int)cz, (int)px, (int)pz, loaded_chunks);
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
			   && (d < c_nearest_dist2 || !c_nearest)
			   && server_world_disk_has_chunk(&s->world, x, z)) {
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
	int load_per_tick = finished_loading ? 1 : 8;
#else
	int load_per_tick = s->player.finished_loading ? 1 : 8;
#endif
	int loaded_this_tick = 0;
	for(int load_i = 0; load_i < load_per_tick; load_i++) {
		/* recompute nearest disk candidate each iteration */
		bool c_found = false;
		w_coord_t cand_x = 0, cand_z = 0;
		w_coord_t cand_dist2 = 0;
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
				   && (d < cand_dist2 || !c_found)
				   && server_world_disk_has_chunk(&s->world, x, z)) {
					cand_dist2 = d;
					cand_x = x;
					cand_z = z;
					c_found = true;
				}
			}
		}

		if(!c_found)
			break;

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
			/* failed to load this candidate, try next iteration */
			printf("[server_local] server_world_load_chunk failed for %d,%d\n",
				   (int)cand_x, (int)cand_z);
		}
	}

#ifdef SPLITSCREEN
	if(loaded_this_tick == 0 && !finished_loading) {
		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_TIME_SET,
			.payload.time_set = s->world_time,
		});

		for(int i = 0; i < num_players; i++) {
			struct server_player* player = &s->players[i];
			if(player->finished_loading)
				continue;

			clin_rpc_send(&(struct client_rpc) {
				CRPC_PLAYER_ID(i)
				.type = CRPC_PLAYER_POS,
				.payload.player_pos.position = {player->x, player->y, player->z},
				.payload.player_pos.rotation = {player->rx, player->ry},
			});

			printf("[server_local] finished loading for player %d near %d,%d (loaded_chunks=%zu)\n",
				   i, (int)WCOORD_CHUNK_OFFSET(floor(player->x)),
				   (int)WCOORD_CHUNK_OFFSET(floor(player->z)), loaded_chunks);

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
	}
#else
	if(loaded_this_tick == 0 && !s->player.finished_loading) {
		struct client_rpc pos;
		pos.type = CRPC_PLAYER_POS;
		if(level_archive_read_player(&s->level, pos.payload.player_pos.position,
									 pos.payload.player_pos.rotation, NULL,
									 NULL))
			clin_rpc_send(&pos);

		clin_rpc_send(&(struct client_rpc) {
			.type = CRPC_TIME_SET,
			.payload.time_set = s->world_time,
		});

		printf("[server_local] no disk chunk candidate found near player %d,%d (loaded_chunks=%zu)\n",
			   (int)px, (int)pz, loaded_chunks);

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
		if(player->y != 0) {
			server_world_get_block(&s->world, player->x-1, player->y-1, player->z, &blk);
			if(blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW) in_lava = true;
		}

		// check if player is falling
		// reset falling height if player is underwater
		if((player->old_vel_y >= -0.079f && player->vel_y < -0.079f) || in_water) {
			player->fall_y = player->y;
		}
		if(player->old_vel_y < -0.079f && player->vel_y >= -0.079f) {
			int fall_distance = player->fall_y - player->y;
			if(fall_distance >= 4) {
				server_local_set_player_health(s, i, player->health-HEALTH_PER_HEART*(fall_distance-3));
			}
			player->fall_y = player->y;
		}

		if(in_lava) {
			// damage player in lava every 8 ticks
			if((player->oxygen & 7) == 0) {
				server_local_set_player_health(s, i, player->health-HEALTH_PER_HEART*2);
			}
			player->oxygen--;
		} else if(in_water) {
			// damage drowning player every 32 ticks
			if(player->oxygen <= OXYGEN_THRESHOLD && (player->oxygen&31) == 0) {
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
	if(s->player.y != 0) {
		server_world_get_block(&s->world, s->player.x-1, s->player.y-1, s->player.z, &blk);
		if(blk.type == BLOCK_LAVA_STILL || blk.type == BLOCK_LAVA_FLOW) in_lava = true;
	}

	// check if player is falling
	// reset falling height if player is underwater
	if((s->player.old_vel_y >= -0.079f && s->player.vel_y < -0.079f) || in_water) {
		s->player.fall_y = s->player.y;
	}
	if(s->player.old_vel_y < -0.079f && s->player.vel_y >= -0.079f) {
		int fall_distance = s->player.fall_y - s->player.y;
		if(fall_distance >= 4) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART*(fall_distance-3));
		}
		s->player.fall_y = s->player.y;
	}

	if(in_lava) {
		// damage player in lava every 8 ticks
		if((s->player.oxygen & 7) == 0) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART*2);
		}
		s->player.oxygen--;
	} else if(in_water) {
		// damage drowning player every 32 ticks
		if(s->player.oxygen <= OXYGEN_THRESHOLD && (s->player.oxygen&31) == 0) {
			server_local_set_player_health(s, 0, s->player.health-HEALTH_PER_HEART);
		}
		s->player.oxygen--;
	} else s->player.oxygen = MAX_OXYGEN;
#endif
}

static void* server_local_thread(void* user) {
	while(1) {
		server_local_update(user);
		thread_msleep(50);
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
	s->last_tick = time_get();

	string_init(s->level_name);

	dict_entity_init(s->entities);
	memset(s->chest_pos, -1, MAX_CHESTS * 3 * sizeof(int));
	memset(s->sign_pos, -1, MAX_SIGNS * 3 * sizeof(int));

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
		s->players[i].fall_y = 0;
		s->players[i].oxygen = MAX_OXYGEN;
		s->players[i].health = MAX_PLAYER_HEALTH;
		s->players[i].spawn_x = 0;
		s->players[i].spawn_y = 80;
		s->players[i].spawn_z = 0;

		inventory_create(&s->players[i].inventory, &inventory_logic_player, s,
						 INVENTORY_SIZE, 0, 0, 0);
		s->players[i].active_inventory = &s->players[i].inventory;
	}
#endif

	struct thread t;
	thread_create(&t, server_local_thread, s, 8);
}
