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
    along with CavEX. If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if 0
#define CAVEX_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define CAVEX_DEBUG_PRINTF(...) ((void)0)
#endif

#include "entity.h"
#include "entity_monster.h"
#include "mob_navigation.h"
#include "../block/blocks.h"
#include "../block/blocks_data.h"
#include "../network/server_local.h"
#include "../network/client_interface.h"
#include "../network/server_interface.h"
#include "../sound/sound.h"

#define CREEPER_FUSE_TICKS 50 /* 2.5 seconds at 20 TPS */
#include "../world.h"
#include "../graphics/render_entity.h"
#include "../platform/gfx.h"
#include "../util.h"
#include "../particle.h"
#include "../game/game_state.h"
#include "../network/server_world.h"




// Creeper movement constants
#define CREEPER_ACCEL            0.10f    // same acceleration as normal walking
#define CREEPER_MAX_SPEED        0.12f    // same practical speed as walking
#define CREEPER_WANDER_INTERVAL  20      // ticks between direction changes
#define GRAVITY                  2.00f   // gravity per tick
#define UNSTUCK_MOVE             0.01f   // slight upward nudge if stuck
#define YAW_SMOOTH_FACTOR        0.20f    // smoothing factor [0..1]

// Pig movement constants (similar to creeper but slightly slower)
#define PIG_ACCEL                0.20f
#define PIG_MAX_SPEED            0.12f

// Sheep
#define SHEEP_ACCEL                0.20f
#define SHEEP_MAX_SPEED            0.12f
#define ANIMAL_FLEE_TICKS          100

#define MOB_TARGET_RANGE           16.0f
#define CREEPER_IGNITE_RANGE       3.0f

/* A player's stored y is the EYE position (see entity_local_player.c), so the
 * feet -- and the floor block to path toward -- are EYE_HEIGHT lower. Using the
 * raw y made every target ~1.6 blocks too high, so the pathfinder aimed above
 * an elevated player and never reached them. */
#define PLAYER_EYE_HEIGHT          1.62f
#define MOB_TARGET_FLOOR_Y(py)     ((int)floorf((py) - PLAYER_EYE_HEIGHT - 0.05f))

/* Find the nearest loaded player in the same world as the mob.  Keeping this
 * in the server tick makes the decision authoritative and deterministic for
 * all clients. */
static bool mob_find_nearest_player(const struct entity* e,
                                    const struct server_local* s,
                                    float max_range, int* out_pid,
                                    float* out_dx, float* out_dz,
                                    float* out_distance_sq) {
    /* Follow range is axis-aligned: 16 blocks vertically counts the same as
     * 16 blocks horizontally.  This also keeps players on a winding stairway
     * detectable instead of rejecting them because of a diagonal distance. */
    float best_distance_sq = FLT_MAX;
    int best_pid = -1;
    float best_dx = 0.0f, best_dz = 0.0f;

    for(int pid = 0; pid < MAX_SERVER_PLAYERS; pid++) {
        const struct server_player* player = &s->players[pid];
        if(!player->has_pos || !player->finished_loading || player->creative
           || (void*)PWORLD(s, pid) != e->world)
            continue;

        float dx = (float)player->x - e->pos[0];
        float dy = (float)player->y - e->pos[1];
        float dz = (float)player->z - e->pos[2];
        float distance_sq = dx * dx + dy * dy + dz * dz;
        /* A true horizontal circle of radius max_range (not a square), plus a
         * vertical band of the same height so a player on a stairway above or
         * below stays detectable. */
        float horizontal_sq = dx * dx + dz * dz;
        if(horizontal_sq <= max_range * max_range && fabsf(dy) <= max_range
           && distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best_pid = pid;
            best_dx = dx;
            best_dz = dz;
        }
    }

    if(best_pid < 0)
        return false;
    if(out_pid) *out_pid = best_pid;
    if(out_dx) *out_dx = best_dx;
    if(out_dz) *out_dz = best_dz;
    if(out_distance_sq) *out_distance_sq = best_distance_sq;
    return true;
}

static bool mob_player_holds_wheat(const struct server_player* player) {
    struct item_data held;
    return player && inventory_get_hotbar_item(
        (struct inventory*)&player->inventory, &held)
        && held.id == ITEM_WHEAT;
}

static bool mob_find_nearest_wheat_player(const struct entity* e,
                                         const struct server_local* s,
                                         float max_range, int* out_pid,
                                         float* out_dx, float* out_dz) {
    float best_distance_sq = FLT_MAX;
    int best_pid = -1;
    float best_dx = 0.0f, best_dz = 0.0f;

    for(int pid = 0; pid < MAX_SERVER_PLAYERS; pid++) {
        const struct server_player* player = &s->players[pid];
        if(!player->has_pos || !player->finished_loading || player->creative
           || (void*)PWORLD(s, pid) != e->world
           || !mob_player_holds_wheat(player))
            continue;

        float dx = (float)player->x - e->pos[0];
        float dy = (float)player->y - e->pos[1];
        float dz = (float)player->z - e->pos[2];
        float distance_sq = dx * dx + dy * dy + dz * dz;
        if(distance_sq <= max_range * max_range
           && distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best_pid = pid;
            best_dx = dx;
            best_dz = dz;
        }
    }

    if(best_pid < 0)
        return false;
    if(out_pid) *out_pid = best_pid;
    if(out_dx) *out_dx = best_dx;
    if(out_dz) *out_dz = best_dz;
    return true;
}

static void mob_set_direction(float dx, float dz, vec2 direction) {
    float length = sqrtf(dx * dx + dz * dz);
    if(length > 0.001f) {
        direction[0] = dx / length;
        direction[1] = dz / length;
    }
}

/* Mobs use their feet as e->pos, just like the player movement code.  Sample
 * the complete mob height so swimming starts as soon as any part of the body
 * enters a water block. */
static bool mob_is_in_water(struct entity* e, float height) {
    int x = (int)floorf(e->pos[0]);
    int z = (int)floorf(e->pos[2]);
    int y0 = (int)floorf(e->pos[1]);
    int y1 = (int)floorf(e->pos[1] + height);
    for(int y = y0; y <= y1; y++) {
        struct block_data block;
        if(entity_get_block(e, x, y, z, &block)
           && (block.type == BLOCK_WATER_FLOW
               || block.type == BLOCK_WATER_STILL))
            return true;
    }
    return false;
}

/* Equivalent to holding jump while swimming: upward impulse when sinking,
 * water drag, and a small downward bias produce the characteristic up/down
 * bob instead of making animals sink to the bottom. */
static void mob_apply_gravity_or_swim(struct entity* e, float height) {
    if(mob_is_in_water(e, height)) {
        e->on_ground = false;
        e->vel[0] *= 0.8f;
        e->vel[2] *= 0.8f;
        if(e->vel[1] < 0.05f)
            e->vel[1] = 0.30f;
        e->vel[1] = e->vel[1] * 0.8f - 0.02f;
    } else {
        entity_apply_gravity(e, GRAVITY);
        entity_apply_friction(e, e->on_ground ? 0.6f : 1.0f);
    }
}

#define MOB_PATH_RADIUS 20
#define MOB_PATH_SIZE (MOB_PATH_RADIUS * 2 + 1)
#define MOB_PATH_NODES (MOB_PATH_SIZE * MOB_PATH_SIZE)
#define MOB_NAV_RADIUS 16
#define MOB_NAV_SIZE (MOB_NAV_RADIUS * 2 + 1)
#define MOB_NAV_HEIGHT (MOB_NAV_RADIUS * 2 + 1)
#define MOB_NAV_VOXELS (MOB_NAV_SIZE * MOB_NAV_SIZE * MOB_NAV_HEIGHT)

/* Debug: the full BFS route (x,y,z per cell) of the most recent grid pathfind,
 * so the JSON export can show the path the creeper actually follows in BFS
 * mode. */
