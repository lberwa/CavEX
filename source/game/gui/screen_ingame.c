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

#include <stddef.h>

#include "../../block/blocks.h"
#include "../../graphics/gfx_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/render_model.h"
#include "../../network/server_interface.h"
#include "../../network/server_local.h"
#include "../../chunk_mesher.h"
#include "../../world.h"
#include "../../particle.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "../../daytime.h"
#include "screen_inventory_creative.h"

#include <malloc.h>
#ifdef PLATFORM_WII
#include <ogc/system.h>
#endif

static struct window_container* active_inventory_window(void) {
	return gstate_windows()[WINDOWC_INVENTORY];
}

static void screen_ingame_reset(struct screen* s, int width, int height) {
	input_pointer_enable(false);

	gstate_set_capture_input_all(true);
}

void screen_ingame_render3D(struct screen* s, mat4 view) {
	if(!active_inventory_window())
		return;
	
	if (gstate.world_loaded && gstate.camera_hit.entity_hit) {
		struct entity **ptr = dict_entity_get(gstate.entities,
		                                      gstate.camera_hit.entity_id);
		struct entity *e = ptr ? *ptr : NULL;
		if (e) {
			if (e->type != 0 && e->type != 1){
			gfx_blending(MODE_BLEND);
			gfx_alpha_test(false);

			gutil_entity_selection(view, e);

			gfx_blending(MODE_OFF);
			gfx_alpha_test(true);
			}
		}
	}

	if(gstate.world_loaded && gstate.camera_hit.hit) {
		struct block_data blk
			= world_get_block(gstate_player_world(gstate_active_player()), gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);

		if(gstate.digging.active)
			render_block_cracks(&blk, view, gstate.camera_hit.x,
								gstate.camera_hit.y, gstate.camera_hit.z);

		gfx_blending(MODE_BLEND);
		gfx_alpha_test(false);

		gutil_block_selection(view,
							  &(struct block_info) {
								  .block = &blk,
								  .x = gstate.camera_hit.x,
								  .y = gstate.camera_hit.y,
								  .z = gstate.camera_hit.z,
							  });

		gfx_blending(MODE_OFF);
		gfx_alpha_test(true);
	}

	

	float place_lerp = 0.0F;
	size_t slot = inventory_get_hotbar(
		windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]));

	float dig_lerp
		= time_diff_s(gstate.held_item_animation.punch.start, time_get())
		/ 0.4F;

	if(gstate.held_item_animation.punch.place)
		place_lerp = 1.0F - glm_clamp(dig_lerp * 4.0F, 0, 1);

	if(dig_lerp >= 1.0F)
		dig_lerp = 0.0F;

	float swing_lerp
		= time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
		/ 0.3F;

	if(swing_lerp < 0.5F)
		slot = gstate.held_item_animation.switch_item.old_slot;

	if(swing_lerp >= 1.0F)
		swing_lerp = 0.0F;

	float sinHalfCircle = sinf(dig_lerp * GLM_PI);
	float sqrtLerpPI = sqrtf(dig_lerp) * GLM_PI;
	float sinHalfCircleWeird = sinf(glm_pow2(dig_lerp) * GLM_PI);

	struct block_data in_block
		= world_get_block(gstate_player_world(gstate_active_player()), floorf(gstate.camera.x),
						  floorf(gstate.camera.y), floorf(gstate.camera.z));
	uint8_t light = (in_block.torch_light << 4) | in_block.sky_light;

	gfx_depth_range(0.0F, 0.1F);

	mat4 model;
	struct item_data item;

	if(inventory_get_slot(windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]),
						  slot + INVENTORY_SLOT_HOTBAR, &item)
	   && item_get(&item)) {
		glm_translate_make(model,
						   (vec3) {0.56F - sinf(sqrtLerpPI) * 0.4F,
								   -0.52F + sinf(sqrtLerpPI * 2.0F) * 0.2F
									   - 0.6F * place_lerp
									   - 0.4F * sinf(swing_lerp * GLM_PI),
								   -0.72F - sinHalfCircle * 0.2F});
		glm_rotate_y(model, glm_rad(45.0F), model);
		glm_rotate_y(model, glm_rad(-sinHalfCircleWeird * 20.0F), model);
		#ifdef GFX_3D_ELEMENTS
		glm_rotate_z(model, glm_rad(-sinf(sqrtLerpPI) * 20.0F), model);
		#endif
		glm_rotate_x(model, glm_rad(-sinf(sqrtLerpPI) * 80.0F), model);

		glm_scale_uni(model, 0.4F);
		glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});
		render_item_update_light(light);
		items[item.id]->renderItem(item_get(&item), &item, model, false,
								   R_ITEM_ENV_FIRSTPERSON);
	} else {
		glm_translate_make(model,
						   (vec3) {0.64F - sinf(sqrtLerpPI) * 0.3F,
								   -0.6F + sinf(sqrtLerpPI * 2.0F) * 0.4F
									   - 0.4F * sinf(swing_lerp * GLM_PI),
								   -0.72F - sinHalfCircle * 0.4F});
		glm_rotate_y(model, glm_rad(45.0F), model);
		glm_rotate_y(model, glm_rad(sinf(sqrtLerpPI) * 70.0F), model);
		glm_rotate_z(model, glm_rad(-sinHalfCircleWeird * 20.0F), model);

		gfx_lighting(false);
		gfx_bind_texture_virtual(&texture_mob_char);

		glm_translate(model, (vec3) {-1.0F, 3.6F, 3.5F});
		glm_rotate_z(model, glm_rad(120.0F), model);
		glm_rotate_x(model, glm_rad(200.0F), model);
		glm_rotate_y(model, glm_rad(-135.0F), model);
		glm_translate(model, (vec3) {5.6F, 0.0F, 0.0F});

		glm_translate(model,
					  (vec3) {-5.0F / 16.0F, 2.0F / 16.0F, 0.0F / 16.0F});
		glm_scale_uni(model, 1.0F / 16.0F);
		glm_translate(model, (vec3) {-3.0F, -2.0F, -2.0F});

		// TODO: depth fix in inventory
		render_model_box(model, (vec3) {2, 12, 2}, (vec3) {2, 0, 2},
						 (vec3) {180.0F, 0, 0}, (ivec2) {44, 20},
						 (ivec3) {4, 4, 12}, 0.0F, false,
						 gfx_lookup_light(light));
	}

	gfx_depth_range(0.0F, 1.0F);
}

	static void screen_ingame_update_player(struct screen* s, float dt) {
		if(input_pressed(IB_HOME, gstate_active_player())) {
			screen_set_player(gstate_active_player(), &screen_game_menu);
			return;
		}

		if(!active_inventory_window())
			return;

		bool creative_mode = gstate.local_player
			&& gstate.local_player->data.local_player.creative;

		// left click interaction
		if (gstate.camera_hit.entity_hit
		    && input_pressed(IB_ACTION1, gstate_active_player())
		    && !gstate.digging.active)
		{
		    struct entity **ptr = dict_entity_get(
		        gstate.entities,
		        gstate.camera_hit.entity_id
		    );
		    if (ptr) {
		        struct entity *e = *ptr;
				// Ignore floating item pickups: allow block interaction "through" them.
				if(e && e->type == ENTITY_ITEM) {
					// fall through to block digging / interaction code below
				} else
		        if (e && e->onLeftClick) {
		            e->onLeftClick(e);
		            // Optionele punch‐animatie (zoals eerder)
		            struct item_data held;
		            if (inventory_get_hotbar_item(
		                   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), &held))
		            {
		                gstate.held_item_animation.punch.start = time_get();
		                gstate.held_item_animation.punch.place = false;
		            }
		            return;
		        }
		    }
		}

	// right click interaction met entity via dict_entity_get
	if (gstate.camera_hit.entity_hit
	    && input_pressed(IB_ACTION2, gstate_active_player())
	    && !gstate.digging.active)
	{
	    struct entity **ptr = dict_entity_get(
	        gstate.entities,
	        gstate.camera_hit.entity_id
	    );
		    if (ptr) {
		        struct entity *e = *ptr;
				// Ignore floating item pickups: allow block interaction "through" them.
				if(e && e->type == ENTITY_ITEM) {
					// fall through
				} else if(e) {
				if (e->onRightClick) {
					struct item_data held;
					struct item_data *held_ptr = NULL;
					bool has_held_item = false;

				if (inventory_get_hotbar_item(
				        windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), &held)) {
					held_ptr = &held;
					has_held_item = true;
				}
	            e->onRightClick(e, held_ptr);
	            if (has_held_item) {
	                gstate.held_item_animation.punch.start = time_get();
	                gstate.held_item_animation.punch.place = false;
	            }
	            }
	            return;
	        }
	    }
	}


	// block place
	if(gstate.camera_hit.hit && input_pressed(IB_ACTION2, gstate_active_player())
	   && !gstate.digging.active) {
		svin_rpc_try_send(&(struct server_rpc) {
			RPC_PLAYER_ID(gstate_active_player())
			.type = SRPC_BLOCK_PLACE,
			.payload.block_place.x = gstate.camera_hit.x,
			.payload.block_place.y = gstate.camera_hit.y,
			.payload.block_place.z = gstate.camera_hit.z,
			.payload.block_place.side = gstate.camera_hit.side,
		});

		if(inventory_get_hotbar_item(
			   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL)) {
			gstate.held_item_animation.punch.start = time_get();
			gstate.held_item_animation.punch.place = true;
		}
	}

	// item use with no target (right- OR left-click in the air), e.g. drinking
	// milk to get an empty bucket back. Only fires when neither a block nor an
	// entity is under the crosshair, so it never interferes with placing, mining
	// or interaction (those all require a target).
	if(!gstate.camera_hit.hit && !gstate.camera_hit.entity_hit
	   && (input_pressed(IB_ACTION2, gstate_active_player())
		   || input_pressed(IB_ACTION1, gstate_active_player()))
	   && !gstate.digging.active) {
		struct item_data held;
		if(inventory_get_hotbar_item(
			   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), &held)
		   && (held.id == ITEM_MILK_BUCKET
		       || held.id == ITEM_FISHING_ROD)) {
			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(gstate_active_player())
				.type = SRPC_ITEM_USE,
			});
		}
	}

		// block dig
		if(gstate.digging.active) {
		struct block_data blk
			= world_get_block(gstate_player_world(gstate_active_player()), gstate.digging.x, gstate.digging.y,
							  gstate.digging.z);
		struct item_data it;
		inventory_get_hotbar_item(
			windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), &it);
		int delay = blocks[blk.type] ?
			tool_dig_delay_ms(blocks[blk.type], item_get(&it)) :
			0;
		if(creative_mode && delay >= 0)
			delay = 0;

		if(!gstate.camera_hit.hit || gstate.digging.x != gstate.camera_hit.x
		   || gstate.digging.y != gstate.camera_hit.y
		   || gstate.digging.z != gstate.camera_hit.z) {
			gstate.digging.start = time_get();
			gstate.digging.x = gstate.camera_hit.x;
			gstate.digging.y = gstate.camera_hit.y;
			gstate.digging.z = gstate.camera_hit.z;

			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(gstate_active_player())
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = false,
			});
		}

		if(delay >= 0
		   && time_diff_ms(gstate.digging.start, time_get()) >= delay) {
			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(gstate_active_player())
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = true,
			});

			gstate.digging.cooldown = time_get();
			gstate.digging.active = false;
		}

		// Require the button to be held while mining.
		// Using `input_released()` here can miss quick tap/release sequences for
		// non-master players (shared PC keyboard/mouse), leaving digging active
		// and letting blocks break after a single tap.
		if(!input_held(IB_ACTION1, gstate_active_player()))
			gstate.digging.active = false;
		} else {
			if(gstate.camera_hit.hit && input_held(IB_ACTION1, gstate_active_player())
			   && time_diff_ms(gstate.digging.cooldown, time_get()) >= 250) {
				gstate.digging.active = true;
				gstate.digging.start = time_get();
				gstate.digging.x = gstate.camera_hit.x;
				gstate.digging.y = gstate.camera_hit.y;
				gstate.digging.z = gstate.camera_hit.z;

			svin_rpc_try_send(&(struct server_rpc) {
				RPC_PLAYER_ID(gstate_active_player())
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = false,
			});
		}
	}

	if(input_held(IB_ACTION1, gstate_active_player())
	   && time_diff_s(gstate.held_item_animation.punch.start, time_get())
		   >= 0.2F) {
		gstate.held_item_animation.punch.start = time_get();
		gstate.held_item_animation.punch.place = false;

		if(gstate.camera_hit.hit) {
			struct block_data blk
				= world_get_block(gstate_player_world(gstate_active_player()), gstate.camera_hit.x,
								  gstate.camera_hit.y, gstate.camera_hit.z);

			struct block_data neighbours[6];

			for(int k = 0; k < SIDE_MAX; k++) {
				int ox, oy, oz;
				blocks_side_offset((enum side)k, &ox, &oy, &oz);

				neighbours[k] = world_get_block(
					gstate_player_world(gstate_active_player()), gstate.camera_hit.x + ox,
					gstate.camera_hit.y + oy, gstate.camera_hit.z + oz);
			}

			particle_spawn_dim = gstate.player_dims[gstate_active_player()];
			particle_generate_side(
				&(struct block_info) {.block = &blk,
									  .neighbours = neighbours,
									  .x = gstate.camera_hit.x,
									  .y = gstate.camera_hit.y,
									  .z = gstate.camera_hit.z},
				gstate.camera_hit.side);
		}
	}

	size_t slot = inventory_get_hotbar(
		windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]));
	bool old_item_exists = inventory_get_hotbar_item(
		windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL);

	if(input_pressed(IB_SCROLL_LEFT, gstate_active_player())) {
		size_t next_slot = (slot == 0) ? INVENTORY_SIZE_HOTBAR - 1 : slot - 1;
		inventory_set_hotbar(
			windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), next_slot);
		bool new_item_exists = inventory_get_hotbar_item(
			windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL);

		if(time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
			   >= 0.15F
		   && (old_item_exists || new_item_exists)) {
			gstate.held_item_animation.switch_item.start = time_get();
			gstate.held_item_animation.switch_item.old_slot = slot;
		}

		if(gstate.digging.active)
			gstate.digging.start = time_get();

		svin_rpc_try_send(&(struct server_rpc) {
			RPC_PLAYER_ID(gstate_active_player())
			.type = SRPC_HOTBAR_SLOT,
			.payload.hotbar_slot.slot = next_slot,
		});
	}

	if(input_pressed(IB_SCROLL_RIGHT, gstate_active_player())) {
#ifdef FAST_MOVING
		gstate.fast_moving = gstate.fast_moving ? false : true;
#endif
		size_t next_slot = (slot == INVENTORY_SIZE_HOTBAR - 1) ? 0 : slot + 1;
		inventory_set_hotbar(
			windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), next_slot);
		bool new_item_exists = inventory_get_hotbar_item(
			windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), NULL);

		if(time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
			   >= 0.15F
		   && (old_item_exists || new_item_exists)) {
			gstate.held_item_animation.switch_item.start = time_get();
			gstate.held_item_animation.switch_item.old_slot = slot;
		}

		if(gstate.digging.active)
			gstate.digging.start = time_get();

		svin_rpc_try_send(&(struct server_rpc) {
			RPC_PLAYER_ID(gstate_active_player())
			.type = SRPC_HOTBAR_SLOT,
			.payload.hotbar_slot.slot = next_slot,
		});
	}

	if(input_pressed(IB_INVENTORY, gstate_active_player()))
		screen_set_player(gstate_active_player(),
			creative_mode ? &screen_inventory_creative : &screen_inventory);

	if(input_pressed(IB_TOGGLE_CREATIVE, gstate_active_player()))
		svin_rpc_try_send(&(struct server_rpc) {
			RPC_PLAYER_ID(gstate_active_player())
			.type = SRPC_SET_GAMEMODE,
			.payload.set_gamemode.toggle = true,
		});
}

	static void screen_ingame_update(struct screen* s, float dt) {
	#ifdef SPLITSCREEN
		if(splitscreen_enabled()) {
			int player_count = splitscreen_player_count();
			for(int p = 0; p < player_count; p++) {
				splitscreen_load_player(p);
				struct screen* ps = screen_get_player(p);
				if(ps == &screen_ingame)
					screen_ingame_update_player(s, dt);
				else if(ps && ps->update)
					ps->update(ps, dt);
				splitscreen_store_player(p);
			}
			splitscreen_load_player(0);
			return;
		}
	#endif
		{
			struct screen* ps = screen_get_player(0);
			if(ps == &screen_ingame)
				screen_ingame_update_player(s, dt);
			else if(ps && ps->update)
				ps->update(ps, dt);
		}
	}

