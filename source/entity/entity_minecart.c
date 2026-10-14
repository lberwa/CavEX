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
// todo: Bounding box behavior (not clipping through blocks and such

#include "../block/blocks_data.h"
#include "../game/game_state.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "entity.h"
#include "../graphics/gfx_settings.h"
#include "entity_minecart.h"
#include "../platform/gfx.h"
#include "../platform/texture.h"
#include <math.h>
#include "../platform/displaylist.h"
#include "../graphics/render_block.h"
#include "../graphics/render_entity.h"
#include "../network/client_interface.h"
#include "../network/server_interface.h"
#include "../network/server_local.h"
#include "../platform/gfx.h"
#include "../item/items.h"


#define MC_EPS 0.02f
#define MINECART_RENDER_Y_OFFSET 0.05f

static inline float tile_center(float v) {
    // Center of current tile
    return floorf(v) + 0.5f;
}

static inline int is_near_center_axis(float pos, int axis_is_x) {
    float c = axis_is_x ? tile_center(pos) : tile_center(pos);
    (void)c; // silence -O2 warning
    return fabsf((axis_is_x ? pos : pos) - (axis_is_x ? tile_center(pos) : tile_center(pos))) < MC_EPS;
}

static inline int is_near_center2(float x, float z) {
    return fabsf(x - tile_center(x)) < MC_EPS && fabsf(z - tile_center(z)) < MC_EPS;
}

static inline bool is_rail_type(uint8_t type) {
    return type == BLOCK_RAIL
        || type == BLOCK_POWERED_RAIL
        || type == BLOCK_DETECTOR_RAIL;
}

// Turn table for curved rails (6..9) based on *effective* incoming dir (dx,dz).
// Meta meaning (MC Beta 1.7.3):
// 6: SE (connects South(+z) & East(+x))
// 7: SW (connects South(+z) & West(-x))
// 8: NW (connects North(-z) & West(-x))
// 9: NE (connects North(-z) & East(+x))
static inline void curve_turn(uint8_t meta, int in_dx, int in_dz, int *out_dx, int *out_dz) {
    *out_dx = in_dx; *out_dz = in_dz; // default: no change (safety)
    switch (meta) {
        case 6: // SE
            if (in_dz == -1) { *out_dx = +1; *out_dz = 0; }   // N -> E
            else if (in_dx == -1) { *out_dx = 0; *out_dz = +1; } // W -> S
            break;
        case 7: // SW
            if (in_dz == -1) { *out_dx = -1; *out_dz = 0; }   // N -> W
            else if (in_dx == +1) { *out_dx = 0; *out_dz = +1; } // E -> S
            break;
        case 8: // NW
            if (in_dz == +1) { *out_dx = -1; *out_dz = 0; }   // S -> W
            else if (in_dx == +1) { *out_dx = 0; *out_dz = -1; } // E -> N
            break;
        case 9: // NE
            if (in_dz == +1) { *out_dx = +1; *out_dz = 0; }   // S -> E
            else if (in_dx == -1) { *out_dx = 0; *out_dz = -1; } // W -> N
            break;
        default: break;
    }
}

// Seed initial heading (hx,hz) on a tile given metadata and current speed sign.
// Returns (hx,hz) with |hx|+|hz|==1. Straight/slopes pick axis; curves pick axis
// towards the tile center to avoid ambiguity when spawning on a curve.
static inline void seed_heading(uint8_t meta, float px, float pz, float spd, int8_t *hx, int8_t *hz) {
    int sgn = (spd >= 0.0f) ? +1 : -1;
    switch (meta) {
        case 0: case 4: case 5:  *hx = 0;     *hz = sgn; break; // NS (lock X)
        case 1: case 2: case 3:  *hx = sgn;   *hz = 0;   break; // EW (lock Z)
        case 6: case 7: case 8: case 9: {
            float cx = tile_center(px), cz = tile_center(pz);
            float dx = cx - px, dz = cz - pz;
            if (fabsf(dx) > fabsf(dz)) { *hx = (dx > 0 ? +1 : -1) * sgn; *hz = 0; }
            else { *hx = 0; *hz = (dz > 0 ? +1 : -1) * sgn; }
            break;
        }
        default: *hx = 0; *hz = (sgn != 0 ? sgn : +1); break;
    }
}