static float g_bfs_path[MOB_PATH_NODES * 3];
static int g_bfs_path_len = 0;

/* Height-aware (3D) BFS working set. The state is (x, z, floor), so the same
 * column can be visited at several heights -- a tiled/pillar/staircase geometry
 * where a cell must be traversed at a specific height no longer gets locked to
 * one height (the old 2.5D bug where the mob circled below an elevated player).
 * Static (not stack) to avoid overflow and per-tick allocation; the server tick
 * is single-threaded so there is no reentrancy. */
#define BFS_VERT 18   /* floor levels tracked around the start */
#define BFS_DOWN 6    /* how far below start_y the window begins */
#define BFS_QMAX 4096 /* max explored states (graceful cap) */
static unsigned char bfs_seen[MOB_PATH_SIZE][MOB_PATH_SIZE][BFS_VERT];
static short bfs_qx[BFS_QMAX];
static short bfs_qz[BFS_QMAX];
static short bfs_qy[BFS_QMAX];
static int bfs_qpar[BFS_QMAX];

static bool mob_block_is_solid(struct entity* e, int x, int y, int z) {
    /* Above the build limit there is only open sky, never a wall. Without this
     * the generic unloaded-cell fallback below (e->on_server) treats every cell
     * above WORLD_HEIGHT as solid, so mob_find_floor's head-clearance check
     * fails and the mob cannot walk on top of tall structures near the build
     * height (it saw the whole sky as a ceiling). */
    if(y >= WORLD_HEIGHT)
        return false;

    struct block_data block;
    if(!entity_get_block(e, x, y, z, &block)) {
        /* Match entity_intersection(): an unloaded server cell is not air.
         * Treating it as walkable lets the navmesh create paths through
         * unknown chunks and makes the mob appear to have no world map. */
        return e->on_server;
    }
    if(!blocks[block.type])
        return false;

    /* A door blocks only while closed. Open, its panel swings aside and the
     * doorway is clear, so navigation must treat it as passable (the full-cube
     * approximation below would otherwise wall it off). The open bit (0x04)
     * lives in the lower half; the upper half reuses that bit for the hinge, so
     * read the lower half for it. */
    if(blocks[block.type] == &block_wooden_door
       || blocks[block.type] == &block_iron_door) {
        bool open;
        if(block.metadata & 0x08) {
            struct block_data lower;
            open = entity_get_block(e, x, y - 1, z, &lower)
                && lower.type == block.type && (lower.metadata & 0x04);
        } else {
            open = (block.metadata & 0x04) != 0;
        }
        return !open;
    }

    /* can_see_through is a rendering property: glass is transparent but
     * still has a full collision box. Use the block's collision definition
     * instead, while the navigation adapter later represents every such
     * collidable block as a full cube. */
    if(blocks[block.type]->getBoundingBox) {
        struct block_data neighbours[SIDE_MAX];
        for(int side = 0; side < SIDE_MAX; side++) {
            int ox, oy, oz;
            blocks_side_offset((enum side)side, &ox, &oy, &oz);
            if(!entity_get_block(e, x + ox, y + oy, z + oz,
                                 &neighbours[side]) && e->on_server)
                neighbours[side] = (struct block_data){.type = BLOCK_STONE};
        }
        struct block_info info = {
            .block = &block,
            .neighbours = neighbours,
            .neighbours_ext = NULL,
            .x = x, .y = y, .z = z
        };
        return blocks[block.type]->getBoundingBox(&info, true, NULL) > 0;
    }

    return !blocks[block.type]->can_see_through;
}

/* C side of the navigation adapter: the C++ tile builder calls this to sample
 * the live world. ctx is the entity pointer passed to mob_nav_steer(). */
int mob_nav_world_solid(void* ctx, int x, int y, int z) {
    return mob_block_is_solid((struct entity*)ctx, x, y, z) ? 1 : 0;
}

static bool mob_find_floor(struct entity* e, int x, int base_y, int z,
                           int* floor_y) {
    /* Search the current level, a one-block step upward, and shallow drops.
     * The order keeps flat movement preferred over unnecessary jumps. */
    static const int offsets[] = {0, 1, -1, -2, -3};
    for(size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        int candidate_y = base_y + offsets[i];
        if(!mob_block_is_solid(e, x, candidate_y, z)
           || mob_block_is_solid(e, x, candidate_y + 1, z)
           || mob_block_is_solid(e, x, candidate_y + 2, z))
            continue;
        *floor_y = candidate_y;
        return true;
    }
    return false;
}

/* Find a bounded height-aware path through the voxel grid.  The y value is
 * carried by each node, so a staircase remains connected while a one-block
 * wall can still be climbed by the normal jump movement. */
static bool mob_set_grid_path_direction(struct entity* e, int target_x, int target_y,
                                   int target_z, vec2 direction) {
    static const int step_x[4] = {1, -1, 0, 0};
    static const int step_z[4] = {0, 0, 1, -1};
    int start_x = (int)floorf(e->pos[0]);
    int start_z = (int)floorf(e->pos[2]);
    int start_y = (int)floorf(e->pos[1] - 0.05f);
    int target_floor_y = target_y;
    int goal_x = target_x - start_x + MOB_PATH_RADIUS;
    int goal_z = target_z - start_z + MOB_PATH_RADIUS;
    if(goal_x < 0) goal_x = 0;
    if(goal_x >= MOB_PATH_SIZE) goal_x = MOB_PATH_SIZE - 1;
    if(goal_z < 0) goal_z = 0;
    if(goal_z >= MOB_PATH_SIZE) goal_z = MOB_PATH_SIZE - 1;

    const int base_y = start_y - BFS_DOWN; /* floor window [base_y, base_y+VERT) */
#define BFS_FOK(fy) ((fy) >= base_y && (fy) < base_y + BFS_VERT)
#define BFS_FIDX(fy) ((fy) - base_y)

    memset(bfs_seen, 0, sizeof(bfs_seen));
    int head = 0, tail = 0;
    bfs_qx[0] = MOB_PATH_RADIUS;
    bfs_qz[0] = MOB_PATH_RADIUS;
    bfs_qy[0] = start_y;
    bfs_qpar[0] = -1;
    tail = 1;
    if(BFS_FOK(start_y))
        bfs_seen[MOB_PATH_RADIUS][MOB_PATH_RADIUS][BFS_FIDX(start_y)] = 1;

    int found_qi = -1;
    while(head < tail) {
        int qi = head++;
        int x = bfs_qx[qi], z = bfs_qz[qi], fy = bfs_qy[qi];
        /* Accept arrival at the player's column within a couple of blocks: the
         * target height (from the player feet) can sit a block off the walkable
         * floor block the BFS tracks. */
        if(x == goal_x && z == goal_z && abs(fy - target_floor_y) <= 2) {
            found_qi = qi;
            break;
        }
        for(int i = 0; i < 4; i++) {
            int nx = x + step_x[i];
            int nz = z + step_z[i];
            if(nx < 0 || nx >= MOB_PATH_SIZE || nz < 0 || nz >= MOB_PATH_SIZE)
                continue;
            int world_x = start_x + nx - MOB_PATH_RADIUS;
            int world_z = start_z + nz - MOB_PATH_RADIUS;
            int next_y;
            if(!mob_find_floor(e, world_x, fy, world_z, &next_y))
                continue;
            if(!BFS_FOK(next_y) || bfs_seen[nx][nz][BFS_FIDX(next_y)])
                continue;
            bfs_seen[nx][nz][BFS_FIDX(next_y)] = 1;
            if(tail >= BFS_QMAX)
                continue; /* explored budget reached: stop growing */
            bfs_qx[tail] = (short)nx;
            bfs_qz[tail] = (short)nz;
            bfs_qy[tail] = (short)next_y;
            bfs_qpar[tail] = qi;
            tail++;
        }
    }

#ifdef FIND_MOB_PATH_DEBUG
    {
        static long p = 0;
        if(p < 60) {
            char hs[128]; int hl = 0;
            for(int f = 0; f < BFS_VERT; f++)
                if(bfs_seen[goal_x][goal_z][f])
                    hl += snprintf(hs + hl, sizeof(hs) - hl, "%d ", base_y + f);
            if(hl == 0) snprintf(hs, sizeof(hs), "(nie)");
            fprintf(stderr,
                "[BFS3D] start=(%d,%d,%d) goal_w=(%d,%d) tgt_y=%d found_qi=%d "
                "states=%d goalcell_reached_at: %s\n",
                start_x, start_z, start_y,
                start_x + goal_x - MOB_PATH_RADIUS,
                start_z + goal_z - MOB_PATH_RADIUS, target_floor_y, found_qi,
                tail, hs);
            fflush(stderr);
            p++;
        }
    }
#endif

    if(found_qi < 0)
        return false;

    /* Collect the full route (goal -> start) into the debug buffer. */
    g_bfs_path_len = 0;
    for(int qi = found_qi; qi >= 0; qi = bfs_qpar[qi]) {
        if(g_bfs_path_len < MOB_PATH_NODES) {
            g_bfs_path[g_bfs_path_len * 3 + 0]
                = (float)(start_x + bfs_qx[qi] - MOB_PATH_RADIUS) + 0.5f;
            g_bfs_path[g_bfs_path_len * 3 + 1] = (float)(bfs_qy[qi] + 1);
            g_bfs_path[g_bfs_path_len * 3 + 2]
                = (float)(start_z + bfs_qz[qi] - MOB_PATH_RADIUS) + 0.5f;
            g_bfs_path_len++;
        }
    }

    /* Walk back to the first step out of the start (whose parent is start, qi 0). */
    int qi = found_qi;
    while(bfs_qpar[qi] > 0)
        qi = bfs_qpar[qi];
    int path_x = bfs_qx[qi], path_z = bfs_qz[qi];

    /* Steer toward the CENTER of the next cell (not just the grid-axis
     * direction): the creeper is ~0.6 wide, and a purely axial heading lets its
     * off-center body clip the wall of an adjacent blocked cell in a one-wide
     * gap. Aiming at the cell center pulls it onto the corridor centre line. */
    mob_set_direction(
        (float)(start_x + path_x - MOB_PATH_RADIUS) + 0.5f - e->pos[0],
        (float)(start_z + path_z - MOB_PATH_RADIUS) + 0.5f - e->pos[2],
        direction);
    return true;
#undef BFS_FOK
#undef BFS_FIDX
}

