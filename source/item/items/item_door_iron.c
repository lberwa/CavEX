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

#include "../../network/server_local.h"

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	struct block_data support;
	if(!server_world_get_block(AWORLD(s), where->x, where->y - 1, where->z,
							   &support))
		return false;
	if(!blocks[support.type] || blocks[support.type]->can_see_through)
		return false;

	struct block_data top;
	if(!server_world_get_block(AWORLD(s), where->x, where->y + 1, where->z,
							   &top))
		return false;
	if(blocks[top.type] && !blocks[top.type]->place_ignore)
		return false;

	bool hinge_left = false;
	if(s->placement_hit_valid) {
		/* Server yaw points forward as (-sin(yaw), cos(yaw)); its screen
		 * right vector is therefore (cos(yaw), sin(yaw)). */
		float rx = glm_rad((float)s->players[s->active_player_id].rx);
		float hx = s->placement_hit_x - (on->x + 0.5F);
		float hz = s->placement_hit_z - (on->z + 0.5F);
		hinge_left = hx * cosf(rx) + hz * sinf(rx) < 0.0F;
	}
	(void)on_side;
	float player_x = s->placement_player_pos_valid
		? (float)s->placement_player_x
		: (float)s->players[s->active_player_id].x;
	float player_z = s->placement_player_pos_valid
		? (float)s->placement_player_z
		: (float)s->players[s->active_player_id].z;
	double dx = player_x - (where->x + 0.5);
	double dz = player_z - (where->z + 0.5);
	uint8_t dir;
	if(fabs(dx) > fabs(dz))
		dir = (dx >= 0.0) ? 2 : 0;
	else
		dir = (dz >= 0.0) ? 3 : 1;

	(void)on;
	(void)on_side;

	struct block_data blk2 = (struct block_data) {
		.type = BLOCK_DOOR_IRON,
		.metadata = dir,
		.sky_light = 0,
		.torch_light = 0,
	};

	struct block_info blk2_info = *where;
	blk2_info.block = &blk2;
	vec3 player_pos = {
		player_x,
		s->placement_player_pos_valid
			? (float)s->placement_player_y
			: (float)s->players[s->active_player_id].y,
		player_z};

	if(entity_local_player_block_collide(player_pos, &blk2_info))
		return false;

	struct block_info top_info = *where;
	top_info.y++;
	struct block_data top_door = blk2;
	top_door.metadata |= 0x08;
	top_info.block = &top_door;
	if(entity_local_player_block_collide(player_pos, &top_info))
		return false;

	server_world_set_block(s, where->x, where->y, where->z, blk2);
	block_door_set_hinge(s, where->x, where->y, where->z,
					 hinge_left, dir);

	/* The upper half stores the hinge bit so the renderer can mirror the
	 * door texture even though the two halves have separate block metadata. */
	top_door.metadata = (uint8_t)(dir | (hinge_left ? 0x04 : 0) | 0x08);
	server_world_set_block(s, where->x, where->y + 1, where->z, top_door);
	return true;
}

struct item item_door_iron = {
	.name = "Iron Door",
	.has_damage = false,
	.max_stack = 64,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 12,
			.texture_y = 2,
		},
	},
};