// Try to find a rail at (bx, by, bz); if not found, also try by+1 and by-1.
// Returns 1 and fills *out/*out_by when a rail is found, else 0.
static inline int get_rail_with_y_fallback(int bx, int by, int bz,
                                           struct block_data* out, int* out_by) {
    struct block_data b0 = world_get_block(&gstate.world, bx, by, bz);
    if (is_rail_type(b0.type)) {
        *out = b0; *out_by = by; return 1;
    }
    struct block_data b1 = world_get_block(&gstate.world, bx, by + 1, bz);
    if (is_rail_type(b1.type)) {
        *out = b1; *out_by = by + 1; return 1;
    }
    struct block_data b2 = world_get_block(&gstate.world, bx, by - 1, bz);
    if (is_rail_type(b2.type)) {
        *out = b2; *out_by = by - 1; return 1;
    }
    return 0;
}

// Absolute height on a slope tile (linear, center-based).
// by is the chosen rail-block Y (after fallback), cx/cz are centers of the current tile.
// current_y is used for non-slope metas.
static inline float slope_height(uint8_t meta, int by,
                                 float x, float z, float cx, float cz,
                                 float current_y)
{
    switch (meta) {
        case 2: /* up to E (+X) */ return (float)by + 0.5f + (x - cx);
        case 3: /* up to W (-X) */ return (float)by + 0.5f - (x - cx);
        case 4: /* up to N (-Z) */ return (float)by + 0.5f - (z - cz);
        case 5: /* up to S (+Z) */ return (float)by + 0.5f + (z - cz);
        default: return current_y; // non-slope
    }
}
// Keep comments in English per project style.
// Keep comments in English per project style.
static bool minecart_server_tick(struct entity* e, struct server_local* s) {
    // Provide a stable previous for interpolation in this sim step.
    glm_vec3_copy(e->pos, e->pos_old);

    // Find base block coords under cart
    int bx = (int)floorf(e->pos[0]);
    int by_guess = (int)floorf(e->pos[1] - 0.1f);
    int bz = (int)floorf(e->pos[2]);

    // Robust rail lookup with ±1Y fallback (crucial at slope seams)
    struct block_data below;
    int rail_y = by_guess;
    if (!get_rail_with_y_fallback(bx, by_guess, bz, &below, &rail_y)) {
        e->data.minecart.speed = 0.0f;
        return false;
    }

    // Stable centers for THIS tile (prevents micro drift across frames)
    float cx = (float)bx + 0.5f;
    float cz = (float)bz + 0.5f;

    // Regular rails: bits 0-3 = shape (0-9). Powered/detector: bits 0-2 = shape, bit 3 = on/activated.
    uint8_t meta = below.metadata & (below.type == BLOCK_RAIL ? 0x0F : 0x07);

    // Powered rail: boost when powered, extra braking when unpowered.
    bool on_active_powered_rail = false;
    if (below.type == BLOCK_POWERED_RAIL) {
        if (below.metadata & 0x08) {
            // Powered rails restart the allowed slope oscillation cycle.
            e->data.minecart.slope_restarts = 0;
            // Only accelerate when already moving, always in the current direction.
            // Cap is 0.4 (double the normal rolling max) to reward powered rail use.
            float spd = e->data.minecart.speed;
            if (spd != 0.0f) {
                float dir = (spd > 0.0f) ? 1.0f : -1.0f;
                spd += dir * 0.01f;
                if (fabsf(spd) > 0.4f) spd = dir * 0.4f;
                e->data.minecart.speed = spd;
                on_active_powered_rail = true;
            }
        } else {
            // Unpowered: act as brake rail (higher friction, but cart keeps moving)
            e->data.minecart.speed *= 0.5f;
        }
    }

    // Friction — skipped on active powered rails so nothing can counteract the boost.
    if (!on_active_powered_rail)
        e->data.minecart.speed *= 0.97f;
    if (fabsf(e->data.minecart.speed) < 0.001f)
        e->data.minecart.speed = 0.0f;

    float spd = e->data.minecart.speed;
    if (spd == 0.0f) {
        /* A cart resting on a slope starts rolling downhill by itself. */
        if(e->data.minecart.slope_restarts >= 2) {
            e->data.minecart.rail_meta = meta;
            e->data.minecart.last_meta = meta;
            return false;
        }
        switch(meta) {
            case 2: /* rises east, roll west */
                e->data.minecart.hx = -1;
                e->data.minecart.hz = 0;
                break;
            case 3: /* rises west, roll east */
                e->data.minecart.hx = 1;
                e->data.minecart.hz = 0;
                break;
            case 4: /* rises north, roll south */
                e->data.minecart.hx = 0;
                e->data.minecart.hz = 1;
                break;
            case 5: /* rises south, roll north */
                e->data.minecart.hx = 0;
                e->data.minecart.hz = -1;
                break;
            default:
                e->data.minecart.rail_meta = meta;
                e->data.minecart.last_meta = meta;
                return false;
        }
        e->data.minecart.slope_restarts++;
        e->data.minecart.speed = 0.0025f;
        spd = e->data.minecart.speed;
    }

    // Ensure heading exists; use the persistent hx,hz in entity.h
    if (e->data.minecart.hx == 0 && e->data.minecart.hz == 0) {
        seed_heading(meta, e->pos[0], e->pos[2], spd, &e->data.minecart.hx, &e->data.minecart.hz);
    }

    int sgn    = (spd >= 0.0f) ? +1 : -1;
    int cur_dx = e->data.minecart.hx * sgn;
    int cur_dz = e->data.minecart.hz * sgn;

    /* Slopes change the rolling speed: uphill costs a little speed, while
     * downhill movement gives the cart a small additional push. */
    bool uphill = (meta == 2 && cur_dx > 0)
        || (meta == 3 && cur_dx < 0)
        || (meta == 4 && cur_dz < 0)
        || (meta == 5 && cur_dz > 0);
    if(meta >= 2 && meta <= 5) {
        float slope_speed = fabsf(spd) + (uphill ? -0.0025f : 0.0025f);
        if(slope_speed < 0.0f)
            slope_speed = 0.0f;
        if(slope_speed > 0.4f)
            slope_speed = 0.4f;
        spd = copysignf(slope_speed, spd);
        e->data.minecart.speed = spd;
    }

    // Cheap helpers
    #define MOVE_ALONG_X(amount) do { e->pos[0] += (amount) * (float)cur_dx; } while (0)
    #define MOVE_ALONG_Z(amount) do { e->pos[2] += (amount) * (float)cur_dz; } while (0)

    switch (meta) {
        // --- Straight NS ---
        case 0:
            e->pos[0] = cx;                // lock X
            MOVE_ALONG_Z(fabsf(spd));
            break;

        // --- Straight EW ---
        case 1:
            e->pos[2] = cz;                // lock Z
            MOVE_ALONG_X(fabsf(spd));
            break;

        // --- Slopes EW (2: up to W, 3: up to E) ---
        case 2: // ascend East (+X uphill)
        case 3: { // ascend West (-X uphill)
            e->pos[2] = cz;                // lock Z (unchanged)
            MOVE_ALONG_X(fabsf(spd));      // move along X (unchanged)

            // Deterministic absolute Y from tile center (no accumulation drift)
            float y = slope_height(meta, rail_y, e->pos[0], e->pos[2], cx, cz, e->pos[1]);
            // Clamp to [rail_y, rail_y+1] to avoid tiny float overshoot
            if (y < (float)rail_y) y = (float)rail_y;
            if (y > (float)rail_y + 1.0f) y = (float)rail_y + 1.0f;
            e->pos[1] = y;
            break;
        }

        // --- Slopes NS (4: up to N, 5: up to S) ---
        case 4: // ascend North (-Z uphill)
        case 5: { // ascend South (+Z uphill)
            e->pos[0] = cx;                // lock X (unchanged)
            MOVE_ALONG_Z(fabsf(spd));      // move along Z (unchanged)

            float y = slope_height(meta, rail_y, e->pos[0], e->pos[2], cx, cz, e->pos[1]);
            if (y < (float)rail_y) y = (float)rail_y;
            if (y > (float)rail_y + 1.0f) y = (float)rail_y + 1.0f;
            e->pos[1] = y;
            break;
        }

        // --- Curves (center -> turn -> out) ---
        case 6: case 7: case 8: case 9: {
            if (cur_dx != 0) e->pos[2] = cz; else e->pos[0] = cx;

            float dist_to_center = (cur_dx != 0)
                ? (cx - e->pos[0]) * (float)cur_dx
                : (cz - e->pos[2]) * (float)cur_dz;

            float step = fabsf(spd);

            if (dist_to_center > MC_EPS) {
                float move = (step <= dist_to_center) ? step : dist_to_center;
                if (cur_dx != 0) MOVE_ALONG_X(move); else MOVE_ALONG_Z(move);
                if (step <= dist_to_center + 1e-6f) break;
                step -= dist_to_center;
            }

            int out_dx = cur_dx, out_dz = cur_dz;
            curve_turn(meta, cur_dx, cur_dz, &out_dx, &out_dz);

            if (out_dx != 0) { e->pos[2] = cz; e->pos[0] += step * (float)out_dx; }
            else              { e->pos[0] = cx; e->pos[2] += step * (float)out_dz; }

            e->data.minecart.hx = (int8_t)(out_dx * sgn);
            e->data.minecart.hz = (int8_t)(out_dz * sgn);
            break;
        }

        default:
            // Unknown meta: clamp to center to avoid drifting off-rail
            e->pos[0] = cx;
            e->pos[2] = cz;
            break;
    }

    // Save meta for debugging/consistency
    e->data.minecart.rail_meta = meta;
    e->data.minecart.last_meta = meta;

    // IMPORTANT: do NOT touch rider here. Local player handles camera attach.

    return false;
}


