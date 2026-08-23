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

/* #define SIMULATE_OOM 150 */ /* auskommentiert = deaktiviert */

/* ---- CHUNK DEBUG ---- Auf 1 setzen, nur diese Datei neu kompilieren ---- */
#define CHUNK_MESHER_DEBUG 0
/* ----------------------------------------------------------------------- */
#if CHUNK_MESHER_DEBUG
#include <stdarg.h>
#include <stdio.h>
#ifdef PLATFORM_WII
#include <gccore.h>
#endif
#include "network/server_comunication.h"
static void _mdbg(const char* fmt, ...) {
	char buf[192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	debug_send(buf);
}
#define MDBG(...) _mdbg(__VA_ARGS__)
#else
#define MDBG(...) ((void)0)
#endif

#include <assert.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>

#include "chunk_mesher.h"
#include "network/server_local.h"
#include "platform/displaylist.h"
#include "platform/thread.h"
#include "stack.h"
#include "world.h"

#define BLK_INDEX(x, y, z)                                                     \
	((x) + ((z) + (y) * (CHUNK_SIZE + 2)) * (CHUNK_SIZE + 2))
#define BLK_INDEX2(x, y, z) ((x) + ((z) + (y) * CHUNK_SIZE) * CHUNK_SIZE)
#define BLK_DATA(b, x, y, z) ((b)[BLK_INDEX((x) + 1, (y) + 1, (z) + 1)])

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

struct chunk_mesher_rpc {
	struct chunk* chunk;
	// ingoing
	struct {
		struct block_data* blocks;
		struct block_data bd_buf[(CHUNK_SIZE + 2) * (CHUNK_SIZE + 2) * (CHUNK_SIZE + 2)];
		uint8_t light_data_buf[(CHUNK_SIZE + 2) * (CHUNK_SIZE + 2) * (CHUNK_SIZE + 2) * 3];
		bool visited_buf[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
		uint8_t queue_buf[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE * 3];
	} request;
	// outgoing
	struct {
		struct displaylist mesh[13];
		bool has_displist[13];
		uint8_t reachable[6];
	} result;
};

static struct chunk_mesher_rpc rpc_msg[CHUNK_MESHER_QLENGTH];
static struct thread_channel mesher_requests;
static struct thread_channel mesher_results;
static struct thread_channel mesher_empty_msg;

/* debug counters for the in-game overlay -- tell us where the mesh pipeline
 * stalls: sent (queued to worker), failed (queue/alloc full), received
 * (results applied), built (worker actually finished a mesh). */
volatile unsigned long chunk_mesher_dbg_sent = 0;
volatile unsigned long chunk_mesher_dbg_failed = 0;
volatile unsigned long chunk_mesher_dbg_recv = 0;
volatile unsigned long chunk_mesher_dbg_built = 0;

/* Mesher-Thread-Stats für die Debug-Anzeige (geschrieben vom Mesher-Thread,
 * gelesen vom Main-Thread – volatile reicht für einfache floats/ints). */
volatile float chunk_mesher_stat_ms_per_build = 0.0f;
volatile float chunk_mesher_stat_builds_per_sec = 0.0f;
volatile int   chunk_mesher_stat_queue_depth = 0;

static int chunk_test_side(enum side* on_sides, c_coord_t x, c_coord_t y,
						   c_coord_t z) {
	assert(on_sides);

	int count = 0;

	if(x == 0)
		on_sides[count++] = SIDE_LEFT;

	if(x == CHUNK_SIZE - 1)
		on_sides[count++] = SIDE_RIGHT;

	if(y == 0)
		on_sides[count++] = SIDE_BOTTOM;

	if(y == CHUNK_SIZE - 1)
		on_sides[count++] = SIDE_TOP;

	if(z == 0)
		on_sides[count++] = SIDE_FRONT;

	if(z == CHUNK_SIZE - 1)
		on_sides[count++] = SIDE_BACK;

	return count;
}

static void chunk_test(struct block_data* bd, struct stack* queue,
					   bool* visited, uint8_t* reachable, c_coord_t x,
					   c_coord_t y, c_coord_t z) {
	assert(bd && queue && visited && reachable);

	if(visited[BLK_INDEX2(x, y, z)]
	   || (blocks[BLK_DATA(bd, x, y, z).type]
		   && !blocks[BLK_DATA(bd, x, y, z).type]->can_see_through))
		return;

	stack_clear(queue);
	stack_push(queue, (uint8_t[]) {x, y, z});
	visited[BLK_INDEX2(x, y, z)] = true;

	uint8_t reached_sides = 0;

	while(!stack_empty(queue)) {
		uint8_t block[3];
		stack_pop(queue, block);

		enum side on_sides[3];
		size_t on_sides_len
			= chunk_test_side(on_sides, block[0], block[1], block[2]);
		assert(on_sides_len <= 3);

		for(size_t k = 0; k < on_sides_len; k++)
			reached_sides |= (1 << on_sides[k]);

		for(int s = 0; s < 6; s++) {
			int nx, ny, nz;
			blocks_side_offset(s, &nx, &ny, &nz);
			nx += block[0];
			ny += block[1];
			nz += block[2];

			if(nx >= 0 && ny >= 0 && nz >= 0 && nx < CHUNK_SIZE
			   && ny < CHUNK_SIZE && nz < CHUNK_SIZE
			   && !visited[BLK_INDEX2(nx, ny, nz)]
			   && (!blocks[BLK_DATA(bd, nx, ny, nz).type]
				   || blocks[BLK_DATA(bd, nx, ny, nz).type]->can_see_through)) {
				stack_push(queue, (uint8_t[]) {nx, ny, nz});
				visited[BLK_INDEX2(nx, ny, nz)] = true;
			}
		}
	}

	for(int s = 0; s < 6; s++) {
		if(reached_sides & (1 << s))
			reachable[s] |= reached_sides;
	}
}

static void chunk_test_init(struct block_data* bd, uint8_t* reachable,
							bool* visited, void* queue_buf) {
	assert(bd && reachable && visited && queue_buf);

	memset(reachable, 0, 6 * sizeof(uint8_t));
	memset(visited, false, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE);

	struct stack queue;
	queue.element_size = 3;
	queue.index = 0;
	queue.length = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
	queue.data = queue_buf;

	for(int y = 0; y < CHUNK_SIZE; y++) {
		for(int x = 0; x < CHUNK_SIZE; x++) {
			chunk_test(bd, &queue, visited, reachable, x, y, 0);
			chunk_test(bd, &queue, visited, reachable, x, 0, y);
			chunk_test(bd, &queue, visited, reachable, 0, x, y);
			chunk_test(bd, &queue, visited, reachable, x, y, CHUNK_SIZE - 1);
			chunk_test(bd, &queue, visited, reachable, x, CHUNK_SIZE - 1, y);
			chunk_test(bd, &queue, visited, reachable, CHUNK_SIZE - 1, x, y);
		}
	}

	/* queue.data zeigt auf queue_buf (statisch) — kein free nötig */
}

static void chunk_mesher_vertex_light(struct block_data* bd,
									  uint8_t* light_data) {
	assert(bd && light_data);

	const int shade_table[5] = {0, 5, 3, 1, 0};

	/* MUSS int sein, nicht c_coord_t (uint32_t): BLK_DATA(bd, x, y-1, z) bei
	 * y=0 wuerde mit uint32_t zu y-1=0xFFFFFFFF und damit zu einem massiven
	 * Out-of-Bounds-Schreibzugriff fuehren -> Crash im Mesher-Thread. */
	for(int y = 0; y < CHUNK_SIZE + 2; y++) {
		for(int z = 0; z < CHUNK_SIZE + 2; z++) {
			for(int x = 0; x < CHUNK_SIZE + 2; x++) {

				if(x != CHUNK_SIZE + 1 && z != CHUNK_SIZE + 1) {
					struct block_data b1[4] = {
						BLK_DATA(bd, x + 0, y - 1, z + 0),
						BLK_DATA(bd, x - 1, y - 1, z + 0),
						BLK_DATA(bd, x + 0, y - 1, z - 1),
						BLK_DATA(bd, x - 1, y - 1, z - 1),
					};

					int sum_sky = 0, sum_torch = 0, count = 0;
					for(int k = 0; k < 4; k++) {
						if(!blocks[b1[k].type]
						   || (blocks[b1[k].type]->can_see_through
							   && !blocks[b1[k].type]->ignore_lighting)) {
							sum_sky += b1[k].sky_light;
							sum_torch += b1[k].torch_light;
							count++;
						}
					}

					sum_torch = count > 0 ? (sum_torch + count - 1) / count : 0;
					sum_sky = count > 0 ? (sum_sky + count - 1) / count : 0;
					sum_torch = MAX(sum_torch - shade_table[count], 0);
					sum_sky = MAX(sum_sky - shade_table[count], 0);

					light_data[BLK_INDEX(x, y, z) * 3 + 0]
						= (sum_torch << 4) | sum_sky;
				}

				if(y != CHUNK_SIZE + 1 && z != CHUNK_SIZE + 1) {
					struct block_data b2[4] = {
						BLK_DATA(bd, x - 1, y + 0, z + 0),
						BLK_DATA(bd, x - 1, y - 1, z + 0),
						BLK_DATA(bd, x - 1, y + 0, z - 1),
						BLK_DATA(bd, x - 1, y - 1, z - 1),
					};

					int sum_sky = 0, sum_torch = 0, count = 0;
					for(int k = 0; k < 4; k++) {
						if(!blocks[b2[k].type]
						   || (blocks[b2[k].type]->can_see_through
							   && !blocks[b2[k].type]->ignore_lighting)) {
							sum_sky += b2[k].sky_light;
							sum_torch += b2[k].torch_light;
							count++;
						}
					}

					sum_torch = count > 0 ? (sum_torch + count - 1) / count : 0;
					sum_sky = count > 0 ? (sum_sky + count - 1) / count : 0;
					sum_torch = MAX(sum_torch - shade_table[count], 0);
					sum_sky = MAX(sum_sky - shade_table[count], 0);

					light_data[BLK_INDEX(x, y, z) * 3 + 1]
						= (sum_torch << 4) | sum_sky;
				}

				if(x != CHUNK_SIZE + 1 && y != CHUNK_SIZE + 1) {
					struct block_data b3[4] = {
						BLK_DATA(bd, x + 0, y + 0, z - 1),
						BLK_DATA(bd, x - 1, y + 0, z - 1),
						BLK_DATA(bd, x + 0, y - 1, z - 1),
						BLK_DATA(bd, x - 1, y - 1, z - 1),
					};

					int sum_sky = 0, sum_torch = 0, count = 0;
					for(int k = 0; k < 4; k++) {
						if(!blocks[b3[k].type]
						   || (blocks[b3[k].type]->can_see_through
							   && !blocks[b3[k].type]->ignore_lighting)) {
							sum_sky += b3[k].sky_light;
							sum_torch += b3[k].torch_light;
							count++;
						}
					}

					sum_torch = count > 0 ? (sum_torch + count - 1) / count : 0;
					sum_sky = count > 0 ? (sum_sky + count - 1) / count : 0;
					sum_torch = MAX(sum_torch - shade_table[count], 0);
					sum_sky = MAX(sum_sky - shade_table[count], 0);

					light_data[BLK_INDEX(x, y, z) * 3 + 2]
						= (sum_torch << 4) | sum_sky;
				}
			}
		}
	}
}

static void chunk_mesher_rebuild(struct block_data* bd, w_coord_t cx,
								 w_coord_t cy, w_coord_t cz,
								 struct displaylist* d, bool count_only,
								 size_t* vertices, uint8_t* light_data_buf) {
	assert(bd && d && vertices);

	uint8_t* light_data = NULL;
	bool light_data_initialized = false;
	(void)light_data_buf;

	for(int k = 0; k < 13; k++)
		vertices[k] = 0;

	for(c_coord_t y = 0; y < CHUNK_SIZE; y++) {
		for(c_coord_t z = 0; z < CHUNK_SIZE; z++) {
			for(c_coord_t x = 0; x < CHUNK_SIZE; x++) {
				struct block_data local = BLK_DATA(bd, x, y, z);

					if(blocks[local.type]) {
						struct block_data neighbours[6];
						struct block_data neighbours_ext[26];
						struct block_info neighbours_info[6];

						for(int k = 0; k < SIDE_MAX; k++) {
							enum side s = (enum side)k;

						int ox, oy, oz;
						blocks_side_offset(s, &ox, &oy, &oz);

						neighbours[k] = BLK_DATA(bd, x + ox, y + oy, z + oz);

							neighbours_info[k] = (struct block_info) {
								.block = neighbours + k,
								.neighbours = NULL,
								.neighbours_ext = NULL,
								.x = cx + x + ox,
								.y = cy + y + oy,
								.z = cz + z + oz,
							};
						}

						bool want_ext = (local.type == BLOCK_REDSTONE_WIRE);
						if (want_ext) {
							int ei = 0;
							for (int dz = -1; dz <= 1; ++dz) {
								for (int dy = -1; dy <= 1; ++dy) {
									for (int dx = -1; dx <= 1; ++dx) {
										if (dx == 0 && dy == 0 && dz == 0) continue;
										neighbours_ext[ei++] = BLK_DATA(bd, x + dx, y + dy, z + dz);
									}
								}
							}
						}

						struct block_info local_info = (struct block_info) {
							.block = &local,
							.neighbours = neighbours,
							.neighbours_ext = want_ext ? neighbours_ext : NULL,
							.x = cx + x,
							.y = cy + y,
							.z = cz + z,
						};

					uint8_t vertex_light[24];
					bool light_loaded = count_only;

					for(int k = 0; k < SIDE_MAX; k++) {
						enum side s = (enum side)k;

						bool face_visible = true;

						if(blocks[neighbours[k].type]
						   && ((!blocks[local.type]->transparent
								&& !blocks[neighbours[k].type]->transparent)
							   || blocks[local.type]->transparent)) {
							struct face_occlusion* a
								= blocks[local.type]->getSideMask(
									&local_info, s, neighbours_info + k);
							struct face_occlusion* b
								= blocks[neighbours[k].type]->getSideMask(
									neighbours_info + k,
									blocks_side_opposite(s), &local_info);

							face_visible = face_occlusion_test(a, b);
						}

						int dp_index = k;

						if(blocks[local.type]->transparent)
							dp_index += 6;

						if(blocks[local.type]->double_sided)
							dp_index = 12;

						if(face_visible
						   || blocks[local.type]->renderBlockAlways) {
							if(!light_data) {
								light_data = light_data_buf;
								if(!light_data_initialized) {
									chunk_mesher_vertex_light(bd, light_data);
									light_data_initialized = true;
								}
							}

							if(!light_loaded) {
								light_loaded = true;
								vertex_light[0]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 0)
													 * 3
												 + 0];
								vertex_light[1]
									= light_data[BLK_INDEX(x + 1, y + 0, z + 0)
													 * 3
												 + 0];
								vertex_light[2]
									= light_data[BLK_INDEX(x + 1, y + 0, z + 1)
													 * 3
												 + 0];
								vertex_light[3]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 1)
													 * 3
												 + 0];
								vertex_light[4]
									= light_data[BLK_INDEX(x + 0, y + 2, z + 0)
													 * 3
												 + 0];
								vertex_light[5]
									= light_data[BLK_INDEX(x + 1, y + 2, z + 0)
													 * 3
												 + 0];
								vertex_light[6]
									= light_data[BLK_INDEX(x + 1, y + 2, z + 1)
													 * 3
												 + 0];
								vertex_light[7]
									= light_data[BLK_INDEX(x + 0, y + 2, z + 1)
													 * 3
												 + 0];

								vertex_light[8]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 0)
													 * 3
												 + 1];
								vertex_light[9]
									= light_data[BLK_INDEX(x + 0, y + 1, z + 0)
													 * 3
												 + 1];
								vertex_light[10]
									= light_data[BLK_INDEX(x + 0, y + 1, z + 1)
													 * 3
												 + 1];
								vertex_light[11]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 1)
													 * 3
												 + 1];
								vertex_light[12]
									= light_data[BLK_INDEX(x + 2, y + 0, z + 0)
													 * 3
												 + 1];
								vertex_light[13]
									= light_data[BLK_INDEX(x + 2, y + 1, z + 0)
													 * 3
												 + 1];
								vertex_light[14]
									= light_data[BLK_INDEX(x + 2, y + 1, z + 1)
													 * 3
												 + 1];
								vertex_light[15]
									= light_data[BLK_INDEX(x + 2, y + 0, z + 1)
													 * 3
												 + 1];

								vertex_light[16]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 0)
													 * 3
												 + 2];
								vertex_light[17]
									= light_data[BLK_INDEX(x + 1, y + 0, z + 0)
													 * 3
												 + 2];
								vertex_light[18]
									= light_data[BLK_INDEX(x + 1, y + 1, z + 0)
													 * 3
												 + 2];
								vertex_light[19]
									= light_data[BLK_INDEX(x + 0, y + 1, z + 0)
													 * 3
												 + 2];
								vertex_light[20]
									= light_data[BLK_INDEX(x + 0, y + 0, z + 2)
													 * 3
												 + 2];
								vertex_light[21]
									= light_data[BLK_INDEX(x + 1, y + 0, z + 2)
													 * 3
												 + 2];
								vertex_light[22]
									= light_data[BLK_INDEX(x + 1, y + 1, z + 2)
													 * 3
												 + 2];
								vertex_light[23]
									= light_data[BLK_INDEX(x + 0, y + 1, z + 2)
													 * 3
												 + 2];
							}
						}

						if(face_visible)
							vertices[dp_index]
								+= blocks[local.type]->renderBlock(
									   d + dp_index, &local_info, s,
									   neighbours_info + k, vertex_light,
									   count_only)
								* 4;

						if(blocks[local.type]->renderBlockAlways)
							vertices[dp_index]
								+= blocks[local.type]->renderBlockAlways(
									   d + dp_index, &local_info, s,
									   neighbours_info + k, vertex_light,
									   count_only)
								* 4;

							if(blocks[local.type]->renderBlockAlwaysTransparent) {
								const int dp_trans = k + 6;
								vertices[dp_trans]
									+= blocks[local.type]->renderBlockAlwaysTransparent(
										   d + dp_trans, &local_info, s,
										   neighbours_info + k, vertex_light,
										   count_only)
									* 4;
							}

						}
					}
				}
			}
		}

	/* light_data zeigt auf light_data_buf (statisch) — kein free nötig. */
}

