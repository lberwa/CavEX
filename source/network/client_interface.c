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

#ifdef PLATFORM_PC
#include "../particle.h"
#endif

#include "client_interface.h"
#include "../game/game_state.h"
#include "../particle.h"
#include "../platform/thread.h"
#include "server_interface.h"
#include "server_local.h"

#define RPC_INBOX_SIZE 16
static struct client_rpc rpc_msg[RPC_INBOX_SIZE];
static struct thread_channel clin_inbox;
static struct thread_channel clin_empty_msg;

static ptime_t last_pos_update;
#ifdef SPLITSCREEN
static bool splitscreen_p2_spawned = false;
#endif


void clin_chunk(w_coord_t x, w_coord_t y, w_coord_t z, w_coord_t sx,
				w_coord_t sy, w_coord_t sz, uint8_t* ids, uint8_t* metadata,
				uint8_t* lighting_sky, uint8_t* lighting_torch) {
	assert(sx > 0 && sz > 0 && y >= 0 && y + sy <= WORLD_HEIGHT);
	assert(ids && metadata && lighting_sky && lighting_torch);

	uint8_t* ids_t = ids;
	uint8_t* metadata_t = metadata;
	uint8_t* lighting_s_t = lighting_sky;
	uint8_t* lighting_t_t = lighting_torch;
	bool flip = true;

	for(w_coord_t ox = x; ox < x + sx; ox++) {
		for(w_coord_t oz = z; oz < z + sz; oz++) {
			for(w_coord_t oy = y; oy < y + sy; oy++) {
				uint8_t md = flip ? (*metadata_t) & 0xF : (*metadata_t) >> 4;
				uint8_t sky
					= flip ? (*lighting_s_t) & 0xF : (*lighting_s_t) >> 4;
				uint8_t torch
					= flip ? (*lighting_t_t) & 0xF : (*lighting_t_t) >> 4;

				world_set_block(&gstate.world, ox, oy, oz,
								(struct block_data) {
									.type = *ids_t,
									.metadata = md,
									.sky_light = sky,
									.torch_light = torch,
								},
								false);
				ids_t++;

				flip = !flip;
				if(flip) {
					lighting_s_t++;
					lighting_t_t++;
					metadata_t++;
				}
			}
		}
	}

	free(ids);
	free(metadata);
	free(lighting_sky);
	free(lighting_torch);
}