static bool minecart_client_tick(struct entity* e) {
    return minecart_server_tick(e, NULL);
}

static void entity_minecart_render(struct entity* e, mat4 view, float tick_delta) {
    vec3 pos_lerp;
    glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

    struct block_data in_block;
    entity_get_block(e,
        floorf(pos_lerp[0]),
        floorf(pos_lerp[1]),
        floorf(pos_lerp[2]),
        &in_block
    );

    render_entity_update_light(
        (in_block.torch_light << 4) | (in_block.sky_light)
    );

    struct block_data rail_block;
    int bx = (int)floorf(pos_lerp[0]);
    int by = (int)floorf(pos_lerp[1] - 0.1f);
    int bz = (int)floorf(pos_lerp[2]);
    entity_get_block(e, bx, by, bz, &rail_block);

    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    float y_offset = 0.0f;

    // Prefer live block lookup; fall back to last-known meta stored by server tick.
    uint8_t meta;
    if (is_rail_type(rail_block.type))
        meta = rail_block.metadata & (rail_block.type == BLOCK_RAIL ? 0x0F : 0x07);
    else
        meta = e->data.minecart.rail_meta;

    int8_t hx = e->data.minecart.hx;
    int8_t hz = e->data.minecart.hz;

    switch (meta) {
        // Slopes: yaw is always axis-aligned (EW or NS), pitch sign encodes which end
        // is higher. This never depends on movement direction — the terrain is fixed.
        case 2: yaw_deg =  90.0f; pitch_deg = +45.0f; y_offset = 0.1f; break; // E up
        case 3: yaw_deg =  90.0f; pitch_deg = -45.0f; y_offset = 0.1f; break; // W up
        case 4: yaw_deg =   0.0f; pitch_deg = +45.0f; y_offset = 0.1f; break; // N up
        case 5: yaw_deg =   0.0f; pitch_deg = -45.0f; y_offset = 0.1f; break; // S up
        // Curves: hx/hz are always cardinal (never diagonal), so use fixed bisector.
        case 6: yaw_deg =  45.0f;   break;
        case 7: yaw_deg = -45.0f;   break;
        case 8: yaw_deg = -135.0f;  break;
        case 9: yaw_deg =  135.0f;  break;
        // Straight rails: follow heading when moving, meta when stationary.
        default:
            if (hx != 0 || hz != 0)
                yaw_deg = glm_deg(atan2f((float)hx, (float)hz));
            else
                yaw_deg = (meta == 1) ? 90.0f : 0.0f;
            break;
    }

    mat4 model, mv;
    glm_translate_make(model, (vec3){
        pos_lerp[0],
        pos_lerp[1] + y_offset + MINECART_RENDER_Y_OFFSET,
        pos_lerp[2]
    });

    glm_rotate_y(model, glm_rad(-yaw_deg), model);
    glm_rotate_x(model, glm_rad(pitch_deg), model);

    glm_mat4_mul(view, model, mv);

    render_entity_minecart(mv);

    struct AABB bbox;
    aabb_setsize_centered(&bbox, 0.25F, 0.25F, 0.25F);
    aabb_translate(&bbox,
        pos_lerp[0],
        pos_lerp[1] - 0.04F,
        pos_lerp[2]
    );
    entity_shadow(e, &bbox, view);
}

