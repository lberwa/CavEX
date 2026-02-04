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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PLATFORM_WII
	#include <fat.h>
	#include <gccore.h> 
	#ifdef NDEBUG
		#include <my_text_renderer.h>
	#endif
	#include <network.h>
#endif

#include "item/recipe.h"
#include "particle.h"
#include "chunk_mesher.h"
#include "daytime.h"
#include "game/game_state.h"
#include "game/gui/screen.h"
#include "graphics/gfx_util.h"
#include "graphics/gui_util.h"
#include "graphics/gfx_settings.h"
#include "graphics/render_entity.h"
//#include "graphics/render_monster.h"
#include "item/recipe.h"
#include "network/client_interface.h"
#include "network/server_interface.h"
#include "network/server_local.h"
#include "particle.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "world.h"
#include "sound.h"
#include "network/server_comunication.h"

#include "cNBT/nbt.h"
#include "cglm/cglm.h"
#include "lodepng/lodepng.h"

#ifdef PLATFORM_WII
static void *xfb2 = NULL;

static void *net_thread(void *arg) {
    int ret = net_init();
    if (ret >= 0)
        *((int*)arg) = 1;   // net_ready setzen
    return NULL;
}

GXRModeObj* rmode3;
void* framebuffer3;
#endif
#ifdef PLATFORM_WII
#ifdef NDEBUG
	// ram checken
	#include <ogc/system.h>
	#include <stdio.h>
	#include <malloc.h>
	#include <ogc/lwp_heap.h>


	extern void* __Arena1Lo;
	extern void* __Arena1Hi;
	extern void* __Arena2Lo;
	extern void* __Arena2Hi;
bool debugsendfirst = false;
#endif
#endif

#ifdef PLATFORM_PC
#include <signal.h>
#endif

int main(void) {

	#ifdef PLATFORM_PC
	signal(SIGPIPE, SIG_IGN);
	#endif

	//video_init_custom();
	#ifdef PLATFORM_WII
	VIDEO_Init();

	rmode3 = VIDEO_GetPreferredMode(NULL);
    framebuffer3 = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode3));
	#endif

	float daytime, tick_delta;
	bool render_world;

	gstate.quit = false;
	gstate.camera = (struct camera) {
		.x = 0, .y = 0, .z = 0, .rx = 0, .ry = 0, .controller = {0, 0, 0}};
	gstate.config.fov = 70.0F;
	gstate.config.render_distance = 192.0F;
	gstate.config.fog_distance = 5 * 16.0F;
	gstate.world_loaded = false;
	gstate.held_item_animation.punch.start = time_get();
	gstate.held_item_animation.switch_item.start = time_get();
	gstate.digging.cooldown = time_get();
	gstate.digging.active = false;
	gstate.paused = false;

	for(int i=1; i<4; i++) {
        gstate.player_sequence[i] = -1;
    }
    gstate.player_sequence[0] = 0; // default: chan 0

	rand_gen_seed(&gstate.rand_src);

#ifdef PLATFORM_WII
	fatInitDefault();
	#ifndef NDEBUG
		SYS_STDIO_Report(true);
		SYS_Report("[INIT] STDIO redirection is now active\n");
	#endif
