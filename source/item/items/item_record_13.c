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
#include "../../item/items.h"

struct item item_record_13 = {
	.name           = "Record 13",
	.has_damage     = false,
	.max_stack      = 1,
	.fuel           = 0,
	.renderItem     = render_item_flat,
	.onItemPlace    = NULL,
	.armor.is_armor = false,
	.tool.type      = TOOL_TYPE_ANY,
	.render_data    = {
		.item = {
			.texture_x = 0,
			.texture_y = 15,
		},
	},
};
