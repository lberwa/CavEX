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

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#ifdef CAMERA_DEBUG
#if 0
#define CAVEX_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define CAVEX_DEBUG_PRINTF(...) ((void)0)
#endif
#endif

#include "../platform/gfx.h"
#include "../platform/input.h"
#include "camera.h"
#include "game_state.h"

#include "../cglm/cglm.h"

// all vectors normalized
static float plane_distance(vec3 n, vec3 p0, vec3 l0, vec3 l) {
	float d = glm_vec3_dot(n, l);

	if(d < GLM_FLT_EPSILON)
		return FLT_MAX;

	vec3 tmp;
	glm_vec3_sub(p0, l0, tmp);
	return glm_vec3_dot(tmp, n) / d;
}

static void ray_hit_position(float gx0, float gy0, float gz0, vec3 dir,
							 w_coord_t x, w_coord_t y, w_coord_t z,
							 enum side side, float* hit_x, float* hit_y,
							 float* hit_z) {
	float plane;
	float distance;
	int axis;

	switch(side) {
		case SIDE_LEFT: axis = 0; plane = x; break;
		case SIDE_RIGHT: axis = 0; plane = x + 1.0F; break;
		case SIDE_BOTTOM: axis = 1; plane = y; break;
		case SIDE_TOP: axis = 1; plane = y + 1.0F; break;
		case SIDE_FRONT: axis = 2; plane = z; break;
		case SIDE_BACK: axis = 2; plane = z + 1.0F; break;
		default:
			*hit_x = x + 0.5F;
			*hit_y = y + 0.5F;
			*hit_z = z + 0.5F;
			return;
	}

	float origin[3] = {gx0, gy0, gz0};
	if(fabsf(dir[axis]) < GLM_FLT_EPSILON) {
		*hit_x = x + 0.5F;
		*hit_y = y + 0.5F;
		*hit_z = z + 0.5F;
		return;
	}

	distance = (plane - origin[axis]) / dir[axis];
	*hit_x = gx0 + dir[0] * distance;
	*hit_y = gy0 + dir[1] * distance;
	*hit_z = gz0 + dir[2] * distance;
}

void camera_ray_pick(struct world* w, float gx0, float gy0, float gz0,
					 float gx1, float gy1, float gz1,
					 struct camera_ray_result* res, bool allow_water_target) {
	assert(w && res);
	int sx = gx1 > gx0 ? 1 : -1;
	int sy = gy1 > gy0 ? 1 : -1;
	int sz = gz1 > gz0 ? 1 : -1;

	int gx = floor(gx0);
	int gy = floor(gy0);
	int gz = floor(gz0);

	int x1 = floor(gx1);
	int y1 = floor(gy1);
	int z1 = floor(gz1);

	vec3 dir = {gx1 - gx0, gy1 - gy0, gz1 - gz0};
	glm_vec3_normalize(dir);

	// DDA entry side for the current cell; when we step into a new cell we can
	// mark which face was crossed. Useful for targetable "non-solid" blocks
	// like water surface interactions.
	enum side entered_side = SIDE_MAX;

	while(1) {
		enum side s;
		// TODO:
		if(world_block_intersection(w,
									&(struct ray) {.x = gx0,
												   .y = gy0,
												   .z = gz0,
												   .dx = dir[0],
												   .dy = dir[1],
												   .dz = dir[2]},
									gx, gy, gz, &s)) {
			res->x = gx;
			res->y = gy;
			res->z = gz;
			res->hit = true;
			res->side = s;
			ray_hit_position(gx0, gy0, gz0, dir, gx, gy, gz, s,
							 &res->hit_x, &res->hit_y, &res->hit_z);
			return;
		}

		if(allow_water_target) {
			struct block_data blk = world_get_block(w, gx, gy, gz);
			if(blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL) {
				// Only allow targeting the surface of the water.
				if(entered_side == SIDE_TOP) {
					res->x = gx;
					res->y = gy;
					res->z = gz;
					res->hit = true;
					res->side = SIDE_TOP;
					ray_hit_position(gx0, gy0, gz0, dir, gx, gy, gz,
									 SIDE_TOP, &res->hit_x, &res->hit_y,
									 &res->hit_z);
					return;
				}
			}
		}

		bool past_x = (sx > 0) ? (gx >= x1) : (gx <= x1);
		bool past_y = (sy > 0) ? (gy >= y1) : (gy <= y1);
		bool past_z = (sz > 0) ? (gz >= z1) : (gz <= z1);
		if(past_x && past_y && past_z) {
			res->hit = false;
			return;
		}

		float t1 = plane_distance((vec3) {sx, 0, 0},
								  (vec3) {gx + (sx + 1) / 2, 0, 0},
								  (vec3) {gx0, gy0, gz0}, dir);
		float t2 = plane_distance((vec3) {0, sy, 0},
								  (vec3) {0, gy + (sy + 1) / 2, 0},
								  (vec3) {gx0, gy0, gz0}, dir);
		float t3 = plane_distance((vec3) {0, 0, sz},
								  (vec3) {0, 0, gz + (sz + 1) / 2},
								  (vec3) {gx0, gy0, gz0}, dir);

		if(t1 <= t2 && t1 <= t3) {
			entered_side = (sx > 0) ? SIDE_LEFT : SIDE_RIGHT;
			gx += sx;
		} else if(t2 <= t1 && t2 <= t3) {
			entered_side = (sy > 0) ? SIDE_BOTTOM : SIDE_TOP;
			gy += sy;
		} else if(t3 <= t1 && t3 <= t2) {
			entered_side = (sz > 0) ? SIDE_FRONT : SIDE_BACK;
			gz += sz;
		}
	}
}