static void screen_ingame_render2D(struct screen* s, int width, int height) {
	char str[128];
	if(!active_inventory_window())
		return;
	
   if (gstate.settings.debug) 
   {
	snprintf(str, sizeof(str),
	         GAME_NAME " Alpha %i.%i.%i_f%i (impl. B1.7.3)",
	         VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_FORK);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 0, str, GFX_GUI_SCALE * 8, true);


	snprintf(str, sizeof(str), "%0.1f fps, frame %.1fms",
	         gstate.stats.fps, gstate.stats.dt * 1000.0F);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 1, str, GFX_GUI_SCALE * 8, true);

	snprintf(str, sizeof(str), "%zu chunks", gstate.stats.chunks_rendered);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 2, str, GFX_GUI_SCALE * 8, true);

	snprintf(str, sizeof(str), "(%0.1f, %0.1f, %0.1f) (%0.1f, %0.1f)",
	         gstate.camera.x, gstate.camera.y, gstate.camera.z,
	         glm_deg(gstate.camera.rx), glm_deg(gstate.camera.ry));
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 3, str, GFX_GUI_SCALE * 8, true);

	snprintf(str, sizeof(str), "player: %d", gstate_active_player() + 1);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 4, str, GFX_GUI_SCALE * 8, true);

	float time = gstate.world_time
	             + time_diff_s(gstate.world_time_start, time_get()) * 1000.0f
	                   / 50.0f;
	float day_ticks = fmodf(time, 24000.0f);
	float angle = daytime_celestial_angle(day_ticks / 24000.0f);
	snprintf(str, sizeof(str), "time: %.0f (%.0f)  angle: %.3f", time,
	         day_ticks, angle);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 5, str, GFX_GUI_SCALE * 8, true);

	if (gstate.camera_hit.entity_hit) {
		struct entity **ptr = dict_entity_get(
		    gstate.entities,
		    gstate.camera_hit.entity_id
		);
		if (ptr) {
		    struct entity *e = *ptr;
		    const char *ename = e && e->name ? e->name : "<unnamed>";
		    snprintf(str, sizeof(str), "(%i, %i, %i), %s (%u)",
		             gstate.camera_hit.x, gstate.camera_hit.y,
		             gstate.camera_hit.z, ename, e ? e->id : 0);
		} else {
		    snprintf(str, sizeof(str), "(%i, %i, %i)",
		             gstate.camera_hit.x, gstate.camera_hit.y,
		             gstate.camera_hit.z);
		}
		gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 6, str, GFX_GUI_SCALE * 8, true);
	} else	if(gstate.camera_hit.hit) {
		struct block_data bd
			= world_get_block(gstate_player_world(gstate_active_player()), gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);
		struct block* b = blocks[bd.type];
		snprintf(str, sizeof(str), "side: %s, (%i, %i, %i), %s, (%i:%i)",
		         block_side_name(gstate.camera_hit.side), gstate.camera_hit.x,
		         gstate.camera_hit.y, gstate.camera_hit.z,
		         (b && b->name) ? b->name : "<unknown>", bd.type,
		         bd.metadata);
		gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 6, str, GFX_GUI_SCALE * 8, true);
	}

	snprintf(str, sizeof(str), "server: %.1f TPS, tick %.1f ms (target 20 / 50)",
	         gstate.stats.server_tps, gstate.stats.server_tick_ms);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 7, str, GFX_GUI_SCALE * 8, true);

	snprintf(str, sizeof(str), "mesher: %.0f builds/s, %.1f ms/build, queue %d/%d",
	         chunk_mesher_stat_builds_per_sec,
	         chunk_mesher_stat_ms_per_build,
	         chunk_mesher_stat_queue_depth,
	         CHUNK_MESHER_QLENGTH);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 8, str, GFX_GUI_SCALE * 8, true);

	snprintf(str, sizeof(str),
	         "prof: clin %.1f  tick %.1f  render %.1f  gui %.1f  finish %.1f ms",
	         gstate.stats.prof_clin_ms,
	         gstate.stats.prof_tick_ms,
	         gstate.stats.prof_render_ms,
	         gstate.stats.prof_gui_ms,
	         gstate.stats.prof_finish_ms);
	gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 9, str, GFX_GUI_SCALE * 8, true);

	/* Portal-Teleport-Zähler: zeigt Sekunden im Portal (1..4), bei 4s Teleport */
	int _pticks = gstate.portal_ticks_display[gstate_active_player()];
	if(_pticks > 0) {
		int _psec = _pticks / 20 + 1;      /* 1,2,3,4 */
		if(_psec > 4) _psec = 4;
		snprintf(str, sizeof(str), "Portal-Teleport: %d / 4", _psec);
		gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 10, str, GFX_GUI_SCALE * 8, true);
	}

	if(gstate.local_player && gstate.local_player->data.local_player.creative)
		gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 11, "CREATIVE", GFX_GUI_SCALE * 8, true);
	if(gstate.local_player && gstate.local_player->data.local_player.flying)
		gutil_text(4, 4 + (GFX_GUI_SCALE * 8 + 1) * 12, "FLYING (Sneak=down, double Jump=off)", GFX_GUI_SCALE * 8, true);
   }

	int icon_offset = GFX_GUI_SCALE * 16;
	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Inventory");
	icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Jump");

	if (gstate.camera_hit.entity_hit) {
	    struct entity **ptr = dict_entity_get(
	        gstate.entities,
	        gstate.camera_hit.entity_id
	    );
	    if (ptr) {
	        struct entity *e = *ptr;
	        if (e && e->leftClickText) {
	            icon_offset += gutil_control_icon(icon_offset,
	                                              IB_ACTION1,
	                                              e->leftClickText);
	        }
	        if (e && e->rightClickText) {
	            icon_offset += gutil_control_icon(icon_offset,
	                                              IB_ACTION2,
	                                              e->rightClickText);
	        }
	    }
	}

	else if (gstate.camera_hit.hit) {
		struct item_data item;
		struct block_data bd
			= world_get_block(gstate_player_world(gstate_active_player()), gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);
		if(blocks[bd.type] && blocks[bd.type]->onRightClick) {
			icon_offset += gutil_control_icon(icon_offset, IB_ACTION2, "Use");
		} else if(inventory_get_hotbar_item(
					  windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]),
					  &item)
				  && item_get(&item)) {
			icon_offset
				+= gutil_control_icon(icon_offset, IB_ACTION2,
									  item_is_block(&item) ? "Place" : "Use");
		}

		icon_offset += gutil_control_icon(icon_offset, IB_ACTION1, "Mine");
	} else {
		icon_offset += gutil_control_icon(icon_offset, IB_ACTION1, "Punch");
	}

	icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Pause");

	/* Vertikaler Abstand der Hotbar-Unterkante zum unteren Viewport-Rand.
	 * Im Splitscreen auf 0 -> die Unterkante des Inventars sitzt bei beiden
	 * Spielern buendig am unteren Strip-Rand. Die ganze HUD-Gruppe (Herzen,
	 * Sauerstoff) rutscht dabei einheitlich mit. */
	int hud_gap = (GFX_GUI_SCALE * 16) * 8 / 5;
