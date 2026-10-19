#ifndef CAVEX_MOB_NAVIGATION_H
#define CAVEX_MOB_NAVIGATION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Per-mob navigator. Opaque to C; the C++ side owns the Recast/Detour
 * objects. One handle holds a small tiled Detour navmesh kept up to date
 * around the mob, plus the current path it is following.
 *
 * The navigator never dereferences the mob entity: the caller passes the mob
 * position explicitly and an opaque context pointer that is handed straight
 * back to mob_nav_world_solid(). This keeps the C++ unit free of the C entity
 * layout (m-lib/cglm) and independently testable. */
typedef struct mob_nav mob_nav;

mob_nav* mob_nav_create(void);
void mob_nav_destroy(mob_nav* nav);

/* Advance navigation for one server tick while chasing a goal in world
 * coordinates. Refreshes the tiles around the mob, (re)plans a path when
 * needed and steers along it. Writes a horizontal unit direction and returns
 * 1 when a path is being followed, or 0 when no path is available (the caller
 * then uses its own fallback steering). ctx is passed verbatim to
 * mob_nav_world_solid(). */
int mob_nav_steer(mob_nav* nav, void* ctx, float mob_x, float mob_y,
                  float mob_z, float goal_x, float goal_y, float goal_z,
                  float* dir_x, float* dir_z);

/* Report a world block edit so any tile covering it is dropped and rebuilt
 * lazily on the next steer. Cheap no-op when the block is outside the tiles
 * this navigator currently holds. */
void mob_nav_notify_block_change(mob_nav* nav, int x, int y, int z);

/* Non-zero while at least one navigator exists. Lets the block-change hook
 * skip work entirely when no mob is navigating. */
int mob_nav_any_active(void);

/* Diagnostics: cumulative tile build attempts and failures across all
 * navigators. A steadily climbing failure count means tiles are not
 * persisting (rebuild storm). */
void mob_nav_debug_counts(long* attempts, long* failures);

/* Copy the active navmesh waypoints (x,y,z triples) into out_xyz; returns the
 * number of points, or 0 when no navmesh path is active. */
int mob_nav_copy_path(mob_nav* nav, float* out_xyz, int max_points);

/* Debug: write the 3D navigation view + a given path to a JSON file (one per
 * index). Implemented in C++; called from C with the actually-followed path. */
void mob_nav_write_json(void* ctx, long index, float mob_x, float mob_y,
                        float mob_z, float goal_x, float goal_y, float goal_z,
                        const float* path_xyz, int count, int ymin, int ymax);

/* Implemented on the C side (entity_monster.c): non-zero when the world block
 * at the given coordinates is a full collision obstacle for navigation. The
 * C++ tile builder calls this to rasterize the local solid volume, keeping all
 * block knowledge in C. ctx is the pointer passed to mob_nav_steer(). */
int mob_nav_world_solid(void* ctx, int x, int y, int z);

#ifdef __cplusplus
}
#endif

#endif