/* Primary navigation: the persistent tiled Detour navmesh. Creates the
 * per-mob navigator on first use and steers it toward the goal block. Returns
 * true only while an actual navmesh path is being followed. */
static bool mob_update_navigation(struct entity* e, int target_x, int target_y,
                                  int target_z, vec2 direction) {
    if(!e->data.monster.nav) {
        e->data.monster.nav = mob_nav_create();
        if(!e->data.monster.nav)
            return false;
    }

    float dir_x = 0.0f, dir_z = 0.0f;
    int ok = mob_nav_steer((mob_nav*)e->data.monster.nav, e,
                           e->pos[0], e->pos[1], e->pos[2],
                           (float)target_x + 0.5f, (float)target_y + 1.0f,
                           (float)target_z + 0.5f, &dir_x, &dir_z);
    if(!ok)
        return false;
    direction[0] = dir_x;
    direction[1] = dir_z;
    return true;
}

/* Unified navigation hierarchy: tiled navmesh first, then the height-aware BFS
 * fallback. Sets *nav_active (when provided) iff a real navmesh path is being
 * followed, so the caller can keep its reactive cliff-avoidance only in the
 * weaker modes (the navmesh already routes around walls and drops). Returns
 * true when any direction toward the target was produced. */
static bool mob_set_path_direction(struct entity* e, int target_x, int target_y,
                                   int target_z, vec2 direction,
                                   bool* nav_active) {
    if(nav_active)
        *nav_active = false;

    /* The grid (BFS) pathfind is expensive (thousands of states, each with
     * world lookups). When the last pathfind used it, reuse the cached result
     * for a few ticks instead of re-running it every tick -- otherwise 20+ mobs
     * all in BFS mode collapse the server tick rate. The navmesh steer is cheap
     * and is never throttled. */
    if(e->data.monster.path_cache_mode >= 2 && e->data.monster.path_timer > 0) {
        e->data.monster.path_timer--;
        if(nav_active)
            *nav_active = false;
        return e->data.monster.path_cache_mode == 2;
    }

    int mode; /* 1 = navmesh, 2 = BFS fallback, 0 = no path */
    bool found;
    if(e->data.monster.bfs_lock > 0) {
        /* Committed to the BFS for a few ticks: the navmesh just failed to
         * reach the player (e.g. elevated), so don't let a transient low
         * navmesh path override the climbing route and cause circling. */
        e->data.monster.bfs_lock--;
        found = mob_set_grid_path_direction(e, target_x, target_y, target_z,
                                            direction);
        mode = found ? 2 : 0;
    } else if(mob_update_navigation(e, target_x, target_y, target_z,
                                    direction)) {
        if(nav_active)
            *nav_active = true;
        mode = 1;
        found = true;
    } else {
        found = mob_set_grid_path_direction(e, target_x, target_y, target_z,
                                            direction);
        mode = found ? 2 : 0;
        if(found)
            e->data.monster.bfs_lock = 15; /* stick with the BFS route */
    }

    /* Cache the outcome. Only the BFS modes (2 = found, 3 = no path) are
     * throttled; the navmesh (1) is cheap and recomputes every tick. */
    e->data.monster.path_cache_mode = (mode == 1) ? 1 : (found ? 2 : 3);
    if(e->data.monster.path_cache_mode >= 2)
        e->data.monster.path_timer = 3;

#ifdef FIND_MOB_PATH_DEBUG
    /* TEMP: unconditional capped trace of which mode steers and whether a path
     * was produced -- shows if the BFS fallback runs and climbs. */
    {
        static long mode_prints = 0;
        if(mode_prints < 120) {
            fprintf(stderr, "[MODE] %s found=%d pos=(%.1f,%.1f,%.1f) "
                    "target=(%d,%d,%d) dir=(%.2f,%.2f)\n",
                    mode == 1 ? "navmesh" : (mode == 2 ? "bfs" : "none"),
                    (int)found, e->pos[0], e->pos[1], e->pos[2], target_x,
                    target_y, target_z, direction[0], direction[1]);
            fflush(stderr);
            mode_prints++;
        }
    }
#endif

    /* Throttled so the log only reports when the mode changes. The tile build
     * counters expose a rebuild storm: if failures climb every tick, tiles are
     * not persisting. */
#ifdef FIND_MOB_PATH_DEBUG
    if(gstate.settings.debug && e->data.monster.debug_path_mode != mode) {
        long nav_attempts = 0, nav_failures = 0;
        mob_nav_debug_counts(&nav_attempts, &nav_failures);
        printf("[CREEPER_NAV] id=%u mode=%s pos=(%.2f,%.2f,%.2f) "
               "target=(%d,%d,%d) dir=(%.2f,%.2f) tiles(build=%ld,fail=%ld)\n",
               (unsigned)e->id,
               mode == 1 ? "navmesh" : (mode == 2 ? "bfs" : "none"),
               e->pos[0], e->pos[1], e->pos[2], target_x, target_y, target_z,
               direction[0], direction[1], nav_attempts, nav_failures);
        e->data.monster.debug_path_mode = mode;
    }
#endif

    /* Debug JSON export: write the path the creeper ACTUALLY follows -- navmesh
     * waypoints when the navmesh steers, otherwise the BFS route. */
#ifdef FIND_MOB_PATH_DEBUG
    {
        static long json_index = 0;
        if(json_index < 2000) {
            float path[128 * 3];
            int pcount = 0;
            if(mode == 1 && e->data.monster.nav)
                pcount = mob_nav_copy_path((mob_nav*)e->data.monster.nav, path,
                                           128);
            else if(mode == 2) {
                pcount = g_bfs_path_len < 128 ? g_bfs_path_len : 128;
                for(int i = 0; i < pcount * 3; i++)
                    path[i] = g_bfs_path[i];
            }
            int ymin = (int)floorf(e->pos[1]) - 20;
            int ymax = (int)floorf(e->pos[1]) + 20;
            if(ymin < 0)
                ymin = 0;
            if(ymax > WORLD_HEIGHT)
                ymax = WORLD_HEIGHT;
            mob_nav_write_json(e, json_index, e->pos[0], e->pos[1], e->pos[2],
                               (float)target_x + 0.5f, (float)target_y + 1.0f,
                               (float)target_z + 0.5f, path, pcount, ymin, ymax);
            json_index++;
        }
    }
#endif
    return found;
}