#ifdef SPLITSCREEN
	if(splitscreen_enabled())
		hud_gap = 0;
#endif

	// draw hotbar
	gfx_bind_texture(&texture_gui2);
	gutil_texquad((width - 182 * GFX_GUI_SCALE) / 2, height - hud_gap - 22 * GFX_GUI_SCALE, 0, 0,
				  182, 22, 182 * GFX_GUI_SCALE, 22 * GFX_GUI_SCALE);

	//  +
	{
		int cx = (width - 16 * GFX_GUI_SCALE) / 2;
		int cy = (height - 16 * GFX_GUI_SCALE) / 2;
		int cs = 16 * GFX_GUI_SCALE;

		bool deferred = false;
#ifdef PLATFORM_PC
		deferred = true;
#ifdef SPLITSCREEN
		deferred = !splitscreen_enabled();
#endif
#endif
		if(deferred) {
			/* drawn on top of the composited image so the colour inversion
			 * sees the 3D scene behind it (separate GUI FBO otherwise) */
			gfx_crosshair(&texture_gui2, cx, cy, 0, 229, 16, 16, cs, cs);
		} else {
			gfx_blending(MODE_INVERT);
			gfx_bind_texture(&texture_gui2);
			gutil_texquad(cx, cy, 0, 229, 16, 16, cs, cs);
			gfx_blending(MODE_OFF);
		}
	}

	for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		struct item_data item;
		if(inventory_get_slot(
			   windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]),
			   k + INVENTORY_SLOT_HOTBAR, &item))
			gutil_draw_item(&item, (width - 182 * GFX_GUI_SCALE) / 2 + 3 * GFX_GUI_SCALE + 20 * GFX_GUI_SCALE * k,
							height - hud_gap - 19 * GFX_GUI_SCALE, 0);
	}

	gfx_blending(MODE_BLEND);
	gfx_bind_texture(&texture_gui2);

	// draw hotbar selection
	gutil_texquad((width - 182 * GFX_GUI_SCALE) / 2 - 2
					  + 20 * GFX_GUI_SCALE 
						  * inventory_get_hotbar(windowc_get_latest(
							  gstate_windows()[WINDOWC_INVENTORY])),
				  height - hud_gap - 23 * GFX_GUI_SCALE, 208, 0, 24, 24, 24 * GFX_GUI_SCALE, 24 * GFX_GUI_SCALE);

	{
		bool hud_creative = gstate.local_player
			&& gstate.local_player->data.local_player.creative;

		// HUD is rendered once per viewport. In splitscreen mode `main.c` already
		// loaded the active player via `splitscreen_load_player(p)` before
		// calling render2D, so we must NOT swap players here (it corrupts player
		// state, e.g. digging timers for player 2).
		int heart_start_x = (width - 182 * GFX_GUI_SCALE) / 2;
		int heart_spacing = 8 * GFX_GUI_SCALE;
		int heart_x_base = heart_start_x;

		if(!hud_creative) {
		for(int k = 0; k < MAX_PLAYER_HEALTH / HEALTH_PER_HEART; k++) {
			gutil_texquad(
				heart_x_base + k * heart_spacing,
				height - hud_gap
					- (22 + 10) * GFX_GUI_SCALE,
				16, 229, 9, 9, 9 * GFX_GUI_SCALE, 9 * GFX_GUI_SCALE);
		}
		if(gstate.local_player) {
			for(int k = 0; k < (gstate.local_player->health / HEALTH_PER_HEART);
				k++) {
				gutil_texquad(
					heart_x_base + k * heart_spacing,
					height - hud_gap
						- (22 + 10) * GFX_GUI_SCALE,
					52, 229, 9, 9, 9 * GFX_GUI_SCALE, 9 * GFX_GUI_SCALE);
			}
		}
		}

		if(!hud_creative && gstate.in_water && gstate.oxygen >= OXYGEN_THRESHOLD) {
			int oxy_x_base = heart_x_base;
			for(int k = 0; k < ((gstate.oxygen - OXYGEN_THRESHOLD) / 32); k++) {
				gutil_texquad(
					oxy_x_base + k * heart_spacing,
					height - hud_gap - (GFX_GUI_SCALE * 4) * 8 / 5
						- (22 + 10) * GFX_GUI_SCALE,
					17, 249, 9, 9, 9 * GFX_GUI_SCALE, 9 * GFX_GUI_SCALE);
			}
		}

#ifdef DIGGING_DEBUG
		{
			static ptime_t last_dbg_by_player[4];
			int ap = gstate_active_player();
			ptime_t now_dbg = time_get();
			if(ap >= 0 && ap < 4
			   && time_diff_ms(last_dbg_by_player[ap], now_dbg) >= 250) {
#ifdef PLATFORM_PC
				printf("[dig p=%d] hit=%d ent=%d active=%d start_age=%dms cd_age=%dms pos=(%d %d %d) cur=(%d %d %d)\n",
					   ap,
					   (int)gstate.camera_hit.hit,
					   (int)gstate.camera_hit.entity_hit,
					   (int)gstate.digging.active,
					   (int)time_diff_ms(gstate.digging.start, now_dbg),
					   (int)time_diff_ms(gstate.digging.cooldown, now_dbg),
					   (int)gstate.digging.x, (int)gstate.digging.y, (int)gstate.digging.z,
					   (int)gstate.camera_hit.x, (int)gstate.camera_hit.y, (int)gstate.camera_hit.z);
#endif
				last_dbg_by_player[ap] = now_dbg;
			}
		}
#endif
	}

	/* debug overlay: live chunk generation progress (top-right) */
	if(gstate.settings.debug) {
		int sc = GFX_GUI_SCALE;
		char gstr[64];
		if(gstate.gen_debug.active)
			snprintf(gstr, sizeof(gstr), "gen %d%% (%d, %d)  built %lu",
					 gstate.gen_debug.percent, gstate.gen_debug.chunk_x,
					 gstate.gen_debug.chunk_z, gstate.gen_debug.built);
		else
			snprintf(gstr, sizeof(gstr), "gen idle  built %lu",
					 gstate.gen_debug.built);

		int tw = gutil_font_width(gstr, 8 * sc);
		int gx = width - tw - 4 * sc;
		int gy = 4 * sc;
		gutil_text(gx, gy, gstr, 8 * sc, true);

		/* mesh pipeline diagnostics: dirty chunks waiting + mesher throughput.
		 * dirty climbs but sent flat -> rebuilds not reaching the mesher;
		 * sent climbs but built/recv flat -> worker thread stuck;
		 * all climb but still stale -> rendering/displaylist issue. */
		char mstr[80];
		snprintf(mstr, sizeof(mstr), "dirty %lu  snt %lu fail %lu blt %lu rcv %lu",
				 (unsigned long)world_count_dirty_chunks(&gstate.world),
				 chunk_mesher_dbg_sent, chunk_mesher_dbg_failed,
				 chunk_mesher_dbg_built, chunk_mesher_dbg_recv);
		int mtw = gutil_font_width(mstr, 8 * sc);
		gutil_text(width - mtw - 4 * sc, gy + (8 * sc + 1) * 2, mstr, 8 * sc,
				   true);

		/* progress bar under the text */
		int bw = tw;
		int bh = 2 * sc;
		int bx = gx;
		int by = gy + 9 * sc;
		int fill = gstate.gen_debug.active
			? bw * gstate.gen_debug.percent / 100
			: 0;
		gfx_texture(false);
		gutil_texquad_col(bx, by, 0, 0, 0, 0, bw, bh, 64, 64, 64, 255);
		if(fill > 0)
			gutil_texquad_col(bx, by, 0, 0, 0, 0, fill, bh, 128, 255, 128,
							  255);
		gfx_texture(true);
	}

