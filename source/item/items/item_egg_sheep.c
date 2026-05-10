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

/*
    Sheep spawn egg item
*/

#include "../../network/server_local.h"
#include "../../network/client_interface.h"
#include "../../network/server_interface.h"
#include "../../entity/entity_monster.h"


static bool onItemPlace(struct server_local* s, struct item_data* it,
                        struct block_info* where, struct block_info* on,
                        enum side on_side) {
    /* spawn sheep at the clicked location */
    server_local_spawn_monster((vec3) {where->x, where->y, where->z}, ANIMAIL_SHEEP, s);
    return false; /* spawn eggs have infinite uses */
}

struct item item_egg_sheep = {
    .name = "Sheep Egg",
    .has_damage = false,
    .max_stack = 64,
    .fuel = 0,
    .renderItem = render_item_flat,
    .onItemPlace = onItemPlace,
    .armor.is_armor = false,
    .tool.type = TOOL_TYPE_ANY,
    .render_data = {
        .item = {
            .texture_x = 10,
            .texture_y = 9,
        },
    },
};