static void chunk_mesher_build(struct chunk_mesher_rpc* req) {
	for(int k = 0; k < 13; k++) {
		req->result.has_displist[k] = false;
#ifdef PLATFORM_WII
		/* Wii: pos(3x int16) + texcoord(2x uint16) + color(1x uint8) = 11 bytes. */
		displaylist_init(req->result.mesh + k, 64, 3 * 2 + 2 * 2 + 1);
#else
		displaylist_init(req->result.mesh + k, 64, 3 * 2 + 2 * 2 + 1);
#endif
	}

	size_t vertices[13];
	chunk_mesher_rebuild(req->request.blocks, req->chunk->x, req->chunk->y,
						 req->chunk->z, req->result.mesh, false, vertices,
						 req->request.light_data_buf);

	for(int k = 0; k < 13; k++) {
		if(!req->result.mesh[k].failed && vertices[k] > 0
		   && vertices[k] <= 0xFFFF * 4) {
			displaylist_finalize(req->result.mesh + k, vertices[k]);
			req->result.has_displist[k] = true;
		} else {
			displaylist_destroy(req->result.mesh + k);
		}
	}

	chunk_test_init(req->request.blocks, req->result.reachable,
					req->request.visited_buf, req->request.queue_buf);
	chunk_mesher_dbg_built++;
}