#ifdef PLATFORM_WII
	{
		struct mallinfo mi = mallinfo();
		/* Der newlib-Heap waechst von MEM1 nahtlos in MEM2. "Frei" ist daher der
		 * gesamte WIEDERVERWENDBARE Speicher: Free-List (fordblks) + noch nicht
		 * beanspruchte MEM1- UND MEM2-Arena. Die reine MEM2-Arena (Hi-Lo) allein
		 * ist irrefuehrend -- sie sinkt monoton (free gibt nur an die Free-List
		 * zurueck), obwohl insgesamt reichlich frei ist. */
		u32 mem2_hi    = (u32)SYS_GetArena2Hi();
		u32 mem2_lo    = (u32)SYS_GetArena2Lo();
		u32 mem2_arena = mem2_hi > mem2_lo ? mem2_hi - mem2_lo : 0;
		u32 heap_free  = (u32)mi.fordblks + SYS_GetArena1Size();
		u32 usable_free = heap_free + mem2_arena;
		u32 usable_mb  = usable_free / (1024u * 1024u);
		u32 arena_mb   = mem2_arena / (1024u * 1024u);
		/* Balken 1 = gesamter freier Speicher (Referenz ~48MB "voll gruen"). */
		int pct1 = usable_mb >= 48 ? 100 : (int)(usable_mb * 100 / 48);
		/* Balken 2 = nur zur Info die MEM2-Arena. */
		int pct2 = arena_mb >= 48 ? 100 : (int)(arena_mb * 100 / 48);

		int sc = GFX_GUI_SCALE;
		int bar_w = 80 * sc;
		int bar_h = 4 * sc;
		int bar_x = width - bar_w - 4 * sc;
		int bar_y = gstate.settings.debug
			? 4 * sc + (8 * sc + 1) * 2 + 8 * sc + 6 * sc
			: 4 * sc;
		int row = bar_h + 8 * sc + 2 * sc; // Abstand zwischen den zwei Zeilen

		// --- Gesamter freier Speicher ---
		int fill1 = bar_w * pct1 / 100;
		// wenig frei = rot, viel frei = gruen
		uint8_t r1 = pct1 < 25 ? 0xFF : (pct1 < 50 ? 0xFF : 0x00);
		uint8_t g1 = pct1 < 25 ? 0x30 : (pct1 < 50 ? 0xAA : 0xCC);
		char s1[24]; snprintf(s1, sizeof(s1), "FREI %uMB", usable_mb);
		int tw1 = gutil_font_width(s1, 8 * sc);
		gutil_text(width - tw1 - 4 * sc, bar_y + bar_h + sc, s1, 8 * sc, true);
		gfx_texture(false);
		gutil_texquad_col(bar_x-1, bar_y-1, 0,0,0,0, bar_w+2, bar_h+2, 0,0,0,180);
		if(fill1 > 0)
			gutil_texquad_col(bar_x, bar_y, 0,0,0,0, fill1, bar_h, r1,g1,0,220);
		gfx_texture(true);

		// --- MEM2 ---
		int by2 = bar_y + row;
		int fill2 = bar_w * pct2 / 100;
		char s2[24];
		snprintf(s2, sizeof(s2), "arena %uMB", arena_mb);
		int tw2 = gutil_font_width(s2, 8 * sc);
		gutil_text(width - tw2 - 4 * sc, by2 + bar_h + sc, s2, 8 * sc, true);
		gfx_texture(false);
		gutil_texquad_col(bar_x-1, by2-1, 0,0,0,0, bar_w+2, bar_h+2, 0,0,0,180);
		if(fill2 > 0)
			gutil_texquad_col(bar_x, by2, 0,0,0,0, fill2, bar_h, 0,0xCC,0,220);
		gfx_texture(true);
	}
#endif
}

struct screen screen_ingame = {
	.reset = screen_ingame_reset,
	.update = screen_ingame_update,
	.render2D = screen_ingame_render2D,
	.render3D = screen_ingame_render3D,
	.render_world = true,
};
