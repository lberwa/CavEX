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

#include "../../game/game_state.h"
#include "../../graphics/render_item.h"
#include "../../item/items.h"

/*
 * 32 animation frames across rows 13-15 of items.png:
 *   Row 13: cols  4-15  (frames  0-11, 12 frames)
 *   Row 14: cols  4-15  (frames 12-23, 12 frames)
 *   Row 15: cols  4-11  (frames 24-31,  8 frames)
 *
 * Frame 0 = midday (noon). Frames advance with world time.
 * Minecraft day: 24000 ticks, noon = tick 6000.
 */
#define CLOCK_TOTAL_FRAMES 32
#define CLOCK_TICKS_PER_DAY 24000
#define CLOCK_NOON_TICK     6000

static void frame_to_tex(int frame, uint8_t* tx, uint8_t* ty) {
	if(frame < 12) {
		*tx = (uint8_t)(frame + 4);
		*ty = 13;
	} else if(frame < 24) {
		*tx = (uint8_t)(frame - 12 + 4);
		*ty = 14;
	} else {
		*tx = (uint8_t)(frame - 24 + 4);
		*ty = 15;
	}
}

static int clock_frame(void) {
	uint64_t t = (gstate.world_time + CLOCK_TICKS_PER_DAY - CLOCK_NOON_TICK)
	             % CLOCK_TICKS_PER_DAY;
	return (int)(t * CLOCK_TOTAL_FRAMES / CLOCK_TICKS_PER_DAY);
}

static void render_clock(struct item* item, struct item_data* stack,
                         mat4 view, bool fullbright,
                         enum render_item_env env) {
	uint8_t tx, ty;
	frame_to_tex(clock_frame(), &tx, &ty);

	uint8_t orig_tx = item->render_data.item.texture_x;
	uint8_t orig_ty = item->render_data.item.texture_y;
	item->render_data.item.texture_x = tx;
	item->render_data.item.texture_y = ty;

	render_item_flat(item, stack, view, fullbright, env);

	item->render_data.item.texture_x = orig_tx;
	item->render_data.item.texture_y = orig_ty;
}

struct item item_clock = {
	.name           = "Clock",
	.has_damage     = false,
	.max_stack      = 1,
	.fuel           = 0,
	.renderItem     = render_clock,
	.onItemPlace    = NULL,
	.armor.is_armor = false,
	.tool.type      = TOOL_TYPE_ANY,
	.render_data    = {
		.item = {
			.texture_x = 4,
			.texture_y = 13,
		},
	},
};
