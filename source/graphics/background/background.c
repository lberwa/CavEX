
#include "../../graphics/gui_util.h"
#include "../../graphics/gfx_settings.h"
#include "../../network/level_archive.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../stack.h"
#include "../../util.h"
#include "../../game/game_state.h"
#include "../../game/gui/screen.h"

#include "../../block/blocks.h"
#include "../../graphics/render_model.h"
#include "../../network/server_local.h"
#include "../../particle.h"
#include "../../daytime.h"
#include "../../graphics/gfx_util.h"
#include "../../chunk.h"
#include "../../game/camera.h"

#include <assert.h>
#include <dirent.h>
#include <m-lib/m-string.h>
#include <string.h>
#include <time.h>

static struct stack* worlds = NULL;

static size_t gui_selection;
static int scroll_offset;

static int top_visible;
static int bottom_visible;
static int height_visible;
static int entry_height = 36 * GFX_GUI_SCALE;
static int side_padding = 2 * GFX_GUI_SCALE;
/*
enum screen_exit_reason {
    SCREEN_NONE,
    SCREEN_WORLD_SELECTED,
    SCREEN_HOME_PRESSED
} exit_reason;
*/
/*
struct world_option {
	string_t name;
	string_t directory;
	string_t path;
	int64_t last_access;
	int64_t byte_size;
};*/

bool bg_init() {
	/*
    // initalization paths to background
	int width  = screenMode->fbWidth;    // EFB-Breite
	int height = screenMode->efbHeight;  // EFB-Höhe
	
	GX_SetScissorBoxOffset(0, 0);
	
	// Viewport auf gesamten Bildschirm
	GX_SetViewport(0, 0, width, height, 0, 1);
	GX_SetScissor(0, 0, width, height);
	
	GX_SetDispCopySrc(0, 0, width, height);
	GX_SetDispCopyDst(width, screenMode->xfbHeight);
	GX_SetDispCopyYScale(GX_GetYScaleFactor(height, screenMode->xfbHeight));


	gstate.game_run = false;
	
	input_pointer_enable(true);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;

	if(worlds) {
		while(!stack_empty(worlds)) {
			struct world_option opt;
			stack_pop(worlds, &opt);
			string_clear(opt.name);
			string_clear(opt.directory);
			string_clear(opt.path);
		}

		stack_destroy(worlds);
		free(worlds);
		worlds = NULL;
	}

	worlds = malloc(sizeof(struct stack));
	stack_create(worlds, 8, sizeof(struct world_option));

	const char* saves_path
		= config_read_string(&gstate.config_user, "paths.bg", "bg");

	DIR* d = opendir(saves_path);

	if(d) {
		struct dirent* dir;
		while((dir = readdir(d))) {
			if(dir->d_type & DT_DIR && *dir->d_name != '.') {
				struct world_option opt;
				string_init_printf(opt.path, "%s/%s", saves_path, dir->d_name);

				struct level_archive la;
				if(level_archive_create(&la, opt.path)) {
					char name[64];

					if(!level_archive_read(&la, LEVEL_NAME, name, sizeof(name)))
						strcpy(name, "Missing name");

					string_init_set_str(opt.name, name);
					string_init_set_str(opt.directory, dir->d_name);

					if(!level_archive_read(&la, LEVEL_DISK_SIZE, &opt.byte_size,
										   0))
						opt.byte_size = 0;

					if(!level_archive_read(&la, LEVEL_LAST_PLAYED,
										   &opt.last_access, 0))
						opt.last_access = 0;

					opt.last_access /= 1000;

					level_archive_destroy(&la);
					stack_push(worlds, &opt);
				} else {
					string_clear(opt.path);
				}
			}
		}

		closedir(d);
	}

	gui_selection = 0;
	scroll_offset = side_padding;
	top_visible = height * 0.133F;
	bottom_visible = height - 32 * GFX_GUI_SCALE;
	height_visible = bottom_visible - height * 0.133F;
    

    //load background
   	struct world_option opt;
	stack_at(worlds, &opt, gui_selection);

    struct server_rpc rpc;
	rpc.type = SRPC_LOAD_WORLD;
	string_init_set(rpc.payload.load_world.name, opt.path);
	svin_rpc_send(&rpc);
    */

    screen_set(&screen_init_bg);
    return true;
}


bool _3d_bg() {
    //render 3d background
    	if (gstate.world_loaded && gstate.camera_hit.entity_hit) {
    struct entity *e = *dict_entity_get(gstate.entities,
                                        gstate.camera_hit.entity_id);
		if (e) {
			if (e->type != 0 && e->type != 1){
			gfx_blending(MODE_BLEND);
			gfx_alpha_test(false);

			gutil_entity_selection(gstate.camera.view, e);

			gfx_blending(MODE_OFF);
			gfx_alpha_test(true);
			}
		}
	}

	if(gstate.world_loaded && gstate.camera_hit.hit) {
		struct block_data blk
			= world_get_block(&gstate.world, gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);

		if(gstate.digging.active)
			render_block_cracks(&blk, gstate.camera.view, gstate.camera_hit.x,
								gstate.camera_hit.y, gstate.camera_hit.z);

		gfx_blending(MODE_BLEND);
		gfx_alpha_test(false);

		gutil_block_selection(gstate.camera.view,
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
		windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]));

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
		= world_get_block(&gstate.world, floorf(gstate.camera.x),
						  floorf(gstate.camera.y), floorf(gstate.camera.z));
	uint8_t light = (in_block.torch_light << 4) | in_block.sky_light;

	gfx_depth_range(0.0F, 0.1F);

	mat4 model;
	struct item_data item;

	if(inventory_get_slot(windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]),
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
		gfx_bind_texture(&texture_mob_char);

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

    return true;
}