#endif

	config_create(&gstate.config_user, "config.json");

	input_init();
	blocks_init();
	items_init();
	render_entity_init();
	//render_monster_init();

	recipe_init();
	gfx_setup();
	
	menu_screen_set(&screen_select_world2);

	world_create(&gstate.world);

	for(size_t k = 0; k < 256; k++)
		gstate.windows[k] = NULL;

	clin_init();
	svin_init();
	chunk_mesher_init();
	particle_init();

	dict_entity_init(gstate.entities);
	gstate.local_player = NULL;
	gstate.in_water = false;
	gstate.oxygen = MAX_OXYGEN;

	struct server_local server;
	server_local_create(&server);

	ptime_t last_frame = time_get();
	ptime_t last_tick = last_frame;
	
	#ifdef PLATFORM_WII
	xfb2 = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode3));

	console_init(xfb2,20,20,rmode3->fbWidth,rmode3->xfbHeight,rmode3->fbWidth*VI_DISPLAY_PIX_SZ);
	
    static int net_ready = 0;
    lwp_t thread;
    LWP_CreateThread(
        &thread,
        net_thread,       // normale C-Funktion
        &net_ready,       // Argument
        NULL,             // Stackbase (NULL für automatisch)
        0,                // Stackgröße (0 = default)
        50                // Priorität
    );
	#endif /*PLATFORM_WII*/

	sound_init();

	while(!gstate.quit) { // |-------------------| main loop |-------------------|
		#ifdef PLATFORM_WII
		#ifdef NDEBUG
		if (gstate.network)
		{
			if (!debugsendfirst) {
				debug_init(192,168,15,152);
				debugsendfirst = true;
			}
		}
		#endif /*NDEBUG*/

		if (net_ready)
			gstate.network = true;
		else
			gstate.network = false;
		#endif /*PLATFORM_WII*/

		#ifdef PLATFORM_PC
		//if conected //		TODO
		gstate.network = true;
		#endif /*PLATFORM_PC*/

		ptime_t this_frame = time_get();
		gstate.stats.dt = time_diff_s(last_frame, this_frame);
		gstate.stats.fps = 1.0F / gstate.stats.dt;
		last_frame = this_frame;

		if(!gstate.paused) daytime
			= (float)((gstate.world_time
					 + time_diff_ms(gstate.world_time_start, this_frame)
						 / DAY_TICK_MS)
						% DAY_LENGTH_TICKS)
			/ (float)DAY_LENGTH_TICKS;

		clin_update();

		tick_delta = time_diff_s(last_tick, time_get()) / 0.05F;

		while(tick_delta >= 1.0F) {
			last_tick = time_add_ms(last_tick, 50);
			tick_delta -= 1.0F;
			if(!gstate.paused) {
				particle_update();
				entities_client_tick(gstate.entities);
			}
		}

		if(gstate.local_player)
			camera_attach(&gstate.camera, gstate.local_player, tick_delta,
							gstate.stats.dt);
		// update particle‐system with current camera pos for spawn‐culling
		particle_set_camera((vec3){
		    gstate.camera.x,
		    gstate.camera.y,
		    gstate.camera.z
		});

		render_world
			= gstate.current_screen->render_world && gstate.world_loaded;
		bool in_water_new = false;

		if(render_world) {
			struct block_data blk = world_get_block(
				&gstate.world, floorf(gstate.camera.x),
				floorf(gstate.camera.y + 0.1F), floorf(gstate.camera.z));
			in_water_new		
				= blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL;
			#ifndef GFX_FANCY_LIQUIDS
			if(gstate.in_water != in_water_new) {
			#endif
				gstate.in_water = in_water_new;	
			#ifndef GFX_FANCY_LIQUIDS
				world_redraw_chunks(&gstate.world);
			}
			#endif
		}

		camera_update(&gstate.camera, gstate.in_water);

		if(render_world) {
			world_pre_render(&gstate.world, &gstate.camera, gstate.camera.view);

			{
				// 1) Bereken eerst de ray‐origin en direction uit de camera
				vec3 origin, dir;
				camera_get_ray(&gstate.camera, origin, dir);

				// 2) Probeer eerst een entiteit te raken binnen 4.5 eenheid
				float tHit;
				struct entity *hitE = raycast_entity(&gstate.entities,
													 origin, dir,
													 4.5f,
													 &tHit);
				if (hitE == gstate.local_player) {
				    hitE = NULL;
				}

				if (hitE) {
					gstate.camera_hit.entity_hit = true;
					gstate.camera_hit.entity_id  = hitE->id;
					gstate.camera_hit.hit = false;
				}
				else {
					gstate.camera_hit.entity_hit = false;
					gstate.camera_hit.entity_id  = 0;

					camera_ray_pick(&gstate.world,
									gstate.camera.x, gstate.camera.y, gstate.camera.z,
									gstate.camera.x + sinf(gstate.camera.rx) * sinf(gstate.camera.ry) * 4.5F,
									gstate.camera.y +            cosf(gstate.camera.ry) * 4.5F,
									gstate.camera.z + cosf(gstate.camera.rx) * sinf(gstate.camera.ry) * 4.5F,
									&gstate.camera_hit);
				}
			}
		} else {
		    world_pre_render_clear(&gstate.world);
		    gstate.camera_hit.hit        = false;
		    gstate.camera_hit.entity_hit = false;
		    gstate.camera_hit.entity_id  = 0;
		}

		world_update_lighting(&gstate.world);
		world_build_chunks(&gstate.world, CHUNK_MESHER_QLENGTH);

		if(gstate.current_screen->update)
			gstate.current_screen->update(gstate.current_screen,
											gstate.stats.dt);

		gfx_flip_buffers(&gstate.stats.dt_gpu, &gstate.stats.dt_vsync);

		if(!gstate.paused) {
			// must not modify displaylists while still rendering!
			chunk_mesher_receive();
			world_render_completed(&gstate.world, render_world);

			vec3 top_plane_color, bottom_plane_color, atmosphere_color;
			daytime_sky_colors(daytime, top_plane_color, bottom_plane_color,
								 atmosphere_color);

			if(render_world) {
				gfx_clear_buffers(atmosphere_color[0], atmosphere_color[1],
									atmosphere_color[2]);
			} else {
				gfx_clear_buffers(128, 128, 128);
			}

			gfx_fog_color(atmosphere_color[0], atmosphere_color[1],
							atmosphere_color[2]);

			gfx_mode_world();
			gfx_matrix_projection(gstate.camera.projection, true);

			if(render_world) {
				gfx_update_light(daytime_brightness(daytime),
								 world_dimension_light(&gstate.world));

				if(gstate.world.dimension == WORLD_DIM_OVERWORLD)
					gutil_sky_box(gstate.camera.view,
									daytime_celestial_angle(daytime), top_plane_color,
									bottom_plane_color);

				gstate.stats.chunks_rendered
					= world_render(&gstate.world, &gstate.camera, false);
			} else {
				gstate.stats.chunks_rendered = 0;
			}

			if(gstate.current_screen->render3D) {
				gfx_fog(false);
				gstate.current_screen->render3D(gstate.current_screen,
												gstate.camera.view);
			}

			if(render_world) {
				gfx_fog(false);
				particle_render(
					gstate.camera.view,
					(vec3) {gstate.camera.x, gstate.camera.y, gstate.camera.z},
					tick_delta);
				entities_client_render(gstate.entities, &gstate.camera, tick_delta);
				gfx_fog(true);

				#ifdef GFX_FANCY_LIQUIDS
				world_render(&gstate.world, &gstate.camera, true);
				#endif

				#ifdef GFX_CLOUDS
				if(gstate.world.dimension == WORLD_DIM_OVERWORLD)
					gutil_clouds(gstate.camera.view,
					daytime_brightness(daytime));
				#endif
			}

			gfx_mode_gui();

			if(gstate.in_water) {
				gfx_bind_texture(&texture_water);
				gutil_texquad_col(0, 0, -gstate.camera.rx / GLM_PI * 256,
									gstate.camera.ry / GLM_PI * 256, 512,
									512 * (float)gfx_height() / (float)gfx_width(),
									gfx_width(), gfx_height(), 0xFF, 0xFF, 0xFF,
									0x80);
			}

		}

		if(gstate.current_screen->render2D)
			gstate.current_screen->render2D(gstate.current_screen, gfx_width(),
											gfx_height());

		if(input_pressed(IB_SCREENSHOT, 0)) {
			size_t width, height;
			gfx_copy_framebuffer(NULL, &width, &height);

			void* image = malloc(width * height * 4);

			if(image) {
				gfx_copy_framebuffer(image, &width, &height);

				char name[64];
				snprintf(name, sizeof(name), "%ld.png", (long)time(NULL));

				lodepng_encode32_file(name, image, width, height);
				free(image);
			}
		}

		sound_update();
		input_poll();
		gfx_finish(true);

//--------------------------------------------------------------------
	#ifdef DEBUGSEND
		/*
		char text1[64];
		char text2[64];

		snprintf(text1, sizeof(text1),
        		 "MEM1 free: %u bytes",
        		 (unsigned int)((u8*)__Arena1Hi - (u8*)__Arena1Lo));

		snprintf(text2, sizeof(text2),
        		 "MEM2 free: %u bytes",
        		 (unsigned int)((u8*)__Arena2Hi - (u8*)__Arena2Lo));

*/
/*
		u32 mem1_free = (u32)((u8*)__Arena1Hi - (u8*)__Arena1Lo);
		u32 mem2_free = (u32)((u8*)__Arena2Hi - (u8*)__Arena2Lo);


		char buf[128];
		snprintf(buf, sizeof(buf),
    	     "MEM1 total: %u KB\nMEM2 total: %u KB\n"
	         "MEM1 free: %u KB\nMEM2 free: %u KB\n\n\n",
        	 SYS_GetArena1Size()/1024,
    	     SYS_GetArena2Size()/1024,
	         mem1_free/1024,
        		 mem2_free/1024);

		debug_send(buf);
		
		debug_send("---new---\n");
		struct mallinfo mi;
    
		char buffer[100]; // Puffer groß genug für eine Textzeile


    // Abfrage der Speicherinformationen
    mi = mallinfo();

    // Formatieren und Drucken der ersten Zeile
    snprintf(buffer, sizeof(buffer), "--- Wii RAM Status ---\n");
    debug_send(buffer); // Ruft terminal_print für die erste Zeile auf

    // Formatieren und Drucken der zweiten Zeile (Gesamt-Heap-Größe)
    snprintf(buffer, sizeof(buffer), "Gesamt-Heap (Arena): %d KB\n", mi.arena / 1024);
    debug_send(buffer); // Ruft terminal_print für die zweite Zeile auf

    // Formatieren und Drucken der dritten Zeile (Belegter Speicher)
    snprintf(buffer, sizeof(buffer), "Belegt (uordblks): %d KB\n", mi.uordblks / 1024);
    debug_send(buffer); // Ruft terminal_print für die dritte Zeile auf
    
    // Formatieren und Drucken der vierten Zeile (Freier Speicher)
    snprintf(buffer, sizeof(buffer), "Frei (fordblks): %d KB\n", mi.fordblks / 1024);
    debug_send(buffer); // Ruft terminal_print für die vierte Zeile auf

    // Eine Leerzeile hinzufügen
    snprintf(buffer, sizeof(buffer), "\n");
    debug_send(buffer);

    // Beispiel einer Zuweisung, um den Effekt zu zeigen
    void* big_block = malloc(5 * 1024 * 1024); // 5 MB reservieren
    
    if (big_block) {
        mi = mallinfo(); // Speicher erneut abfragen nach malloc()
        snprintf(buffer, sizeof(buffer), "Nach 5MB malloc():\n");
        debug_send(buffer);

        snprintf(buffer, sizeof(buffer), "Aktuell Frei: %d KB\n", mi.fordblks / 1024);
        debug_send(buffer);
        
        free(big_block);
    } else {
        snprintf(buffer, sizeof(buffer), "Fehler: Konnte 5MB nicht zuweisen.\n");
        debug_send(buffer);
    }
	*/

	// Arena Grenzen
    u32 a1lo = (u32)SYS_GetArena1Lo();
    u32 a1hi = (u32)SYS_GetArena1Hi();
    u32 a2lo = (u32)SYS_GetArena2Lo();
    u32 a2hi = (u32)SYS_GetArena2Hi();
	char buf[256];

    u32 a1_total = (a1hi > a1lo) ? (a1hi - a1lo) : 0;
    u32 a2_total = (a2hi > a2lo) ? (a2hi - a2lo) : 0;

    struct mallinfo mi = mallinfo();

    // mallinfo.uordblks = total allocated bytes by malloc
    // mallinfo.fordblks = total free bytes in malloc's internal free list
    // Achtung: mallinfo bezieht sich auf den malloc-Heap, typischerweise Arena1 (oder wo dein malloc initialisiert wurde).
    int malloc_used = mi.uordblks;
    int malloc_free_internal = mi.fordblks;
/*
    printf("Arena1 (MEM1): lo=0x%08X hi=0x%08X total=%u bytes\n", a1lo, a1hi, a1_total);
    printf("Arena2 (MEM2): lo=0x%08X hi=0x%08X total=%u bytes\n", a2lo, a2hi, a2_total);

    printf("\nmalloc (default heap): used (uordblks) = %d bytes\n", malloc_used);
    printf("malloc internal free (fordblks) = %d bytes\n", malloc_free_internal);

    // Abschätzung: wieviel von Arena1 noch 'verfügbar' (sehr grobe Schätzung):
    // available_in_arena1 = a1_total - malloc_used
    // (Hinweis: das ist nur eine Näherung; malloc hat Overhead/Fragmentierung und andere Komponenten können Arena belegen)
    if (a1_total >= (u32)malloc_used)
        printf("geschätzter freier Platz in Arena1 = %u bytes\n", a1_total - (u32)malloc_used);
    else
        debug_send("geschätzter freier Platz in Arena1 = 0 (oder malloc > arena)\n");
*/

// Arena 1 Adressen und Total
    snprintf(buf, sizeof(buf), "Arena1 (MEM1): lo=0x%08X hi=0x%08X total=%u bytes", 
             a1lo, a1hi, a1_total);
    debug_send(buf);
    
    // Zeilenumbruch (simuliert)
    snprintf(buf, sizeof(buf), " "); 
    debug_send(buf);

    // Arena 2 Adressen und Total
    snprintf(buf, sizeof(buf), "Arena2 (MEM2): lo=0x%08X hi=0x%08X total=%u bytes", 
             a2lo, a2hi, a2_total);
    debug_send(buf);

    // Zeilenumbruch
    snprintf(buf, sizeof(buf), " ");
    debug_send(buf);

    // malloc (default heap) used
    snprintf(buf, sizeof(buf), "malloc (default heap): used (uordblks) = %d bytes", 
             malloc_used);
    debug_send(buf);

    // malloc internal free
    snprintf(buf, sizeof(buf), "malloc internal free (fordblks) = %d bytes", 
             malloc_free_internal);
    debug_send(buf);

    // Zeilenumbruch
    snprintf(buf, sizeof(buf), " ");
    debug_send(buf);

    // Abschätzung
    if (a1_total >= (u32)malloc_used) {
        snprintf(buf, sizeof(buf), "geschätzter freier Platz in Arena1 = %u bytes", 
                 a1_total - (u32)malloc_used);
        debug_send(buf);
    } else {
        snprintf(buf, sizeof(buf), "geschätzter freier Platz in Arena1 = 0 (oder malloc > arena)");
        debug_send(buf);
    }

    // Für Arena2: wenn du MEM2 aktiv verwendest, tracke dortige Allocs separat.
    // Viele Programme legen große Buffers mit memalign/MEM2_alloc in MEM2 an; diese musst du zählen.
    debug_send("\n(Hinweis: MEM2-Nutzung wird nicht automatisch durch mallinfo gemeldet — falls du MEM2 für große Buffers verwendest, musst du deren Größe selbst tracken.)\n");
#endif
		//debug_send(text2);
//----------------------------------------------------------------------------
	}

	return 0;
}
