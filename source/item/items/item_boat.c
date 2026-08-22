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

#include "../../entity/entity.h"
#include "../../graphics/render_item.h"
#include "../../network/server_local.h"

// onItemPlace: spawn a rideable boat entity centred in the targeted empty cell,
// facing away from the acting player. Server-authoritative; the spawn is
// mirrored to the client via CRPC_SPAWN_BOAT. Returns true so one boat is
// consumed.
static bool boat_place(struct server_local* s, struct item_data* it,
					   struct block_info* where, struct block_info* on,
					   enum side on_side) {
	(void)it;
	(void)on;
	(void)on_side;

	// Player look yaw (degrees, negated camera yaw) -> boat heading (radians),
	// matching the sin(yaw)/cos(yaw) convention the entity uses.
	float yaw = glm_rad(-(float)s->players[s->active_player_id].rx);

	// Spawn the hull resting ON the cell floor, not embedded in it. The boat's
	// AABB is BOAT_HEIGHT tall and centred on the entity origin, so the origin
	// must sit at least BOAT_HEIGHT/2 above the cell floor; otherwise the hull's
	// bottom clips into the block below and every steering impulse is cancelled
	// by the resulting horizontal collision -- the boat looks placed but never
	// moves. A small extra epsilon lets gravity settle it cleanly onto uneven
	// ground.
	server_local_spawn_boat((vec3) {where->x + 0.5F,
									where->y + BOAT_HEIGHT / 2.0F + 0.05F,
									where->z + 0.5F},
							yaw, s);

	return true;
}

struct item item_boat = {
	.name = "Boat",
	.has_damage = false,
	.max_stack = 1,
	.fuel = 0,
	.renderItem = render_item_flat,
	.onItemPlace = boat_place,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			// No dedicated boat sprite in the item atlas; reuse the brown
			// saddle tile (8,6) for the hotbar icon rather than add new art.
			.texture_x = 8,
			.texture_y = 8,
		},
	},
};