void camera_physics(struct camera* c, float dt) {
	assert(c);

	float jdx, jdy;
	if(input_joystick(dt, &jdx, &jdy, gstate_active_player())) {
		c->rx -= jdx * 2.0F;
		c->ry -= jdy * 2.0F;
	}

	float acc_x = 0, acc_y = 0, acc_z = 0;
	float speed_c = 40.0F;
	float air_friction = 0.05F;

	if(input_held(IB_LEFT, gstate_active_player())) {
		acc_x += cos(c->rx) * speed_c;
		acc_z -= sin(c->rx) * speed_c;
	}

	if(input_held(IB_RIGHT, gstate_active_player())) {
		acc_x -= cos(c->rx) * speed_c;
		acc_z += sin(c->rx) * speed_c;
	}

	if(input_held(IB_FORWARD, gstate_active_player())) {
		acc_x += sin(c->rx) * sin(c->ry) * speed_c;
		acc_y += cos(c->ry) * speed_c;
		acc_z += cos(c->rx) * sin(c->ry) * speed_c;
	}

	if(input_held(IB_BACKWARD, gstate_active_player())) {
		acc_x -= sin(c->rx) * sin(c->ry) * speed_c;
		acc_y -= cos(c->ry) * speed_c;
		acc_z -= cos(c->rx) * sin(c->ry) * speed_c;
	}

	if(input_held(IB_JUMP, gstate_active_player()))
		acc_y += speed_c;

	if(input_held(IB_SNEAK, gstate_active_player()))
		acc_y -= speed_c;

	c->controller.vx += acc_x * dt;
	c->controller.vy += acc_y * dt;
	c->controller.vz += acc_z * dt;

	c->controller.vx *= powf(air_friction, dt);
	c->controller.vy *= powf(air_friction, dt);
	c->controller.vz *= powf(air_friction, dt);

	struct AABB bbox;

	aabb_setsize_centered(&bbox, 0.6F, 0.6F, 0.6F);
	aabb_translate(&bbox, c->x + c->controller.vx * dt, c->y, c->z);
	if(!world_aabb_intersection(gstate_player_world(gstate_active_player()), &bbox)) {
		c->x += c->controller.vx * dt;
	} else {
		c->controller.vx = 0;
	}

	aabb_setsize_centered(&bbox, 0.6F, 0.6F, 0.6F);
	aabb_translate(&bbox, c->x, c->y + c->controller.vy * dt, c->z);
	if(!world_aabb_intersection(gstate_player_world(gstate_active_player()), &bbox)) {
		c->y += c->controller.vy * dt;
	} else {
		c->controller.vy = 0;
	}

	aabb_setsize_centered(&bbox, 0.6F, 0.6F, 0.6F);
	aabb_translate(&bbox, c->x, c->y, c->z + c->controller.vz * dt);
	if(!world_aabb_intersection(gstate_player_world(gstate_active_player()), &bbox)) {
		c->z += c->controller.vz * dt;
	} else {
		c->controller.vz = 0;
	}

	c->ry = glm_clamp(c->ry, glm_rad(0.5F), GLM_PI - glm_rad(0.5F));
}

void camera_update(struct camera* c, bool in_water) {
	assert(c);

	camera_update_viewport(c, in_water,
						   (float)gfx_width() / (float)gfx_height());
}

void camera_update_viewport(struct camera* c, bool in_water, float aspect) {
	assert(c);

	glm_perspective(glm_rad(gstate.config.fov)
						* (in_water ? 6.0F / 7.0F : 1.0F),
					aspect, 0.075F, gstate.config.render_distance,
					c->projection);

	vec3 _fwd = {sinf(c->rx) * sinf(c->ry), cosf(c->ry),
	             cosf(c->rx) * sinf(c->ry)};
	glm_lookat((vec3) {c->x, c->y, c->z},
			   (vec3) {c->x + _fwd[0], c->y + _fwd[1], c->z + _fwd[2]},
			   (vec3) {0, 1, 0}, c->view);
	/* View-Bob als View-Space-Post-Transform: T_bob * V.
	 * Verschiebt ALLES (Geometrie, Himmel, Wolken) gleichmäßig im View-Space. */
	float _bob_h = sinf(c->walk_bob_phase) * c->walk_bob_amp * 0.08f;
	float _bob_v = -fabsf(cosf(c->walk_bob_phase)) * c->walk_bob_amp * 0.06f; // * 0.06f
	/* Robustheit: ein NaN/Inf im Bob (z.B. aus kaputtem walk_bob_phase) wuerde
	 * via glm_mat4_mul die GESAMTE View-Matrix zerstoeren -- denn NaN*0=NaN
	 * verseucht beim Multiplizieren auch die Rotationsspalten. Dann waere die
	 * komplette 3D-Welt unsichtbar. Lieber den Bob verwerfen als alles killen. */
	if(!isfinite(_bob_h) || !isfinite(_bob_v)) {
		_bob_h = 0.0f;
		_bob_v = 0.0f;
	}
	mat4 _bob_mat;
	glm_translate_make(_bob_mat, (vec3) {_bob_h, _bob_v, 0.0f});
	glm_mat4_mul(_bob_mat, c->view, c->view);

	mat4 view_proj;
	glm_mat4_mul(c->projection, c->view, view_proj);
	glm_frustum_planes(view_proj, c->frustum_planes);
}