static void* chunk_mesher_local_thread(void* user) {
	ptime_t last_build_time = time_get();

	while(1) {
		struct chunk_mesher_rpc* request;
		tchannel_receive(&mesher_requests, (void**)&request, true);

		/* Queue-Tiefe = belegte Slots = Kapazität minus freie Empty-Slots.
		 * Nur auf PC verfügbar (pthreads-Channel hat .count); auf Wii bleibt
		 * der Wert auf 0. */
#ifdef PLATFORM_PC
		chunk_mesher_stat_queue_depth
			= (int)CHUNK_MESHER_QLENGTH - (int)mesher_empty_msg.count;
#endif

		ptime_t t0 = time_get();
		chunk_mesher_build(request);
		ptime_t t1 = time_get();

		float ms = (float)time_diff_ms(t0, t1);
		/* Vergangene Zeit seit letztem Build für builds/s */
		float period_ms = (float)time_diff_ms(last_build_time, t1);
		last_build_time = t1;

		chunk_mesher_stat_ms_per_build
			= chunk_mesher_stat_ms_per_build * 0.9f + ms * 0.1f;
		float inst_bps = (period_ms > 0.0f) ? (1000.0f / period_ms) : 0.0f;
		chunk_mesher_stat_builds_per_sec
			= chunk_mesher_stat_builds_per_sec * 0.9f + inst_bps * 0.1f;

		tchannel_send(&mesher_results, request, true);
	}

	return NULL;
}