void clin_process(struct client_rpc* call) {
	assert(call);

	switch(call->type) {
		case CRPC_CHUNK:
			clin_chunk(call->payload.chunk.x, call->payload.chunk.y,
					   call->payload.chunk.z, call->payload.chunk.sx,
					   call->payload.chunk.sy, call->payload.chunk.sz,
					   call->payload.chunk.ids, call->payload.chunk.metadata,
					   call->payload.chunk.lighting_sky,
					   call->payload.chunk.lighting_torch);
			break;
		case CRPC_UNLOAD_CHUNK:
			world_unload_section(&gstate.world, call->payload.unload_chunk.x,
								 call->payload.unload_chunk.z);
			break;
		case CRPC_PLAYER_POS:
			gstate.camera.rx = glm_rad(-call->payload.player_pos.rotation[0]);
			gstate.camera.ry = glm_rad(glm_clamp(
				call->payload.player_pos.rotation[1] + 90.0F, 0.0F, 180.0F));
			if(gstate.local_player)
				gstate.local_player->teleport(
					gstate.local_player,
					(vec3) {call->payload.player_pos.position[0],
							call->payload.player_pos.position[1],
							call->payload.player_pos.position[2]});
#ifdef SPLITSCREEN
			if(splitscreen_enabled() && gstate.local_players[1]
			   && !splitscreen_p2_spawned) {
				vec3 p2pos = {
					call->payload.player_pos.position[0] + 2.0F,
					call->payload.player_pos.position[1],
					call->payload.player_pos.position[2] + 2.0F
				};
				gstate.local_players[1]->teleport(gstate.local_players[1], p2pos);
				splitscreen_p2_spawned = true;
			}
#endif
			gstate.world_loaded = true;
			break;
		case CRPC_WORLD_RESET:
			world_unload_all(&gstate.world);

			for(size_t k = 0; k < 256; k++) {
				if(gstate_windows()[k]) {
					windowc_destroy(gstate_windows()[k]);
					free(gstate_windows()[k]);
					gstate_windows()[k] = NULL;
				}
			}

			dict_entity_it_t it;
			dict_entity_it(it, gstate.entities);

			while(!dict_entity_end_p(it)) {
				free(dict_entity_ref(it)->value);
				dict_entity_next(it);
			}

			dict_entity_reset(gstate.entities);

			gstate_windows()[WINDOWC_INVENTORY]
				= malloc(sizeof(struct window_container));
			assert(gstate_windows()[WINDOWC_INVENTORY]);
			windowc_create(gstate_windows()[WINDOWC_INVENTORY],
						   WINDOW_TYPE_INVENTORY, INVENTORY_SIZE);

			gstate.world_loaded = false;
			gstate.world.dimension = call->payload.world_reset.dimension;

			struct entity** local_player_ptr = dict_entity_safe_get(
				gstate.entities, call->payload.world_reset.local_entity);
			*local_player_ptr = malloc(sizeof(struct entity));
			assert(*local_player_ptr);
			gstate.local_player = *local_player_ptr;
			entity_local_player(call->payload.world_reset.local_entity,
								gstate.local_player, &gstate.world);

			gstate.local_player->health = MAX_PLAYER_HEALTH;
#ifdef SPLITSCREEN
			gstate.local_player->data.local_player.player_index = 0;
			gstate.local_players[0] = gstate.local_player;
			gstate.oxygen_arr[0] = MAX_OXYGEN;
			splitscreen_p2_spawned = false;

			if(splitscreen_enabled()) {
				uint32_t p2_id = 0xF0000001u;
				if(dict_entity_get(gstate.entities, p2_id))
					p2_id = entity_gen_id(gstate.entities);
				struct entity** p2_ptr = dict_entity_safe_get(gstate.entities,
															 p2_id);
				*p2_ptr = malloc(sizeof(struct entity));
				assert(*p2_ptr);
				gstate.local_players[1] = *p2_ptr;
				entity_local_player(p2_id, gstate.local_players[1],
									&gstate.world);
				gstate.local_players[1]->data.local_player.player_index = 1;
				gstate.local_players[1]->health = MAX_PLAYER_HEALTH;
			} else {
				gstate.local_players[1] = NULL;
			}
#endif

			if(gstate.current_screen == &screen_ingame)
				screen_set(&screen_load_world);
			break;
		case CRPC_INVENTORY_SLOT: {
			uint8_t window = call->payload.inventory_slot.window;
			if(gstate_windows()[window])
				windowc_slot_change(gstate_windows()[window],
									call->payload.inventory_slot.slot,
									call->payload.inventory_slot.item);
			break;
		}
		case CRPC_WINDOW_TRANSACTION: {
			uint8_t window = call->payload.window_transaction.window;
			if(gstate_windows()[window])
				windowc_action_apply_result(
					gstate_windows()[window],
					call->payload.window_transaction.action_id,
					call->payload.window_transaction.accepted);
			break;
		}
		case CRPC_OPEN_WINDOW: {
			uint8_t window = call->payload.window_open.window;

			if(gstate_windows()[window]) {
				windowc_destroy(gstate_windows()[window]);
				free(gstate_windows()[window]);
			}

			gstate_windows()[window] = malloc(sizeof(struct window_container));

			if(gstate_windows()[window]) {
				windowc_create(gstate_windows()[window],
							   call->payload.window_open.type,
							   call->payload.window_open.slot_count);

				switch(call->payload.window_open.type) {
					case WINDOW_TYPE_WORKBENCH:
						screen_crafting_set_windowc(window);
						screen_set(&screen_crafting);
						break;
					case WINDOW_TYPE_FURNACE:
						screen_furnace_set_windowc(window);
						screen_set(&screen_furnace);
						break;
					case WINDOW_TYPE_CHEST:
						screen_chest_set_windowc(window);
						screen_set(&screen_chest);
						break;
					case WINDOW_TYPE_IRON_CHEST:
						screen_iron_chest_set_windowc(window);
						screen_set(&screen_iron_chest);
						break;
					case WINDOW_TYPE_SIGN:
						screen_sign_set_windowc(window);
						screen_set(&screen_sign);
					default: break;
				}
			}

			break;
		}
		case CRPC_TIME_SET:
			gstate.world_time = call->payload.time_set;
			gstate.world_time_start = time_get();
			break;
		case CRPC_SET_BLOCK:
			if(call->payload.set_block.block.type == BLOCK_AIR) {
				struct block_data blk = world_get_block(
					&gstate.world, call->payload.set_block.x,
					call->payload.set_block.y, call->payload.set_block.z);
				struct block_data neighbours[6];

				for(int k = 0; k < SIDE_MAX; k++) {
					int ox, oy, oz;
					blocks_side_offset((enum side)k, &ox, &oy, &oz);

					neighbours[k] = world_get_block(
						&gstate.world, call->payload.set_block.x + ox,
						call->payload.set_block.y + oy,
						call->payload.set_block.z + oz);
				}

				particle_generate_block(&(struct block_info) {
					.block = &blk,
					.neighbours = neighbours,
					.x = call->payload.set_block.x,
					.y = call->payload.set_block.y,
					.z = call->payload.set_block.z,
				});
			}

			world_set_block(&gstate.world, call->payload.set_block.x,
							call->payload.set_block.y,
							call->payload.set_block.z,
							call->payload.set_block.block, true);

			break;
		case CRPC_SPAWN_ITEM: {
			struct entity** e_ptr = dict_entity_safe_get(
				gstate.entities, call->payload.spawn_item.entity_id);
			*e_ptr = malloc(sizeof(struct entity));
			struct entity* e = *e_ptr;
			assert(e);
			entity_item(call->payload.spawn_item.entity_id, e, false,
						&gstate.world, call->payload.spawn_item.item);
			e->teleport(e, call->payload.spawn_item.pos);
			glm_vec3_copy(call->payload.spawn_item.vel, e->vel);
		} break;
		case CRPC_SPAWN_MONSTER: {
			struct entity** e_ptr = dict_entity_safe_get(
				gstate.entities, call->payload.spawn_monster.entity_id);
			*e_ptr = malloc(sizeof(struct entity));
			struct entity* e = *e_ptr;
			assert(e);
			entity_monster(call->payload.spawn_monster.entity_id, e, false,
						&gstate.world, call->payload.spawn_monster.monster_id);
			e->teleport(e, call->payload.spawn_monster.pos);
		} break;

		case CRPC_SPAWN_MINECART: {
		    struct entity** e_ptr = dict_entity_safe_get(
		        gstate.entities,
		        call->payload.spawn_minecart.entity_id
		    );
		    *e_ptr = malloc(sizeof(struct entity));
		    struct entity* e = *e_ptr;
		    assert(e);

		    entity_minecart(call->payload.spawn_minecart.entity_id,
		                    e,
		                    false,                
		                    &gstate.world);
		    e->teleport(e, call->payload.spawn_minecart.pos);
		} break;

		case CRPC_PICKUP_ITEM: {
			if(gstate.local_player
			   && call->payload.pickup_item.collector_id
				   == gstate.local_player->id) {
				struct entity* e = *(dict_entity_get(
					gstate.entities, call->payload.pickup_item.entity_id));
				if(e)
					glm_vec3_copy((vec3) {gstate.camera.x,
										  gstate.camera.y - 0.2F,
										  gstate.camera.z},
								  e->network_pos);
			}
		} break;
		case CRPC_ENTITY_DESTROY:
			free(*dict_entity_get(gstate.entities,
							  call->payload.entity_destroy.entity_id));
			dict_entity_erase(gstate.entities,
							  call->payload.entity_destroy.entity_id);
			break;
		case CRPC_ENTITY_MOVE: {
			struct entity* e = *(dict_entity_get(
				gstate.entities, call->payload.entity_move.entity_id));
			if(e)
				glm_vec3_copy(call->payload.entity_move.pos, e->network_pos);
		} break;
		case CRPC_PLAYER_SET_HEALTH:
			if (gstate.local_player) gstate.local_player->health = call->payload.player_set_health.health;
		break;
	}
}