void entity_monster_free_nav(struct entity* e) {
    if(e && e->type == ENTITY_MONSTER && e->data.monster.nav) {
        mob_nav_destroy((mob_nav*)e->data.monster.nav);
        e->data.monster.nav = NULL;
    }
}

void entities_notify_block_change(struct server_local* s, int x, int y, int z) {
    if(!s || !mob_nav_any_active())
        return;
    dict_entity_it_t it;
    dict_entity_it(it, s->entities);
    while(!dict_entity_end_p(it)) {
        struct entity* e = dict_entity_ref(it)->value;
        if(e->type == ENTITY_MONSTER && e->data.monster.nav)
            mob_nav_notify_block_change((mob_nav*)e->data.monster.nav, x, y, z);
        dict_entity_next(it);
    }
}

static void mob_choose_wander_direction(struct entity* e,
                                        struct server_local* s,
                                        int min_ticks, int max_extra_ticks) {
    float ang = rand_gen_flt(&s->rand_src) * 2.0f * M_PI;
    e->data.monster.direction[0] = cosf(ang);
    e->data.monster.direction[1] = sinf(ang);
    e->data.monster.direction_time = min_ticks
        + rand_gen_int(&s->rand_src, max_extra_ticks);
}

static bool mob_has_ground_ahead(struct entity* e, float dx, float dz) {
    int x = (int)floorf(e->pos[0] + dx * 0.75f);
    int z = (int)floorf(e->pos[2] + dz * 0.75f);
    int y = (int)floorf(e->pos[1] - 0.05f);

    /* A mob may enter a shallow hole.  Falling up to three blocks is still
     * preferable to taking a long detour around it, and normal gravity will
     * bring the mob down into the hole. */
    for(int depth = 0; depth <= 3; depth++) {
        struct block_data ground;
        if(!entity_get_block(e, x, y - depth, z, &ground))
            return false;
        if(blocks[ground.type] && !blocks[ground.type]->can_see_through)
            return true;
    }
    return false;
}

/* Return the depth of the first solid block below a position.  A depth of
 * zero is the normal floor, while -1 means that there is no usable floor in
 * the range a mob can safely enter. */
static int mob_ground_depth(struct entity* e, float x, float z, int y) {
    int bx = (int)floorf(x);
    int bz = (int)floorf(z);
    for(int depth = 0; depth <= 3; depth++) {
        struct block_data ground;
        if(!entity_get_block(e, bx, y - depth, bz, &ground))
            return -1;
        if(blocks[ground.type] && !blocks[ground.type]->can_see_through)
            return depth;
    }
    return -1;
}

/* A drop is useful only when the mob can reach a rising exit.  This rejects
 * the common case of a three-block-deep pit with vertical walls, while still
 * allowing a shallow pit or a stepped/ramped hole to be crossed. */
static bool mob_can_exit_drop(struct entity* e, float dx, float dz) {
    int y = (int)floorf(e->pos[1] - 0.05f);
    int first_depth = mob_ground_depth(e,
        e->pos[0] + dx * 0.75f, e->pos[2] + dz * 0.75f, y);
    if(first_depth <= 0)
        return true;
    if(first_depth < 0)
        return false;

    int previous_depth = first_depth;
    for(int step = 2; step <= 5; step++) {
        int depth = mob_ground_depth(e,
            e->pos[0] + dx * (float)step,
            e->pos[2] + dz * (float)step, y);
        if(depth < 0 || depth > previous_depth + 1)
            return false;
        if(depth == 0)
            return true;
        previous_depth = depth;
    }
    return false;
}

/* Keep mobs from walking off cliffs or into deep holes.  Shallow holes are
 * accepted by mob_has_ground_ahead(), while the side candidates are tried
 * before stopping when the drop is too deep. */
static void mob_avoid_drop(struct entity* e) {
    float dx = e->data.monster.direction[0];
    float dz = e->data.monster.direction[1];
    if(e->data.monster.detour_time > 0)
        e->data.monster.detour_time--;

    bool current_path_safe = mob_has_ground_ahead(e, dx, dz)
        && mob_can_exit_drop(e, dx, dz);
    if(sqrtf(dx * dx + dz * dz) < 0.001f || current_path_safe)
        return;

    const float candidates[3][2] = {
        {-dz, dx}, {dz, -dx}, {-dx, -dz}
    };
    for(size_t i = 0; i < 3; i++) {
        if(mob_has_ground_ahead(e, candidates[i][0], candidates[i][1])
           && mob_can_exit_drop(e, candidates[i][0], candidates[i][1])) {
            e->data.monster.direction[0] = candidates[i][0];
            e->data.monster.direction[1] = candidates[i][1];
            /* Keep this detour long enough to get out of a corridor.  The
             * target direction must not immediately steer the mob back into
             * the same dead end on the next server tick. */
            e->data.monster.detour_time = 20;
            return;
        }
    }

    e->data.monster.direction[0] = 0.0f;
    e->data.monster.direction[1] = 0.0f;
    e->data.monster.detour_time = 10;
    e->vel[0] = 0.0f;
    e->vel[2] = 0.0f;
}


//-------------------------
// CREEPER
//-------------------------
// Local bounding box for collision (centered, half-height offset)
void make_creeper_bbox(struct AABB* out) {
    const float sx = 0.6f, sy = 1.7f, sz = 0.6f;
    aabb_setsize_centered_offset(out, sx, sy, sz, 0.0f, sy * 0.5f, 0.0f);
}

static struct AABB make_creeper_bbox_ret(void) {
    struct AABB b;
    make_creeper_bbox(&b);
    return b;
}

// Client-side tick: interpolate and smooth orientation
static bool client_tick_creeper(struct entity* e) {
    assert(e);

    glm_vec3_copy(e->pos, e->pos_old);
    glm_vec3_lerp(e->pos, e->network_pos, 0.3f, e->pos);

    vec3 delta;
    entity_get_delta(e, delta);
    float speed = sqrtf(delta[0]*delta[0] + delta[2]*delta[2]);
    if (speed > 0.001f) {
        e->data.monster.head_yaw = atan2f(delta[0], delta[2]);
    }
    entity_blend_body_to_head(&e->data.monster.body_yaw,
                              e->data.monster.head_yaw,
                              YAW_SMOOTH_FACTOR);

    e->orient[0] = 0.0f;
    e->orient[1] = e->data.monster.body_yaw + e->data.monster.head_yaw;

    entity_tick_animation(e, speed, 60);
    if (e->health <= 0 && e->data.monster.fuse > 0) {
        vec3 interp;
        glm_vec3_lerp(e->pos_old, e->pos, 1.0f, interp);
        particle_spawn_dim = gstate.player_dims[gstate_active_player()];
        particle_generate_explosion_smoke(interp, 1.0f);
    }

    return false;
}

