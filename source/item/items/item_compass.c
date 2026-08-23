/*
	Copyright (c) 2026 ByteBit/xtreme8000

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

#include <math.h>

#include "../../game/game_state.h"
#include "../../graphics/render_item.h"
#include "../../item/items.h"

/*
 * 40 animation frames spread across rows 9-13 of items.png:
 *   Row  9: cols 0-6   (frames  0-6,  7 frames)
 *   Row 10: cols 0-6   (frames  7-13, 7 frames)
 *   Row 11: cols 0-6   (frames 14-20, 7 frames)
 *   Row 12: cols 0-6   (frames 21-27, 7 frames)
 *   Row 13: cols 0-3   (frames 28-31,  4 frames)
 *
 * Frame 0 = spawn directly BEHIND the player.
 * Frame count increases as the player turns LEFT.
 */
#define COMPASS_TOTAL_FRAMES 32

static void frame_to_tex(int frame, uint8_t* tx, uint8_t* ty) {
	if(frame < 28) {
		*tx = (uint8_t)(frame % 7);
		*ty = (uint8_t)(9 + frame / 7);
	} else {
		*tx = (uint8_t)(frame - 28);
		*ty = 13;
	}
}

static int compass_frame(void) {
	struct entity* lp = gstate.local_player;
	if(!lp)
		return 0;

	float dx = (float)gstate.spawn_x - lp->pos[0];
	float dz = (float)gstate.spawn_z - lp->pos[2];

	/* bearing toward spawn in the same convention as camera.rx:
	   0 = +Z, π/2 = +X  →  atan2(dx, dz) */
	float bearing  = atan2f(dx, dz);
	float relative = bearing - gstate.camera.rx;

	/* normalize to [0, 2π) */
	float two_pi = 2.0f * GLM_PIf;
	relative = fmodf(relative, two_pi);
	if(relative < 0.0f)
		relative += two_pi;

	/* shift so that relative=π (spawn behind) → frame 0 */
	float shifted = relative - GLM_PIf;
	if(shifted < 0.0f)
		shifted += two_pi;

	int frame = (int)(shifted / two_pi * (float)COMPASS_TOTAL_FRAMES)
	            % COMPASS_TOTAL_FRAMES;
	return (COMPASS_TOTAL_FRAMES - frame) % COMPASS_TOTAL_FRAMES;
}

static void render_compass(struct item* item, struct item_data* stack,
                           mat4 view, bool fullbright,
                           enum render_item_env env) {
	uint8_t tx, ty;
	frame_to_tex(compass_frame(), &tx, &ty);

	uint8_t orig_tx = item->render_data.item.texture_x;
	uint8_t orig_ty = item->render_data.item.texture_y;
	item->render_data.item.texture_x = tx;
	item->render_data.item.texture_y = ty;

	render_item_flat(item, stack, view, fullbright, env);

	item->render_data.item.texture_x = orig_tx;
	item->render_data.item.texture_y = orig_ty;
}

struct item item_compass = {
	.name           = "Compass",
	.has_damage     = false,
	.max_stack      = 1,
	.fuel           = 0,
	.renderItem     = render_compass,
	.onItemPlace    = NULL,
	.armor.is_armor = false,
	.tool.type      = TOOL_TYPE_ANY,
	.render_data    = {
		.item = {
			.texture_x = 0,
			.texture_y = 9,
		},
	},
};