void camera_attach(struct camera* c, struct entity* e, float tick_delta,
				   float dt) {
	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);
	c->x = pos_lerp[0];
	c->y = pos_lerp[1];
	c->z = pos_lerp[2];

	// Camera rotation should be owned by the player entity so it persists even
	// if camera structs get reloaded/reset elsewhere (splitscreen swapping etc).
	c->rx = e->orient[0];
	c->ry = e->orient[1];

	//CAVEX_DEBUG_PRINTF("c%.03f %.03f %.03f\n", c->x, c->y, c->z);

	float jdx, jdy;
#ifdef SPLITSCREEN
	int player_index = e->data.local_player.player_index;
#else
	int player_index = 0;
#endif
	bool joy_ok = false;
	jdx = 0.0f;
	jdy = 0.0f;

#ifdef CAMERA_DEBUG
	float rx_before = c->rx;
	float ry_before = c->ry;
#endif

	if(e->data.local_player.capture_input
	   && (joy_ok = input_joystick(dt, &jdx, &jdy, player_index))) {
		c->rx -= jdx * 2.0F;
		c->ry -= jdy * 2.0F;
	}

	#ifdef CAMERA_DEBUG
	float ry_unclamped = c->ry;
	#endif
	c->ry = glm_clamp(c->ry, glm_rad(0.5F), GLM_PI - glm_rad(0.5F));

	#ifdef CAMERA_DEBUG
	// Debug: throttle to avoid spamming stdout.
	static ptime_t last_dbg_print;
	ptime_t now = time_get();
	if(time_diff_ms(last_dbg_print, now) >= 250) {
		float rx_after = c->rx;
		float ry_after = c->ry;
#ifdef PLATFORM_PC
		CAVEX_DEBUG_PRINTF("[camera_attach p=%d eid=%u cap=%d joy=%d] pos=(%.2f %.2f %.2f) "
			   "ent=(%.2f %.2f %.2f old=(%.2f %.2f %.2f) td=%.2f) "
			   "j=(%.5f %.5f) ddeg=(%.2f %.2f) "
			   "rx=%.2f->%.2f ry=%.2f->%.2f clamp(%.2f->%.2f)\n",
			   player_index,
			   (unsigned)e->id,
			   (int)e->data.local_player.capture_input,
			   (int)joy_ok,
			   c->x, c->y, c->z,
			   e->pos[0], e->pos[1], e->pos[2],
			   e->pos_old[0], e->pos_old[1], e->pos_old[2],
			   tick_delta,
			   jdx, jdy,
			   glm_deg(-jdx * 2.0f), glm_deg(-jdy * 2.0f),
			   glm_deg(rx_before), glm_deg(rx_after),
			   glm_deg(ry_before), glm_deg(ry_unclamped),
			   glm_deg(ry_unclamped), glm_deg(ry_after));
#endif
		last_dbg_print = now;
	}
#endif

	e->orient[0] = c->rx;
	e->orient[1] = c->ry;

	c->walk_bob_phase = glm_lerp(e->data.local_player.walk_bob_old,
	                             e->data.local_player.walk_bob, tick_delta);
	c->walk_bob_amp   = gstate.settings.view_bob
		? e->data.local_player.walk_bob_speed : 0.0f;
}


void camera_get_ray(const struct camera *c, vec3 origin, vec3 dir) {
    // Set origin to the camera’s current position
    origin[0] = c->x;
    origin[1] = c->y;
    origin[2] = c->z;

    // Compute an endpoint 4.5 units in front of the camera using rx/ry angles
    vec3 end;
    end[0] = c->x + sinf(c->rx) * sinf(c->ry) * 4.5f;
    end[1] = c->y +            cosf(c->ry) * 4.5f;
    end[2] = c->z + cosf(c->rx) * sinf(c->ry) * 4.5f;

    // Subtract origin from end to get the unnormalized direction
    glm_vec3_sub(end, origin, dir);

    // Normalize the direction vector to unit length
    glm_vec3_normalize(dir);
}