void clin_init() {
	tchannel_init(&clin_inbox, RPC_INBOX_SIZE);
	tchannel_init(&clin_empty_msg, RPC_INBOX_SIZE);

	for(int k = 0; k < RPC_INBOX_SIZE; k++)
		tchannel_send(&clin_empty_msg, rpc_msg + k, true);

	last_pos_update = time_get();
#ifdef SPLITSCREEN
	splitscreen_p2_spawned = false;
#endif
}

void clin_update() {
	void* call;
	while(tchannel_receive(&clin_inbox, &call, false)) {
		clin_process(call);
		tchannel_send(&clin_empty_msg, call, true);
	}

	if(gstate.world_loaded && time_diff_ms(last_pos_update, time_get()) >= 50) {
#ifdef SPLITSCREEN
		if(splitscreen_enabled()) {
			for(int i = 0; i < splitscreen_player_count(); i++) {
				struct camera* cam = &gstate.cameras[i];
				struct entity* player = gstate.local_players[i];
				if(player) {
					svin_rpc_send(&(struct server_rpc) {
						RPC_PLAYER_ID(i)
						.type = SRPC_PLAYER_POS,
						.payload.player_pos.x = cam->x,
						.payload.player_pos.y = cam->y,
						.payload.player_pos.z = cam->z,
						.payload.player_pos.rx = -glm_deg(cam->rx),
						.payload.player_pos.ry = glm_deg(cam->ry) - 90.0F,
						.payload.player_pos.vel_y = player->vel[1]
					});
				}
			}
		} else
#endif
		{
			struct camera* cam = &gstate.camera;
			struct entity* player = gstate.local_player;
			if(player) {
				svin_rpc_send(&(struct server_rpc) {
					RPC_PLAYER_ID(0)
					.type = SRPC_PLAYER_POS,
					.payload.player_pos.x = cam->x,
					.payload.player_pos.y = cam->y,
					.payload.player_pos.z = cam->z,
					.payload.player_pos.rx = -glm_deg(cam->rx),
					.payload.player_pos.ry = glm_deg(cam->ry) - 90.0F,
					.payload.player_pos.vel_y = player->vel[1]
				});
			}
		}
		last_pos_update = time_get();
	}
}

void clin_rpc_send(struct client_rpc* call) {
	struct client_rpc* empty;
	tchannel_receive(&clin_empty_msg, (void**)&empty, true);
	*empty = *call;
	tchannel_send(&clin_inbox, empty, true);
}