// Server-side creeper logic
static bool server_tick_creeper(struct entity* e, struct server_local* s) {
    assert(e && s);

    glm_vec3_copy(e->pos,    e->pos_old);
    glm_vec2_copy(e->orient, e->orient_old);

    entity_damp_velocity(e, 0.005f);

    vec3 center = { e->pos[0], e->pos[1], e->pos[2] };

    /* Killing a creeper must never turn it into an explosion.  Drop its
     * normal loot immediately, including when it was already primed. */
    if(e->health <= 0) {
        e->data.monster.fuse = -1;
        e->ai_state = AI_IDLE;
        e->delay_destroy = 0;
        server_local_spawn_item(center, &e->drop_item, true, s);
        return false;
    }

    int target_pid = -1;
    float target_dx = 0.0f, target_dz = 0.0f;
    float target_distance_sq = 0.0f;
    bool has_target = mob_find_nearest_player(
        e, s, MOB_TARGET_RANGE, &target_pid, &target_dx, &target_dz,
        &target_distance_sq);
    (void)target_pid;
    (void)target_distance_sq;

    /* Set while a real navmesh path is being followed: the navmesh already
     * routes around walls and drops, so the reactive cliff-avoidance and the
     * collision side-step must stand down to avoid fighting it. */
    bool nav_active = false;
    /* True while following ANY real route (navmesh or BFS). The BFS already
     * routes around holes/cliffs, so the reactive cliff-avoidance must only run
     * in the dumb straight-line mode -- otherwise it overrides a correct BFS
     * path and drives the creeper into walls/holes. */
    bool path_found = false;

    if(e->data.monster.jump_hold > 0)
        e->data.monster.jump_hold--;

    if (e->data.monster.fuse >= 0) {
#if defined(CREEPER_DEBUG) && defined(FIND_MOB_PATH_DEBUG)
        CAVEX_DEBUG_PRINTF("[DEBUG] Creeper %u fuse remaining: %d ticks\n",
               (unsigned)e->id,
               e->data.monster.fuse);
#endif
        /* The fuse only completes while the player remains close. */
        if(!has_target
           || target_distance_sq > CREEPER_IGNITE_RANGE * CREEPER_IGNITE_RANGE) {
            e->data.monster.fuse = -1;
            e->ai_state = AI_CHASE;
        } else if(e->data.monster.detour_time <= 0) {
            if(!mob_set_path_direction(e,
                    (int)floorf(e->pos[0] + target_dx),
                    MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                    (int)floorf(e->pos[2] + target_dz),
                    e->data.monster.direction, &nav_active))
                mob_set_direction(target_dx, target_dz,
                                  e->data.monster.direction);
        }

        particle_generate_explosion_smoke(center, 2.0f);

        if (e->data.monster.fuse >= 0
            && --e->data.monster.fuse == 0) {
            // explosie
            particle_generate_explosion_flash(center, 3.0f);
            particle_generate_explosion_smoke(center, 3.0f);
            sound_play_at((enum pcm_sound)(pcm_explode1
                                           + (rand_gen(&s->rand_src) % 4)),
                          center[0], center[1], center[2]);
            server_world_explode(s, center, 3.0f);
            e->delay_destroy = 0;
        }
    }

    if(e->delay_destroy != 0) {
        if(has_target) {
            /* Only (re)plan while grounded. Replanning mid-jump from the higher
             * mid-air position flips the steering direction, so the creeper
             * falls back off the step instead of landing on it -- it ends up
             * hopping up and wandering away. Keep the direction until it lands. */
            if(e->data.monster.detour_time <= 0
               && e->data.monster.jump_hold <= 0) {
                path_found = mob_set_path_direction(e,
                        (int)floorf(e->pos[0] + target_dx),
                        MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                        (int)floorf(e->pos[2] + target_dz),
                        e->data.monster.direction, &nav_active);
                if(!path_found)
                    mob_set_direction(target_dx, target_dz,
                                      e->data.monster.direction);
            }
            if(target_distance_sq <= CREEPER_IGNITE_RANGE * CREEPER_IGNITE_RANGE
               && e->data.monster.fuse < 0) {
                e->data.monster.fuse = CREEPER_FUSE_TICKS;
                e->ai_state = AI_FUSE;
                sound_play_at(pcm_fuse, e->pos[0], e->pos[1], e->pos[2]);
            } else if(e->data.monster.fuse < 0) {
                e->ai_state = AI_CHASE;
            }
        } else if(--e->data.monster.direction_time <= 0) {
            mob_choose_wander_direction(e, s, 30, 30);
            e->ai_state = AI_IDLE;
        }
    }

    if(!path_found && e->data.monster.jump_hold <= 0)
        mob_avoid_drop(e);
    entity_move_in_direction(e, CREEPER_ACCEL, e->data.monster.direction);
    entity_clamp_speed(e, CREEPER_MAX_SPEED);

    entity_try_unstuck(e, make_creeper_bbox);

    bool collision_xz = false;
    entity_try_move_axis(e, 1, make_creeper_bbox_ret, &collision_xz, &e->on_ground);

    bool jumped = false;
    if(e->on_ground)
        jumped = entity_try_auto_jump(e, 4.0f, 0.01f);

    bool recentered = false;
    float pre_hvel = sqrtf(e->vel[0] * e->vel[0] + e->vel[2] * e->vel[2]);
    entity_try_move_axis(e, 0, make_creeper_bbox_ret, &collision_xz, &e->on_ground);
    entity_try_move_axis(e, 2, make_creeper_bbox_ret, &collision_xz, &e->on_ground);

    const int ahead_x = (int)floorf(e->pos[0]
                                    + e->data.monster.direction[0] * 0.75f);
    const int ahead_z = (int)floorf(e->pos[2]
                                    + e->data.monster.direction[1] * 0.75f);
    const int feet_y = (int)floorf(e->pos[1]);

    /* Climbable one-block step: force the jump, independent of the steering
     * mode. The shared auto-jump tests can_see_through while navigation and the
     * real collision use the full bounding box, so step blocks built from
     * slabs/stairs/odd blocks desync and the creeper presses into the step
     * forever. Must run before the wall handling so a staircase is climbed, not
     * recentered (navmesh) or sidestepped (BFS) away from. */
    bool step_jump = false;
    if(collision_xz && has_target && e->on_ground
       && !mob_block_is_solid(e, ahead_x, feet_y + 1, ahead_z)
       && mob_block_is_solid(e, ahead_x, feet_y, ahead_z)) {
        e->vel[1] = 4.0f;
        step_jump = true;
        jumped = true;
        /* Hold the current direction through the jump so the mob lands on the
         * step instead of being redirected mid-air (which made it fall back
         * and wander). */
        e->data.monster.jump_hold = 6;
    }

    /* Followed a navmesh path but clipped the corner of a wall too tall to
     * climb (the path keeps only ~0.25 block clearance, less than the creeper's
     * ~0.3 radius). Pull the creeper onto its block center (clear of the walls)
     * for a few ticks, then let the navmesh resume. */
    if(collision_xz && has_target && nav_active && !step_jump
       && mob_block_is_solid(e, ahead_x, feet_y + 1, ahead_z)) {
        const float bx = floorf(e->pos[0]) + 0.5f;
        const float bz = floorf(e->pos[2]) + 0.5f;
        const float rdx = bx - e->pos[0];
        const float rdz = bz - e->pos[2];
        if(rdx * rdx + rdz * rdz > 0.0001f) {
            mob_set_direction(rdx, rdz, e->data.monster.direction);
            e->data.monster.detour_time = 3;
            recentered = true;
        }
    }

    /* Only relevant for the weaker (BFS / straight-line) steering modes: the
     * game's AABB collision can reject a diagonal step the voxel path chose.
     * A followed navmesh path already accounts for the real geometry, so it is
     * left untouched and simply re-steers on the next tick. */
    if(collision_xz && has_target && !path_found && !step_jump
       && e->data.monster.detour_time <= 0) {
        const float blocked_dx = e->data.monster.direction[0];
        const float blocked_dz = e->data.monster.direction[1];
        const float side_candidates[2][2] = {
            {-blocked_dz, blocked_dx},
            {blocked_dz, -blocked_dx}
        };
        bool sidestepped = false;
        for(size_t side = 0; side < 2; side++) {
            if(mob_has_ground_ahead(e, side_candidates[side][0],
                                    side_candidates[side][1])
               && mob_can_exit_drop(e, side_candidates[side][0],
                                    side_candidates[side][1])) {
                e->data.monster.direction[0] = side_candidates[side][0];
                e->data.monster.direction[1] = side_candidates[side][1];
                sidestepped = true;
                break;
            }
        }
        if(!sidestepped)
            sidestepped = mob_set_grid_path_direction(
                e, (int)floorf(e->pos[0] + target_dx),
                MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                (int)floorf(e->pos[2] + target_dz),
                e->data.monster.direction);
        if(sidestepped) {
            e->data.monster.detour_time = 6;
#ifdef FIND_MOB_PATH_DEBUG
            if(gstate.settings.debug && e->data.monster.debug_path_mode != 2) {
                printf("[CREEPER_NAV] id=%u collision=side_step pos=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f)\n",
                       (unsigned)e->id, e->pos[0], e->pos[1], e->pos[2],
                       e->data.monster.direction[0],
                       e->data.monster.direction[1]);
                e->data.monster.debug_path_mode = 2;
            }
#endif
        }
    }

    /* TEMP per-tick trace to diagnose the step-climb / circling: shows whether
     * auto-jump fired, whether the recenter fired, collision + velocities. */
#ifdef FIND_MOB_PATH_DEBUG
    {
        static long ctick = 0;
        if(ctick < 300 && has_target) {
            fprintf(stderr,
                "[CTICK] pos=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f) hvel_pre=%.3f "
                "vel=(%.2f,%.2f,%.2f) ground=%d coll=%d nav=%d jump=%d "
                "recenter=%d detour=%d\n",
                e->pos[0], e->pos[1], e->pos[2], e->data.monster.direction[0],
                e->data.monster.direction[1], pre_hvel, e->vel[0], e->vel[1],
                e->vel[2], (int)e->on_ground, (int)collision_xz,
                (int)nav_active, (int)jumped, (int)recentered,
                e->data.monster.detour_time);
            fflush(stderr);
            ctick++;
        }
    }
#endif

    mob_apply_gravity_or_swim(e, 1.7f);

    clin_rpc_send(&(struct client_rpc){
        .type = CRPC_ENTITY_MOVE,
        .payload.entity_move.entity_id = e->id,
        .payload.entity_move.pos = {e->pos[0], e->pos[1], e->pos[2]}
    });

    return false;
}