void chunk_mesher_init() {
	tchannel_init(&mesher_requests, CHUNK_MESHER_QLENGTH);
	tchannel_init(&mesher_results, CHUNK_MESHER_QLENGTH);
	tchannel_init(&mesher_empty_msg, CHUNK_MESHER_QLENGTH);

	for(int k = 0; k < CHUNK_MESHER_QLENGTH; k++)
		tchannel_send(&mesher_empty_msg, rpc_msg + k, true);

	struct thread t;
	/* Priority 16: ABOVE the local server thread (priority 8). On the single-core
	 * Wii the server (chunk generation) used to preempt the mesher (was priority
	 * 4), so the mesher starved and a backlog of dirty chunks never got rebuilt --
	 * block break/place and freshly generated chunks stayed unrendered. Giving the
	 * mesher the higher priority makes meshing win over generation: the worker
	 * only blocks waiting for requests, so when there is meshing to do it preempts
	 * generation, and generation transparently uses the leftover time. Still well
	 * below the main/render thread, so rendering is unaffected. */
	thread_create(&t, chunk_mesher_local_thread, NULL, 16);
}

void chunk_mesher_receive() {
	struct chunk_mesher_rpc* result;

	while(tchannel_receive(&mesher_results, (void**)&result, false)) {
		for(int k = 0; k < 13; k++) {
			if(result->chunk->has_displist[k])
				displaylist_destroy(result->chunk->mesh + k);

			result->chunk->mesh[k] = result->result.mesh[k];
			result->chunk->has_displist[k] = result->result.has_displist[k];
		}

		for(int k = 0; k < 6; k++)
			result->chunk->reachable[k] = result->result.reachable[k];

		chunk_unref(result->chunk);

		tchannel_send(&mesher_empty_msg, result, true);
		chunk_mesher_dbg_recv++;
	}
}