static size_t getBoundingBox(const struct entity *e, struct AABB *out) {
    assert(e && out);
    // Keep strictly inside one tile: < 1.0 in X and Z so the AABB never touches
    // a neighbour block boundary. aabb_intersection uses <=, so exact touching
    // would block a 3x3 area around the cart during block placement checks.
	aabb_setsize_centered_offset(
		out,
		0.98f,  // sx
		0.625f, // sy
		0.98f,  // sz: was 1.25, overflowed 0.125 into N/S tiles
		0.0f,
		0.3125f,
		0.0f
	);
    aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
    return 1;
}

// entity_minecart.c (of waar je onRightClick staat)

static bool onRightClick(struct entity* e, struct item_data *held) {
	assert(e);

	struct entity* player = gstate.local_player;
	if(!player) return false;

	if(!e->data.minecart.occupied) {
		// Mount minecart
		e->data.minecart.occupied    = true;
		e->data.minecart.occupant_id = player->id;
		e->rightClickText = "Dismount";
		return true;
	}
	else if(e->data.minecart.occupied &&
	        e->data.minecart.occupant_id == player->id) {
		// Dismount
		e->data.minecart.occupied    = false;
		e->data.minecart.occupant_id = 0;
		player->pos[1] += 1.2f; // avoid being stuck
		e->rightClickText = "Ride";
		return true;
	}

	return false;
}