/* Basic hostile navigation for zombies.  This is deliberately voxel-aware:
 * the normal entity collision code handles walls and floors, while the
 * auto-jump step lets the mob climb one-block obstacles instead of endlessly
 * pushing into them. */
static bool server_tick_zombie(struct entity* e, struct server_local* s) {
    glm_vec3_copy(e->pos, e->pos_old);
    glm_vec2_copy(e->orient, e->orient_old);
    entity_damp_velocity(e, 0.005f);

    vec3 center = {e->pos[0], e->pos[1], e->pos[2]};
    if(e->health <= 0) {
        e->delay_destroy = 0;
        server_local_spawn_item(center, &e->drop_item, true, s);
        return false;
    }

    int target_pid = -1;
    float target_dx = 0.0f, target_dz = 0.0f, target_distance_sq = 0.0f;
    bool has_target = mob_find_nearest_player(
        e, s, MOB_TARGET_RANGE, &target_pid, &target_dx, &target_dz,
        &target_distance_sq);
    (void)target_pid;
    (void)target_distance_sq;
    bool nav_active = false;
    if(has_target && e->data.monster.detour_time <= 0) {
        if(!mob_set_path_direction(e,
                (int)floorf(e->pos[0] + target_dx),
                MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                (int)floorf(e->pos[2] + target_dz),
                e->data.monster.direction, &nav_active))
            mob_set_direction(target_dx, target_dz,
                              e->data.monster.direction);
        e->ai_state = AI_CHASE;
    } else if(--e->data.monster.direction_time <= 0) {
        mob_choose_wander_direction(e, s, 30, 30);
        e->ai_state = AI_IDLE;
    }

    if(!nav_active)
        mob_avoid_drop(e);
    entity_move_in_direction(e, 0.18f, e->data.monster.direction);
    entity_clamp_speed(e, 0.20f);
    entity_try_unstuck(e, make_creeper_bbox);

    bool collision_xz = false;
    entity_try_move_axis(e, 1, make_creeper_bbox_ret, &collision_xz,
                         &e->on_ground);
    if(e->on_ground)
        entity_try_auto_jump(e, 4.0f, 0.01f);

    entity_try_move_axis(e, 0, make_creeper_bbox_ret, &collision_xz,
                         &e->on_ground);
    entity_try_move_axis(e, 2, make_creeper_bbox_ret, &collision_xz,
                         &e->on_ground);
    entity_apply_gravity(e, GRAVITY);
    entity_apply_friction(e, e->on_ground ? 0.6f : 1.0f);

    clin_rpc_send(&(struct client_rpc){
        .type = CRPC_ENTITY_MOVE,
        .payload.entity_move.entity_id = e->id,
        .payload.entity_move.pos = {e->pos[0], e->pos[1], e->pos[2]}
    });
    return false;
}

//-------------------------
// PIG
//-------------------------
static bool client_tick_pig(struct entity* e) {
    assert(e);

    glm_vec3_copy(e->pos, e->pos_old);
    glm_vec3_lerp(e->pos, e->network_pos, 0.3f, e->pos);

    vec3 delta;
    entity_get_delta(e, delta);
    float speed = sqrtf(delta[0]*delta[0] + delta[2]*delta[2]);
    if (speed > 0.001f) {
        e->data.monster.head_yaw = atan2f(delta[0], delta[2]);
    }
    entity_blend_body_to_head(&e->data.monster.body_yaw,
                              e->data.monster.head_yaw,
                              YAW_SMOOTH_FACTOR);

    e->orient[0] = 0.0f;
    e->orient[1] = e->data.monster.body_yaw + e->data.monster.head_yaw;

    entity_tick_animation(e, speed, 60);
    
    return false;
}

// Pig bounding box (centered, slightly shorter than creeper)
void make_pig_bbox(struct AABB* out) {
    const float sx = 0.6f, sy = 1.2f, sz = 0.6f;
    aabb_setsize_centered_offset(out, sx, sy, sz, 0.0f, sy * 0.5f, 0.0f);
}

static struct AABB make_pig_bbox_ret(void) {
    struct AABB b;
    make_pig_bbox(&b);
    return b;
}

static size_t getBoundingBox_pig(const struct entity* e, struct AABB* out) {
    assert(e && out);
    make_pig_bbox(out);
    aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
    return 1;
}

