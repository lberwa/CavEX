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

#include "../../graphics/render_item.h"

// Boat motor (issue #33). A held tool, not a placeable block and with no use
// action: it does nothing on its own. Its effect is read in the boat control
// logic (entity_local_player.c) -- while the motor is the selected hotbar item
// and the player is riding a boat, the boat self-propels forward (capped).
// Runtime state only; nothing is persisted. No new art: reuses the furnace
// front tile.
struct item item_motor = {
	.name = "Boat Motor",
	.has_damage = false,
	.max_stack = 1,
	.fuel = 0,
	.renderItem = render_item_flat,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			// Reuse the furnace-front terrain tile (atlas x=12, y=2) for the
			// hotbar icon -- a fitting "machine" look without adding art.
			.texture_x = 7,
			.texture_y = 0,
		},
	},
};
