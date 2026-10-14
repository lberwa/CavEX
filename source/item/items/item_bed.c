/*
	Copyright (c) 2023 ByteBit/xtreme8000

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

#include "../../block/blocks_data.h"
#include "../../cglm/types.h"
#include "../../entity/entity.h"
#include "../../graphics/render_item.h"
#include "../../network/server_local.h"
#include "../../network/server_world.h"
#include "../items.h"
#include "../tool.h"

static bool onItemPlace(struct server_local* s, struct item_data* it,
                        struct block_info* where, struct block_info* on,
                        enum side on_side) {
    struct server_player* player = &s->players[s->active_player_id];
    float rx = glm_rad(player->rx);
    float look_x = -sinf(rx);
    float look_z = cosf(rx);
    int dx = 0, dz = 0;
    uint8_t metadata = 0;

    if(fabsf(look_x) > fabsf(look_z)) {
        if(look_x > 0.0F) {
            dx = 1;
            metadata = 1;
        } else {
            dx = -1;
            metadata = 3;
        }
    } else {
        if(look_z > 0.0F) {
            dz = 1;
            metadata = 0;
        } else {
            dz = -1;
            metadata = 2;
        }
    }

    struct block_data blk;
    if(!server_world_get_block(AWORLD(s), where->x + dx, where->y,
                               where->z + dz, &blk)
       || blk.type != BLOCK_AIR)
        return false;

    struct block_data foot = {
        .type = BLOCK_BED,
        .metadata = metadata,
        .sky_light = 0,
        .torch_light = 0,
    };
    struct block_data head = {
        .type = BLOCK_BED,
        .metadata = metadata | 8, // Bit 3 = head flag
        .sky_light = 0,
        .torch_light = 0,
    };

    struct block_info foot_info = *where;
    foot_info.block = &foot;
    if(entity_local_player_block_collide(
           (vec3) {player->x, player->y, player->z}, &foot_info)) {
        return false;
    }

    struct block_info head_info = *where;
    head_info.x += dx;
    head_info.z += dz;
    head_info.block = &head;
    if(entity_local_player_block_collide(
           (vec3) {player->x, player->y, player->z}, &head_info)) {
        return false;
    }

    server_world_set_block(s, where->x, where->y, where->z, foot);
    server_world_set_block(s, where->x + dx, where->y, where->z + dz, head);

    return true;
}

struct item item_bed = {
    .name = "Bed",
    .has_damage = false,
    .max_stack = 1,
    .fuel = 0,
    .renderItem = render_item_flat,
    .onItemPlace = onItemPlace,
    .armor.is_armor = false,
    .tool.type = TOOL_TYPE_ANY,
    .render_data = {
        .item = {
            .texture_x = 13,
            .texture_y = 2,
        },
    },
};