bool chunk_mesher_send(struct chunk* c) {
	assert(c);

	struct chunk_mesher_rpc* request;
	if(!tchannel_receive(&mesher_empty_msg, (void**)&request, false)) {
		chunk_mesher_dbg_failed++;
		static int queue_full_log = 0;
		if(++queue_full_log % 200 == 1)
			MDBG("[MESHER] queue full fail=%lu sent=%lu built=%lu recv=%lu\n",
				 chunk_mesher_dbg_failed, chunk_mesher_dbg_sent,
				 chunk_mesher_dbg_built, chunk_mesher_dbg_recv);
		return false;
	}

	/* bd_buf liegt direkt im statischen RPC-Slot (BSS) — kein Heap-malloc,
	 * kein OOM-Risiko durch Fragmentierung. */
	struct block_data* bd = request->request.bd_buf;

#ifdef SIMULATE_OOM
	{
		struct mallinfo _mi = mallinfo();
		if((size_t)_mi.uordblks > (size_t)(SIMULATE_OOM) * 1024 * 1024) {
			chunk_mesher_dbg_failed++;
			tchannel_send(&mesher_empty_msg, request, true);
			static int vd_decrement_cooldown = 0;
			if(vd_decrement_cooldown <= 0) {
				if(g_effective_view_distance > 1)
					g_effective_view_distance--;
				vd_decrement_cooldown = 40;
			} else {
				vd_decrement_cooldown--;
			}
			return false;
		}
	}
#endif

	chunk_ref(c);

	/* Ref alle Nachbar-Chunks (3x3x3 minus Mitte) bevor wir bd befuellen.
	 * chunk_lookup_block greift auf Nachbarn zu (Offset -1..+CHUNK_SIZE).
	 * Ohne Ref koennte ein Nachbar zwischen zwei Schleifeniterationen
	 * freigegeben werden -> use-after-free in chunk_get_block. */
	struct chunk* neighbours[27];
	int nb_count = 0;
	for(int dy = -1; dy <= 1; dy++) {
		for(int dz = -1; dz <= 1; dz++) {
			for(int dx = -1; dx <= 1; dx++) {
				if(dx == 0 && dy == 0 && dz == 0)
					continue; /* c selbst ist bereits geref-t */
				struct chunk* nb = world_find_chunk(
					c->world,
					c->x + dx * CHUNK_SIZE,
					c->y + dy * CHUNK_SIZE,
					c->z + dz * CHUNK_SIZE);
				if(nb) {
					chunk_ref(nb);
					neighbours[nb_count++] = nb;
				}
			}
		}
	}

	request->chunk = c;
	request->request.blocks = bd;

	for(w_coord_t y = -1; y < CHUNK_SIZE + 1; y++) {
		for(w_coord_t z = -1; z < CHUNK_SIZE + 1; z++) {
			for(w_coord_t x = -1; x < CHUNK_SIZE + 1; x++) {
				BLK_DATA(bd, x, y, z) = chunk_lookup_block(c, x, y, z);
			}
		}
	}

	for(int i = 0; i < nb_count; i++)
		chunk_unref(neighbours[i]);

	tchannel_send(&mesher_requests, request, true);
	chunk_mesher_dbg_sent++;
	return true;
}