// Server-side pig logic
static bool server_tick_pig(struct entity* e, struct server_local* s) {
    assert(e && s);

    glm_vec3_copy(e->pos,    e->pos_old);
    glm_vec2_copy(e->orient, e->orient_old);

    entity_damp_velocity(e, 0.005f);

    vec3 center = { e->pos[0], e->pos[1], e->pos[2] };

    // dead: pigs don't have a fuse, remove immediately when health <= 0
    if (e->health <= 0) {
        e->delay_destroy = 0;
        server_local_spawn_item(center,
                    &e->drop_item,
                    true,
                    s
        );
        return false;
    }

    int target_pid = -1;
    float target_dx = 0.0f, target_dz = 0.0f;
    if(e->data.monster.flee_time > 0) {
        e->data.monster.flee_time--;
        target_pid = e->data.monster.flee_player_id;
        if(target_pid >= 0 && target_pid < MAX_SERVER_PLAYERS) {
            const struct server_player* player = &s->players[target_pid];
            if(player->has_pos && player->finished_loading && !player->creative
               && (void*)PWORLD(s, target_pid) == e->world) {
                mob_set_direction((float)e->pos[0] - (float)player->x,
                                  (float)e->pos[2] - (float)player->z,
                                  e->data.monster.direction);
            }
        }
        e->ai_state = AI_FLEE;
    } else if(mob_find_nearest_wheat_player(e, s, MOB_TARGET_RANGE,
                                             &target_pid, &target_dx,
                                             &target_dz)) {
        if(e->data.monster.detour_time <= 0
           && !mob_set_path_direction(e,
                (int)floorf(e->pos[0] + target_dx),
                MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                (int)floorf(e->pos[2] + target_dz),
                e->data.monster.direction, NULL))
            mob_set_direction(target_dx, target_dz,
                              e->data.monster.direction);
        e->ai_state = AI_CHASE;
    } else if(--e->data.monster.direction_time <= 0) {
        mob_choose_wander_direction(e, s, 30, 30);
        e->ai_state = AI_IDLE;
    }

    mob_avoid_drop(e);
    entity_move_in_direction(e, PIG_ACCEL, e->data.monster.direction);
    entity_clamp_speed(e, PIG_MAX_SPEED);

    entity_try_unstuck(e, make_pig_bbox);

    bool collision_xz = false;
    entity_try_move_axis(e, 1, make_pig_bbox_ret, &collision_xz, &e->on_ground);

    if(e->on_ground)
        entity_try_auto_jump(e, 4.0f, 0.01f);

    entity_try_move_axis(e, 0, make_pig_bbox_ret, &collision_xz, &e->on_ground);
    entity_try_move_axis(e, 2, make_pig_bbox_ret, &collision_xz, &e->on_ground);

    mob_apply_gravity_or_swim(e, 1.2f);

    clin_rpc_send(&(struct client_rpc){
        .type = CRPC_ENTITY_MOVE,
        .payload.entity_move.entity_id = e->id,
        .payload.entity_move.pos = {e->pos[0], e->pos[1], e->pos[2]}
    });

    return false;
}

//-------------------------
// SHEEP
//-------------------------
static bool onRightClick_sheep(struct entity* e, struct item_data *held) {
    if (!e || !held)
        return false;

    if (held->id == 359 && !e->data.monster.shared) {
        e->data.monster.shared = 
                e->data.monster.shared_now = true;
        return true;
    }
    return false;
}

static bool client_tick_sheep(struct entity* e) {
    assert(e);

    glm_vec3_copy(e->pos, e->pos_old);
    glm_vec3_lerp(e->pos, e->network_pos, 0.3f, e->pos);

    vec3 delta;
    entity_get_delta(e, delta);
    float speed = sqrtf(delta[0]*delta[0] + delta[2]*delta[2]);
    if (speed > 0.001f) {
        e->data.monster.head_yaw = atan2f(delta[0], delta[2]);
    }
    entity_blend_body_to_head(&e->data.monster.body_yaw,
                              e->data.monster.head_yaw,
                              YAW_SMOOTH_FACTOR);

    e->orient[0] = 0.0f;
    e->orient[1] = e->data.monster.body_yaw + e->data.monster.head_yaw;

    entity_tick_animation(e, speed, 60);
    
    return false;
}

// Sheep bounding box (centered, slightly shorter than creeper)
void make_sheep_bbox(struct AABB* out) {
    const float sx = 0.6f, sy = 1.2f, sz = 0.6f;
    aabb_setsize_centered_offset(out, sx, sy, sz, 0.0f, sy * 0.5f, 0.0f);
}

static struct AABB make_sheep_bbox_ret(void) {
    struct AABB b;
    make_sheep_bbox(&b);
    return b;
}

static size_t getBoundingBox_sheep(const struct entity* e, struct AABB* out) {
    assert(e && out);
    make_sheep_bbox(out);
    aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
    return 1;
}

// Server-side sheep logic
static bool server_tick_sheep(struct entity* e, struct server_local* s) {
    assert(e && s);

    glm_vec3_copy(e->pos,    e->pos_old);
    glm_vec2_copy(e->orient, e->orient_old);

    entity_damp_velocity(e, 0.005f);

    vec3 center = { e->pos[0], e->pos[1], e->pos[2] };
    
    // dead
    if (e->health <= 0) {
        e->delay_destroy = 0;
        server_local_spawn_item(center,
                    &e->drop_item,
                    true,
                    s
        );
        return false;
    }

    if (e->data.monster.shared_now) {
        struct item_data it = { .id = 35, .durability = 0, .count = get_random(1, 3)};
        server_local_spawn_item(center, &it, true, s);
        e->data.monster.shared_now = false;
    }

    int target_pid = -1;
    float target_dx = 0.0f, target_dz = 0.0f;
    if(e->data.monster.flee_time > 0) {
        e->data.monster.flee_time--;
        target_pid = e->data.monster.flee_player_id;
        if(target_pid >= 0 && target_pid < MAX_SERVER_PLAYERS) {
            const struct server_player* player = &s->players[target_pid];
            if(player->has_pos && player->finished_loading && !player->creative
               && (void*)PWORLD(s, target_pid) == e->world) {
                mob_set_direction((float)e->pos[0] - (float)player->x,
                                  (float)e->pos[2] - (float)player->z,
                                  e->data.monster.direction);
            }
        }
        e->ai_state = AI_FLEE;
    } else if(mob_find_nearest_wheat_player(e, s, MOB_TARGET_RANGE,
                                             &target_pid, &target_dx,
                                             &target_dz)) {
        if(e->data.monster.detour_time <= 0
           && !mob_set_path_direction(e,
                (int)floorf(e->pos[0] + target_dx),
                MOB_TARGET_FLOOR_Y(s->players[target_pid].y),
                (int)floorf(e->pos[2] + target_dz),
                e->data.monster.direction, NULL))
            mob_set_direction(target_dx, target_dz,
                              e->data.monster.direction);
        e->ai_state = AI_CHASE;
    } else if(--e->data.monster.direction_time <= 0) {
        mob_choose_wander_direction(e, s, 30, 30);
        e->ai_state = AI_IDLE;
    }

    mob_avoid_drop(e);
    entity_move_in_direction(e, SHEEP_ACCEL, e->data.monster.direction);
    entity_clamp_speed(e, SHEEP_MAX_SPEED);

    entity_try_unstuck(e, make_sheep_bbox);

    bool collision_xz = false;
    entity_try_move_axis(e, 1, make_sheep_bbox_ret, &collision_xz, &e->on_ground);

    if(e->on_ground)
        entity_try_auto_jump(e, 4.0f, 0.01f);

    entity_try_move_axis(e, 0, make_sheep_bbox_ret, &collision_xz, &e->on_ground);
    entity_try_move_axis(e, 2, make_sheep_bbox_ret, &collision_xz, &e->on_ground);

    mob_apply_gravity_or_swim(e, 1.2f);

    clin_rpc_send(&(struct client_rpc){
        .type = CRPC_ENTITY_MOVE,
        .payload.entity_move.entity_id = e->id,
        .payload.entity_move.pos = {e->pos[0], e->pos[1], e->pos[2]}
    });

    return false;
}