static bool onLeftClick(struct entity *e) {
    assert(e);
    svin_rpc_send(&(struct server_rpc){
        .type = SRPC_ENTITY_ATTACK,
        .payload.entity_attack.entity_id = e->id,
    });
    return true;
}

void entity_minecart(uint32_t id, struct entity* e, bool server, void* world) {
    entity_default_init(e, server, world);

    e->name 	   = "Minecart";
    e->id          = id;
    e->tick_server = minecart_server_tick;
    e->tick_client = minecart_client_tick;
    e->render      = entity_minecart_render;
    e->teleport    = entity_default_teleport;
    e->type        = ENTITY_MINECART;
    e->on_ground   = true;
    e->getBoundingBox = getBoundingBox;
    e->leftClickText = NULL;
    e->onLeftClick   = onLeftClick;
    e->rightClickText = e->data.minecart.occupied ? "Dismount" : "Ride";
    e->onRightClick   = onRightClick;

    // zero vectors
    glm_vec3_zero(e->pos);
    glm_vec3_zero(e->pos_old);
    glm_vec3_zero(e->vel);
    glm_vec2_zero(e->orient);
    glm_vec2_zero(e->orient_old);

    e->data.minecart.hx = 0;
    e->data.minecart.hz = 0;
    e->data.minecart.last_meta = 0xFF;
	 e->data.minecart.slope_restarts = 0;

    e->data.minecart.item.id         = ITEM_MINECART;
    e->data.minecart.item.durability = 0;
    e->data.minecart.item.count      = 1;
    e->health = 10;
    e->data.minecart.speed = 0.0f;
    e->data.minecart.occupied = false;
    e->data.minecart.occupant_id = 0;

}