void server_explode(struct server_local *s, vec3 center, float radius) {
    int r = ceilf(radius);
    for(int dx = -r; dx <= r; dx++) {
      for(int dy = -r; dy <= r; dy++) {
        for(int dz = -r; dz <= r; dz++) {
          float dist = sqrtf(dx*dx + dy*dy + dz*dz);
          if (dist > radius) continue;

          int bx = (int)floorf(center[0]) + dx;
          int by = (int)floorf(center[1]) + dy;
          int bz = (int)floorf(center[2]) + dz;

          // *** BOUNDARY CHECK ***
          if (by < 0 || by >= WORLD_HEIGHT) continue;
          // eventueel ook x/z check binnen chunk-limieten,
          // maar server_world_get_block doet dat vaak al intern.

          if (server_world_get_block(AWORLD(s), bx, by, bz, NULL)) {
            server_world_set_block(s, bx, by, bz,
              (struct block_data){ .type = BLOCK_AIR, .metadata = 0 });
          }
        }
      }
    }
}

static bool entity_server_tick(struct entity* e, struct server_local* s) {
    switch (e->data.monster.id) {
        case MONSTER_CREEPER : return server_tick_creeper(e, s);
        case MONSTER_ZOMBIE  : return server_tick_zombie(e, s);
        case ANIMAIL_SHEEP   : return server_tick_sheep(e, s);
        case ANIMAIL_PIG     : return server_tick_pig(e, s);
        default              : return false;
    }
}

static bool entity_client_tick(struct entity* e) {
    if (e->delay_destroy == 0) {
        return true;
    }

    if (e->type == ENTITY_MONSTER) {
        switch (e->data.monster.id) {
            case MONSTER_CREEPER : return client_tick_creeper(e);
            case MONSTER_ZOMBIE  : return client_tick_creeper(e);
            case ANIMAIL_PIG     : return client_tick_pig(e);
            case ANIMAIL_SHEEP   : return client_tick_sheep(e);
            // todo: other monsters
          // case MONSTER_ZOMBIE: return client_tick_zombie(e);
            default:
                return false;
        }
    }
    return false;
}

static void entity_render(struct entity* e, mat4 view, float td) {
    assert(e);

    vec3 p;
    glm_vec3_lerp(e->pos_old, e->pos, td, p);

    vec3 delta;
    glm_vec3_sub(e->pos, e->pos_old, delta);
    float bodyYaw = e->data.monster.body_yaw;
    float headYaw = bodyYaw + e->data.monster.head_yaw;

    // update light
    struct block_data blk;
    entity_get_block(e, floorf(p[0]), floorf(p[1]), floorf(p[2]), &blk);
    render_entity_update_light((blk.torch_light << 4) | blk.sky_light);

    mat4 model, mv;
    glm_translate_make(model, p);
    glm_mat4_mul(view, model, mv);

    switch (e->data.monster.id) {
        case MONSTER_CREEPER:
            render_entity_creeper(mv,
                                  glm_deg(headYaw),
                                  glm_deg(bodyYaw),
                                  e->data.monster.frame);
            break;
        case ANIMAIL_PIG:
            render_entity_pig(mv, glm_deg(bodyYaw), glm_deg(headYaw),
                              e->data.monster.frame);
            break;
        case ANIMAIL_SHEEP:
            render_entity_sheep(mv, glm_deg(bodyYaw), glm_deg(headYaw),
                                e->data.monster.frame,
                                e->data.monster.shared);
            break;
        default:
            render_entity_creeper(mv,
                                  glm_deg(bodyYaw),
                                  glm_deg(headYaw),
                                  e->data.monster.frame);
            break;
    }

    // shadow, doesn't seem to work currently
    struct AABB shadow_bb;
    aabb_setsize_centered(&shadow_bb, 0.25f, 0.25f, 0.25f);
    aabb_translate(&shadow_bb, p[0], p[1] - 0.04f, p[2]);
    entity_shadow(e, &shadow_bb, view);
}



// Raycast & interaction bounding box (world-translated)
static size_t getBoundingBox_creeper(const struct entity* e, struct AABB* out) {
    assert(e && out);
    make_creeper_bbox(out);
    aabb_translate(out, e->pos[0], e->pos[1], e->pos[2]);
    return 1;
}

bool onLeftClick(struct entity *e) {

    svin_rpc_send(&(struct server_rpc){
        .type = SRPC_ENTITY_ATTACK,
        .payload.entity_attack.entity_id = e->id,
    });

    return true;
}

// Factory
void entity_monster(uint32_t id,
                    struct entity* e,
                    bool server,
                    void* world,
                    int monster_id) {
    assert(e && world);
    entity_default_init(e, server, world);
    e->id = id;
    e->type = ENTITY_MONSTER;
    e->drop_item = (struct item_data){
        .id         = ITEM_GUNPOWDER,
        .durability = 0,
        .count      = 1
    };
    e->name = "Monster";
    e->data.monster.id = monster_id;
    e->data.monster.head_yaw = 0.0f;
    e->health     = 20;
    e->data.monster.body_yaw = 0.0f;
    e->data.monster.direction[0] = 0.0f;
    e->data.monster.direction[1] = 0.0f;
    e->data.monster.debug_path_mode = -99;
    e->data.monster.debug_target_x = INT_MIN;
    e->data.monster.debug_target_y = INT_MIN;
    e->data.monster.debug_target_z = INT_MIN;
    e->data.monster.detour_time = 0;
    e->data.monster.nav = NULL;
    e->data.monster.bfs_lock = 0;
    e->data.monster.jump_hold = 0;
    e->data.monster.path_timer = 0;
    e->data.monster.path_cache_mode = 0;
    e->data.monster.flee_time = 0;
    e->data.monster.flee_player_id = -1;
    e->data.monster.direction_time= 0;
    e->data.monster.frame = 0;
    e->data.monster.frame_time_left = 1;
    glm_vec3_copy(e->pos, e->network_pos);
    e->tick_server = entity_server_tick;
    e->tick_client = entity_client_tick;
    e->render      = entity_render;
    e->teleport    = entity_default_teleport;
    e->getBoundingBox = getBoundingBox_creeper;
    e->onLeftClick = onLeftClick;
    e->leftClickText = "Attack";
    e->rightClickText = NULL;
    e->delay_destroy = -1;
    // spawn slightly higher, to prevent getting stuck
    if (server) {
        e->pos[1] += 0.1f;
        e->pos_old[1] = e->pos[1];
    }

    e->ai_timer     = CREEPER_WANDER_INTERVAL;

    /* Per-monster overrides (name, drop, bbox, initial data) */
    switch (monster_id) {
        case MONSTER_CREEPER:
            /* defaults already match creeper */
            e->name = "Creeper";
            e->drop_item = (struct item_data){ .id = ITEM_GUNPOWDER, .durability = 0, .count = 1 };
            e->getBoundingBox = getBoundingBox_creeper;
            e->data.monster.fuse = -1;
            e->onRightClick = NULL;
            break;
        case ANIMAIL_PIG:
            e->name = "Pig";
            e->drop_item = (struct item_data){ .id = ITEM_PORKCHOP, .durability = 0, .count = 1 };
            e->getBoundingBox = getBoundingBox_pig;
            e->onRightClick = NULL;
            /* initialize pig-specific union fields */
            break;
        case ANIMAIL_SHEEP:
            e->name = "Sheep";
            e->drop_item = (struct item_data){ .id = BLOCK_WOOL, .durability = 0, .count = 1 };
            e->getBoundingBox = getBoundingBox_sheep;
            e->onRightClick = onRightClick_sheep;
            e->data.monster.shared = false;
            e->data.monster.shared_now = false;
            break;
        default:
            /* leave generic defaults */
            break;
    }
}
