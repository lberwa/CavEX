#include "mob_navigation.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <cstdio>
#include <new>
#include <vector>
#include <sys/stat.h>

/* Optional navigation debugging. Keep the normal build completely quiet. */
#ifdef FIND_MOB_PATH_DEBUG
static long g_bc = 0;
#define BC(...) do { if(g_bc < 400) { std::fprintf(stderr, __VA_ARGS__); \
                     std::fflush(stderr); g_bc++; } } while(0)
#else
#define BC(...) do { } while(0)
#endif

#include "../recastnavigation/Recast/Include/Recast.h"
#include "../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

/* --- Tuning ---------------------------------------------------------------
 * The tile Y range covers the whole world column so a tile never needs a
 * vertical rebuild. TILE_MAX_Y must stay >= WORLD_HEIGHT (world.h). This is
 * the first knob to lower (to a window around the mob) if tile builds ever get
 * too expensive on the Wii. */
namespace {

/* xz resolution: 0.25 so a one-block-wide corridor survives the walkable-area
 * erosion below (erosion = walkableRadius * cs). Must divide TILE_SIZE evenly. */
const float CELL_SIZE = 0.25f;
const float CELL_HEIGHT = 0.5f; /* y resolution (blocks) */
const int TILE_SIZE = 16;       /* world blocks per tile edge (= CHUNK_SIZE) */
const int WORLD_TOP = 128;      /* must be >= WORLD_HEIGHT (world.h) */
const int BORDER = 3;           /* border voxels for tile portal generation */

/* Tiles cover only a vertical window around the mob, NOT the whole 128-block
 * column. Full-height tiles rasterize the entire underground + sky per tile,
 * which can blow past the per-tile polygon limit (failed addTile -> rebuilt
 * every tick -> runaway allocation). The window comfortably covers a 16-block
 * chase; it recenters (rebuilding all tiles) only when the mob drifts near an
 * edge. */
const int TILE_Y_RADIUS = 20;   /* window half-height (blocks) */
const int TILE_Y_MARGIN = 8;    /* recenter when the mob gets this close to an edge */

/* Diagnostics: counts tile build attempts and failures so a rebuild storm is
 * observable from the C side via mob_nav_debug_counts(). */
long g_tile_build_attempts = 0;
long g_tile_build_failures = 0;

const int TILE_RADIUS = 1;      /* keep (2*R+1)^2 tiles around the mob */
const int MAX_TILES = 64;
const int MAX_POLYS_PER_TILE = 4096;

const int MAX_PATH_POLYS = 128;
const int MAX_WAYPOINTS = 16;
const int MAX_OFFMESH_PER_TILE = 256;

const float WAYPOINT_REACHED = 0.6f; /* advance when this close horizontally */
const float REPLAN_GOAL_MOVE = 2.0f; /* replan when goal moves this far */
const int REPLAN_INTERVAL = 20;      /* safety replan period (ticks) */

/* Faces of a unit cube: four CCW vertices each, outward normal facing away
 * from the solid voxel. */
const float FACE_VERTS[6][12] = {
    {0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0}, /* -z */
    {1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1}, /* +z */
    {0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0}, /* -x */
    {1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1}, /* +x */
    {0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0}, /* +y (top) */
    {0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1}  /* -y (bottom) */
};
const int FACE_DX[6] = {0, 0, -1, 1, 0, 0};
const int FACE_DY[6] = {0, 0, 0, 0, 1, -1};
const int FACE_DZ[6] = {-1, 1, 0, 0, 0, 0};

int g_active_navs = 0;

inline bool arr_solid(const std::vector<unsigned char>& solid, int ex, int ez,
                      int ey, int x, int y, int z) {
    if(x < 0 || x >= ex || y < 0 || y >= ey || z < 0 || z >= ez)
        return false;
    return solid[(size_t)(y * ez + z) * ex + x] != 0;
}

/* Highest standable floor in a column: a solid block with two air cells above.
 * Returns the world y of that solid block, or INT_MIN if none. */
int column_floor(const std::vector<unsigned char>& solid, int ex, int ez,
                 int ey, int y_base, int x, int z) {
    for(int y = ey - 3; y >= 0; y--) {
        if(arr_solid(solid, ex, ez, ey, x, y, z)
           && !arr_solid(solid, ex, ez, ey, x, y + 1, z)
           && !arr_solid(solid, ex, ez, ey, x, y + 2, z))
            return y_base + y;
    }
    return INT_MIN;
}

void free_recast(rcHeightfield*& hf, rcCompactHeightfield*& chf,
                 rcContourSet*& cset, rcPolyMesh*& pmesh,
                 rcPolyMeshDetail*& dmesh) {
    rcFreeHeightField(hf);
    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);
    hf = 0;
    chf = 0;
    cset = 0;
    pmesh = 0;
    dmesh = 0;
}

} /* namespace */

struct mob_nav {
    dtNavMesh* mesh;
    dtNavMeshQuery* query;
    dtQueryFilter filter;

    float straight[3 * MAX_WAYPOINTS];
    unsigned char straight_flags[MAX_WAYPOINTS];
    dtPolyRef straight_refs[MAX_WAYPOINTS];
    int path_count;
    int wp_index;

    float planned_goal[3];
    int replan_timer;

    /* Vertical window the currently loaded tiles were built for, and the mob Y
     * it was centered on (recenter is driven by drift from this center, not the
     * window edges, so it does not misfire when the window is clamped at the
     * world top/bottom). */
    int tile_ymin;
    int tile_ymax;
    int tile_center_y;
};

/* Build (or rebuild) one tile of the mob's navmesh from the live world. */
static bool mob_nav_build_tile(mob_nav* nav, void* ctx, int tx, int tz) {
    const int tile_min_x = tx * TILE_SIZE;
    const int tile_min_z = tz * TILE_SIZE;
    const int origin_x = tile_min_x - BORDER;
    const int origin_z = tile_min_z - BORDER;
    const int ymin = nav->tile_ymin;
    const int ymax = nav->tile_ymax;
    const int ex = TILE_SIZE + 2 * BORDER;
    const int ez = TILE_SIZE + 2 * BORDER;
    const int ey = ymax - ymin;
    if(ey < 3)
        return false;

    g_tile_build_attempts++;
    BC("[BC] build_tile enter tx=%d tz=%d ymin=%d ymax=%d ey=%d\n", tx, tz,
       ymin, ymax, ey);

    /* Snapshot the solid volume once; the triangle soup and the drop detection
     * both read from it, so the world is queried a single time. */
    std::vector<unsigned char> solid((size_t)ex * ez * ey, 0);
    for(int y = 0; y < ey; y++)
        for(int z = 0; z < ez; z++)
            for(int x = 0; x < ex; x++)
                solid[(size_t)(y * ez + z) * ex + x]
                    = mob_nav_world_solid(ctx, origin_x + x, ymin + y,
                                          origin_z + z)
                    ? 1 : 0;

    std::vector<float> verts;
    std::vector<int> tris;
    for(int y = 0; y < ey; y++) {
        for(int z = 0; z < ez; z++) {
            for(int x = 0; x < ex; x++) {
                if(!arr_solid(solid, ex, ez, ey, x, y, z))
                    continue;
                const float wx = (float)(origin_x + x);
                const float wy = (float)(ymin + y);
                const float wz = (float)(origin_z + z);
                for(int f = 0; f < 6; f++) {
                    if(arr_solid(solid, ex, ez, ey, x + FACE_DX[f],
                                 y + FACE_DY[f], z + FACE_DZ[f]))
                        continue;
                    const int base = (int)verts.size() / 3;
                    for(int v = 0; v < 4; v++) {
                        verts.push_back(wx + FACE_VERTS[f][v * 3 + 0]);
                        verts.push_back(wy + FACE_VERTS[f][v * 3 + 1]);
                        verts.push_back(wz + FACE_VERTS[f][v * 3 + 2]);
                    }
                    const int t[6] = {base, base + 1, base + 2,
                                      base, base + 2, base + 3};
                    tris.insert(tris.end(), t, t + 6);
                }
            }
        }
    }
    BC("[BC] tris built tx=%d tz=%d verts=%d tris=%d\n", tx, tz,
       (int)verts.size() / 3, (int)tris.size() / 3);
    if(tris.empty())
        return false; /* nothing solid here: leave the tile empty */

    /* Down-only off-mesh links for 2-3 block drops at walkable surface edges.
     * One-block steps are handled by walkableClimb; larger drops are left out
     * so the mob stops at the edge instead of plunging. */
    std::vector<float> oc_verts;
    std::vector<float> oc_rad;
    std::vector<unsigned char> oc_dir;
    std::vector<unsigned char> oc_areas;
    std::vector<unsigned short> oc_flags;
    std::vector<unsigned int> oc_ids;
    int oc_count = 0;
    const int nb_dx[4] = {1, -1, 0, 0};
    const int nb_dz[4] = {0, 0, 1, -1};
    for(int bz = 0; bz < TILE_SIZE && oc_count < MAX_OFFMESH_PER_TILE; bz++) {
        for(int bx = 0; bx < TILE_SIZE && oc_count < MAX_OFFMESH_PER_TILE;
            bx++) {
            const int fh = column_floor(solid, ex, ez, ey, ymin, bx + BORDER,
                                        bz + BORDER);
            if(fh == INT_MIN)
                continue;
            for(int n = 0; n < 4; n++) {
                const int nbx = bx + nb_dx[n];
                const int nbz = bz + nb_dz[n];
                if(nbx < 0 || nbx >= TILE_SIZE || nbz < 0 || nbz >= TILE_SIZE)
                    continue; /* seam drops handled by the neighbour tile */
                const int nh = column_floor(solid, ex, ez, ey, ymin,
                                            nbx + BORDER, nbz + BORDER);
                if(nh == INT_MIN)
                    continue;
                const int delta = fh - nh;
                if(delta < 2 || delta > 3)
                    continue;
                oc_verts.push_back((float)(tile_min_x + bx) + 0.5f);
                oc_verts.push_back((float)(fh + 1));
                oc_verts.push_back((float)(tile_min_z + bz) + 0.5f);
                oc_verts.push_back((float)(tile_min_x + nbx) + 0.5f);
                oc_verts.push_back((float)(nh + 1));
                oc_verts.push_back((float)(tile_min_z + nbz) + 0.5f);
                oc_rad.push_back(0.6f);
                oc_dir.push_back(0); /* one-way: drop down, no climb up */
                oc_areas.push_back(1);
                oc_flags.push_back(1);
                oc_ids.push_back((unsigned int)(1000 + oc_count));
                oc_count++;
                if(oc_count >= MAX_OFFMESH_PER_TILE)
                    break;
            }
        }
    }

    rcConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.cs = CELL_SIZE;
    cfg.ch = CELL_HEIGHT;
    cfg.walkableSlopeAngle = 89.0f;
    cfg.walkableHeight = (int)ceilf(2.0f / cfg.ch); /* 2 blocks clearance */
    cfg.walkableClimb = (int)floorf(1.0f / cfg.ch); /* 1 block step up/down */
    /* Erode the walkable area away from walls so paths do not hug wall faces
     * and the ~0.6-wide creeper (radius ~0.3) stops clipping/sticking on
     * corners. erosion = walkableRadius * cs = 2 * 0.25 = 0.5 block, which fully
     * keeps paths off the walls. erosion = walkableRadius * cs = 1 * 0.25 = 0.25
     * block. The small remaining clip on corners is handled reactively (the mob
     * recenters on its block on collision); more erosion (0.4+) eroded tight
     * corridors shut and broke reachability. */
    cfg.walkableRadius = 1;
    cfg.maxEdgeLen = 8;
    cfg.maxSimplificationError = 0.05f; /* keep one-block walls intact */
    cfg.minRegionArea = 1;
    cfg.mergeRegionArea = 20;
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.tileSize = (int)(TILE_SIZE / cfg.cs);
    cfg.borderSize = cfg.walkableRadius + BORDER;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.tileSize + cfg.borderSize * 2;
    cfg.detailSampleDist = 0.0f;
    cfg.detailSampleMaxError = 0.0f;
    cfg.bmin[0] = (float)tile_min_x - cfg.borderSize * cfg.cs;
    cfg.bmin[1] = (float)ymin;
    cfg.bmin[2] = (float)tile_min_z - cfg.borderSize * cfg.cs;
    cfg.bmax[0] = (float)(tile_min_x + TILE_SIZE) + cfg.borderSize * cfg.cs;
    cfg.bmax[1] = (float)ymax;
    cfg.bmax[2] = (float)(tile_min_z + TILE_SIZE) + cfg.borderSize * cfg.cs;

    const int num_verts = (int)verts.size() / 3;
    const int num_tris = (int)tris.size() / 3;
    std::vector<unsigned char> areas((size_t)num_tris, 0);

    rcContext rc_ctx(false);
    rcHeightfield* hf = 0;
    rcCompactHeightfield* chf = 0;
    rcContourSet* cset = 0;
    rcPolyMesh* pmesh = 0;
    rcPolyMeshDetail* dmesh = 0;
    unsigned char* nav_data = 0;
    bool ok = false;

    do {
        BC("[BC]  R1 heightfield w=%d h=%d\n", cfg.width, cfg.height);
        hf = rcAllocHeightfield();
        if(!hf
           || !rcCreateHeightfield(&rc_ctx, *hf, cfg.width, cfg.height,
                                   cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
            break;
        BC("[BC]  R2 rasterize ntris=%d\n", num_tris);
        rcMarkWalkableTriangles(&rc_ctx, cfg.walkableSlopeAngle, &verts[0],
                                num_verts, &tris[0], num_tris, &areas[0]);
        if(!rcRasterizeTriangles(&rc_ctx, &verts[0], num_verts, &tris[0],
                                 &areas[0], num_tris, *hf, cfg.walkableClimb))
            break;
        rcFilterLowHangingWalkableObstacles(&rc_ctx, cfg.walkableClimb, *hf);
        rcFilterLedgeSpans(&rc_ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
        rcFilterWalkableLowHeightSpans(&rc_ctx, cfg.walkableHeight, *hf);

        BC("[BC]  R3 compact\n");
        chf = rcAllocCompactHeightfield();
        if(!chf
           || !rcBuildCompactHeightfield(&rc_ctx, cfg.walkableHeight,
                                         cfg.walkableClimb, *hf, *chf))
            break;
        if(!rcErodeWalkableArea(&rc_ctx, cfg.walkableRadius, *chf))
            break;
        BC("[BC]  R4 regions\n");
        if(!rcBuildRegionsMonotone(&rc_ctx, *chf, cfg.borderSize,
                                   cfg.minRegionArea, cfg.mergeRegionArea))
            break;

        BC("[BC]  R5 contours\n");
        cset = rcAllocContourSet();
        if(!cset
           || !rcBuildContours(&rc_ctx, *chf, cfg.maxSimplificationError,
                               cfg.maxEdgeLen, *cset))
            break;
        if(cset->nconts == 0) /* no walkable contour in this tile */
            break;
        BC("[BC]  R6 polymesh nconts=%d\n", cset->nconts);
        pmesh = rcAllocPolyMesh();
        if(!pmesh
           || !rcBuildPolyMesh(&rc_ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
            break;
        if(pmesh->npolys == 0)
            break;
        /* No Recast detail mesh: rcBuildPolyMeshDetail can loop/allocate
         * unboundedly on this voxel-derived input, and we only steer in XZ.
         * dtCreateNavMeshData builds a trivial detail mesh from the polys when
         * none is supplied. */
        BC("[BC]  R7 createNavData npolys=%d\n", pmesh->npolys);

        for(int i = 0; i < pmesh->npolys; i++) {
            pmesh->areas[i] = 1;
            pmesh->flags[i] = 1;
        }

        dtNavMeshCreateParams params;
        std::memset(&params, 0, sizeof(params));
        params.verts = pmesh->verts;
        params.vertCount = pmesh->nverts;
        params.polys = pmesh->polys;
        params.polyAreas = pmesh->areas;
        params.polyFlags = pmesh->flags;
        params.polyCount = pmesh->npolys;
        params.nvp = pmesh->nvp;
        params.detailMeshes = 0;
        params.detailVerts = 0;
        params.detailVertsCount = 0;
        params.detailTris = 0;
        params.detailTriCount = 0;
        if(oc_count > 0) {
            params.offMeshConVerts = &oc_verts[0];
            params.offMeshConRad = &oc_rad[0];
            params.offMeshConDir = &oc_dir[0];
            params.offMeshConAreas = &oc_areas[0];
            params.offMeshConFlags = &oc_flags[0];
            params.offMeshConUserID = &oc_ids[0];
            params.offMeshConCount = oc_count;
        }
        params.walkableHeight = 2.0f;
        params.walkableRadius = 0.25f;
        params.walkableClimb = 1.0f;
        rcVcopy(params.bmin, pmesh->bmin);
        rcVcopy(params.bmax, pmesh->bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.tileX = tx;
        params.tileY = tz;
        params.tileLayer = 0;
        params.buildBvTree = true;

        int nav_data_size = 0;
        if(!dtCreateNavMeshData(&params, &nav_data, &nav_data_size))
            break;

        dtTileRef old = nav->mesh->getTileRefAt(tx, tz, 0);
        if(old)
            nav->mesh->removeTile(old, 0, 0);
        if(dtStatusFailed(nav->mesh->addTile(nav_data, nav_data_size,
                                             DT_TILE_FREE_DATA, 0, 0)))
            break; /* nav_data is freed once by the cleanup below */
        nav_data = 0; /* owned by the navmesh now */
        ok = true;
    } while(false);
    BC("[BC] build_tile exit tx=%d tz=%d ok=%d\n", tx, tz, (int)ok);

    if(nav_data)
        dtFree(nav_data);
    free_recast(hf, chf, cset, pmesh, dmesh);
    if(!ok)
        g_tile_build_failures++;
    return ok;
}

/* Build missing tiles in range and evict tiles that drifted out of range. */
static void mob_nav_update_tiles(mob_nav* nav, void* ctx, float mob_x,
                                 float mob_y, float mob_z) {
    /* Recenter the vertical window when the mob nears an edge (or on the first
     * call). All tiles are rebuilt at the new Y range so every loaded tile
     * shares one window and portals line up across tile borders. */
    const int center = (int)floorf(mob_y);
    if(nav->tile_ymax - nav->tile_ymin < 3
       || abs(center - nav->tile_center_y) > TILE_Y_RADIUS - TILE_Y_MARGIN) {
        int ymin = center - TILE_Y_RADIUS;
        int ymax = center + TILE_Y_RADIUS;
        if(ymin < 0)
            ymin = 0;
        if(ymax > WORLD_TOP)
            ymax = WORLD_TOP;
        nav->tile_ymin = ymin;
        nav->tile_ymax = ymax;
        nav->tile_center_y = center;

        const dtNavMesh* cm = nav->mesh;
        for(int i = 0; i < cm->getMaxTiles(); i++) {
            const dtMeshTile* tile = cm->getTile(i);
            if(tile && tile->header)
                nav->mesh->removeTile(cm->getTileRef(tile), 0, 0);
        }
        nav->path_count = 0;
    }

    const int mtx = (int)floorf(mob_x / (float)TILE_SIZE);
    const int mtz = (int)floorf(mob_z / (float)TILE_SIZE);

    const dtNavMesh* cmesh = nav->mesh; /* const overloads of getTile are public */
    for(int i = 0; i < cmesh->getMaxTiles(); i++) {
        const dtMeshTile* tile = cmesh->getTile(i);
        if(!tile || !tile->header)
            continue;
        if(abs(tile->header->x - mtx) > TILE_RADIUS
           || abs(tile->header->y - mtz) > TILE_RADIUS)
            nav->mesh->removeTile(cmesh->getTileRef(tile), 0, 0);
    }

    for(int tz = mtz - TILE_RADIUS; tz <= mtz + TILE_RADIUS; tz++)
        for(int tx = mtx - TILE_RADIUS; tx <= mtx + TILE_RADIUS; tx++)
            if(!nav->mesh->getTileAt(tx, tz, 0))
                mob_nav_build_tile(nav, ctx, tx, tz);
}

#ifdef FIND_MOB_PATH_DEBUG
/* Debug: dump the full 3D navigation view around the mob to a JSON file, one
 * file per plan. Each cell is x,y,z + type (begehbar/wand/creeper/spieler), plus
 * the path in block coordinates. Called from the C side with whichever path the
 * creeper actually follows (navmesh waypoints or the BFS route). */
extern "C" void mob_nav_write_json(void* ctx, long index, float mob_x,
                                   float mob_y, float mob_z, float goal_x,
                                   float goal_y, float goal_z,
                                   const float* straight, int count, int ymin,
                                   int ymax) {
    static bool dir_made = false;
    if(!dir_made) {
        mkdir("/tmp/cavex_nav", 0777);
        dir_made = true;
    }
    char fname[256];
    std::snprintf(fname, sizeof(fname), "/tmp/cavex_nav/map_%05ld.json", index);
    FILE* f = std::fopen(fname, "w");
    if(!f)
        return;

    const int cx = (int)std::floor(mob_x);
    const int cy = (int)std::floor(mob_y);
    const int cz = (int)std::floor(mob_z);
    const int gx = (int)std::floor(goal_x);
    const int gy = (int)std::floor(goal_y);
    const int gz = (int)std::floor(goal_z);
    const int R = 16; /* the detection radius */

    std::fprintf(f, "{\n  \"creeper\": {\"x\": %d, \"y\": %d, \"z\": %d},\n",
                 cx, cy, cz);
    std::fprintf(f, "  \"spieler\": {\"x\": %d, \"y\": %d, \"z\": %d},\n",
                 gx, gy, gz);
    std::fprintf(f, "  \"zellen\": [");

    bool first = true;
    for(int x = cx - R; x <= cx + R; x++) {
        for(int z = cz - R; z <= cz + R; z++) {
            /* Walk the column once: a cell is "wand" when solid, "begehbar"
             * when it is a standable spot (solid below, air here and above). */
            bool prev = mob_nav_world_solid(ctx, x, ymin - 1, z) != 0;
            bool cur = mob_nav_world_solid(ctx, x, ymin, z) != 0;
            for(int y = ymin; y < ymax; y++) {
                bool next = mob_nav_world_solid(ctx, x, y + 1, z) != 0;
                const char* type = 0;
                if(cur)
                    type = "wand";
                else if(prev && !next)
                    type = "begehbar";
                if(type) {
                    std::fprintf(f,
                        "%s\n    {\"x\": %d, \"y\": %d, \"z\": %d, "
                        "\"type\": \"%s\"}",
                        first ? "" : ",", x, y, z, type);
                    first = false;
                }
                prev = cur;
                cur = next;
            }
        }
    }
    std::fprintf(f,
        "%s\n    {\"x\": %d, \"y\": %d, \"z\": %d, \"type\": \"creeper\"},"
        "\n    {\"x\": %d, \"y\": %d, \"z\": %d, \"type\": \"spieler\"}",
        first ? "" : ",", cx, cy, cz, gx, gy, gz);

    std::fprintf(f, "\n  ],\n  \"pfad\": [");
    for(int i = 0; i < count; i++)
        std::fprintf(f, "%s\n    {\"x\": %d, \"y\": %d, \"z\": %d}",
                     i ? "," : "", (int)std::floor(straight[i * 3]),
                     (int)std::floor(straight[i * 3 + 1]),
                     (int)std::floor(straight[i * 3 + 2]));
    std::fprintf(f, "\n  ]\n}\n");
    std::fclose(f);
}
#endif

static void mob_nav_replan(mob_nav* nav, void* ctx, float mob_x, float mob_y,
                           float mob_z, const float goal[3]) {
    nav->path_count = 0;
    nav->wp_index = 0;
    nav->replan_timer = REPLAN_INTERVAL;
    nav->planned_goal[0] = goal[0];
    nav->planned_goal[1] = goal[1];
    nav->planned_goal[2] = goal[2];

    const float start[3] = {mob_x, mob_y, mob_z};
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    dtPolyRef start_ref = 0, goal_ref = 0;
    float ns[3], ng[3];
    if(dtStatusFailed(nav->query->findNearestPoly(start, extents, &nav->filter,
                                                  &start_ref, ns))
       || dtStatusFailed(nav->query->findNearestPoly(goal, extents,
                                                     &nav->filter, &goal_ref,
                                                     ng))
       || !start_ref || !goal_ref)
        return;

    /* If the nearest start polygon is far from the mob, the mob is not actually
     * standing on the navmesh (e.g. boxed in by walls, so findNearestPoly
     * snapped to a polygon outside its cell). Report no path -- otherwise it
     * would chase a route it cannot even begin. */
    {
        const float sdx = ns[0] - mob_x;
        const float sdz = ns[2] - mob_z;
        if(sdx * sdx + sdz * sdz > 0.9f * 0.9f)
            return;
    }

    /* If the goal snapped onto the mob's own polygon while the real goal is far
     * away, the player is not on the navmesh here (e.g. standing elevated on a
     * spot that has no poly). findPath would then return a trivial "already
     * there" path and the mob would never try the stairs. Report no path so the
     * block-centered BFS takes over. */
    if(goal_ref == start_ref) {
        const float dx = goal[0] - mob_x;
        const float dy = goal[1] - mob_y;
        const float dz = goal[2] - mob_z;
        if(dx * dx + dy * dy + dz * dz > 2.0f * 2.0f)
            return;
    }

    /* The snapped goal is on a very different level than the real goal: the
     * player's (elevated) spot has no navmesh polygon, so findNearestPoly fell
     * back to a lower polygon. A path there would never climb -- report no path
     * so the block-centered BFS (which climbs stairs) takes over. */
    {
        const float gyd = ng[1] - goal[1];
        if(gyd * gyd > 3.0f * 3.0f)
            return;
    }

    dtPolyRef polys[MAX_PATH_POLYS];
    int poly_count = 0;
    dtStatus find_status = nav->query->findPath(start_ref, goal_ref, ns, ng,
                                                &nav->filter, polys, &poly_count,
                                                MAX_PATH_POLYS);
    if(dtStatusFailed(find_status) || poly_count == 0)
        return;

    int count = 0;
    if(dtStatusFailed(nav->query->findStraightPath(
           ns, ng, polys, poly_count, nav->straight, nav->straight_flags,
           nav->straight_refs, &count, MAX_WAYPOINTS,
           DT_STRAIGHTPATH_ALL_CROSSINGS)))
        return;
    nav->path_count = count;
    nav->wp_index = 0;

    /* Detour sets DT_PARTIAL_RESULT when the goal polygon could not be reached
     * (unreachable across the navmesh -- e.g. an elevated player reachable only
     * by stairs that did not connect, or a boxed-in spot). Such a stub strands
     * the mob short of the player, so drop it and let the caller fall back to
     * the block-centered BFS, which climbs stairs and reaches tight spots. */
    if(dtStatusDetail(find_status, DT_PARTIAL_RESULT))
        nav->path_count = 0;

    /* A one-point straight path is just the start: no useful route (goal in the
     * same polygon). Treat as no path so the BFS fallback runs. */
    if(count <= 1)
        nav->path_count = 0;

#ifdef FIND_MOB_PATH_DEBUG
    static long replan_prints = 0;
    if(replan_prints < 60) {
        std::fprintf(stderr, "[BC] replan start=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) "
                     "pathcount=%d polys=%d wps=%d:", mob_x, mob_y, mob_z,
                     goal[0], goal[1], goal[2], nav->path_count, poly_count, count);
        for(int i = 0; i < count; i++)
            std::fprintf(stderr, " (%.1f,%.1f,%.1f)", nav->straight[i * 3],
                         nav->straight[i * 3 + 1], nav->straight[i * 3 + 2]);
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        replan_prints++;
    }
#endif
}

extern "C" mob_nav* mob_nav_create(void) {
    BC("[BC] mob_nav_create\n");
    mob_nav* nav = (mob_nav*)std::calloc(1, sizeof(mob_nav));
    if(!nav)
        return 0;
    nav->mesh = dtAllocNavMesh();
    nav->query = dtAllocNavMeshQuery();
    if(!nav->mesh || !nav->query) {
        mob_nav_destroy(nav);
        return 0;
    }

    dtNavMeshParams params;
    std::memset(&params, 0, sizeof(params));
    params.orig[0] = 0.0f;
    params.orig[1] = 0.0f;
    params.orig[2] = 0.0f;
    params.tileWidth = (float)TILE_SIZE;
    params.tileHeight = (float)TILE_SIZE;
    params.maxTiles = MAX_TILES;
    params.maxPolys = MAX_POLYS_PER_TILE;
    if(dtStatusFailed(nav->mesh->init(&params))
       || dtStatusFailed(nav->query->init(nav->mesh, 2048))) {
        mob_nav_destroy(nav);
        return 0;
    }
    /* calloc zeroed the filter; run its constructor so include flags and area
     * costs are the Detour defaults (otherwise every poly is excluded). */
    new(&nav->filter) dtQueryFilter();
    nav->path_count = 0;
    nav->wp_index = 0;
    nav->replan_timer = 0;
    nav->tile_ymin = 0;
    nav->tile_ymax = 0; /* invalid window -> first steer centers it */
    nav->tile_center_y = 0;
    g_active_navs++;
    return nav;
}

extern "C" void mob_nav_destroy(mob_nav* nav) {
    if(!nav)
        return;
    if(nav->query)
        dtFreeNavMeshQuery(nav->query);
    if(nav->mesh)
        dtFreeNavMesh(nav->mesh);
    std::free(nav);
    if(g_active_navs > 0)
        g_active_navs--;
}

extern "C" int mob_nav_steer(mob_nav* nav, void* ctx, float mob_x, float mob_y,
                             float mob_z, float goal_x, float goal_y,
                             float goal_z, float* dir_x, float* dir_z) {
    if(!nav || !dir_x || !dir_z)
        return 0;
    *dir_x = 0.0f;
    *dir_z = 0.0f;

    BC("[BC] steer enter pos=(%.1f,%.1f,%.1f) goal=(%.1f,%.1f,%.1f)\n", mob_x,
       mob_y, mob_z, goal_x, goal_y, goal_z);
    mob_nav_update_tiles(nav, ctx, mob_x, mob_y, mob_z);
    BC("[BC] steer: tiles updated\n");

    const float goal[3] = {goal_x, goal_y, goal_z};
    const float gdx = goal_x - nav->planned_goal[0];
    const float gdz = goal_z - nav->planned_goal[2];
    const bool need_replan = nav->path_count == 0
        || (gdx * gdx + gdz * gdz) > REPLAN_GOAL_MOVE * REPLAN_GOAL_MOVE
        || --nav->replan_timer <= 0;
    BC("[BC] steer: need_replan=%d\n", (int)need_replan);
    if(need_replan)
        mob_nav_replan(nav, ctx, mob_x, mob_y, mob_z, goal);
    BC("[BC] steer: replan done pc=%d\n", nav->path_count);

    if(nav->path_count == 0)
        return 0;

    /* Advance past waypoints already reached horizontally. */
    while(nav->wp_index < nav->path_count) {
        const float* wp = &nav->straight[nav->wp_index * 3];
        const float dx = wp[0] - mob_x;
        const float dz = wp[2] - mob_z;
        if(dx * dx + dz * dz < WAYPOINT_REACHED * WAYPOINT_REACHED)
            nav->wp_index++;
        else
            break;
    }

    const float* target = (nav->wp_index < nav->path_count)
        ? &nav->straight[nav->wp_index * 3]
        : &nav->straight[(nav->path_count - 1) * 3];
    const float dx = target[0] - mob_x;
    const float dz = target[2] - mob_z;
    const float len = sqrtf(dx * dx + dz * dz);
    if(len < 0.001f)
        return 0;
    *dir_x = dx / len;
    *dir_z = dz / len;
    return 1;
}

extern "C" void mob_nav_notify_block_change(mob_nav* nav, int x, int y, int z) {
    (void)y;
    if(!nav || !nav->mesh)
        return;
    const int tx = (int)floorf((float)x / (float)TILE_SIZE);
    const int tz = (int)floorf((float)z / (float)TILE_SIZE);
    dtTileRef ref = nav->mesh->getTileRefAt(tx, tz, 0);
    if(ref) {
        nav->mesh->removeTile(ref, 0, 0);
        nav->path_count = 0; /* path may have crossed the removed tile */
    }
}

extern "C" int mob_nav_any_active(void) {
    return g_active_navs > 0;
}

/* Copy the currently followed navmesh waypoints (x,y,z each) for debug export.
 * Returns 0 when no navmesh path is active. */
extern "C" int mob_nav_copy_path(mob_nav* nav, float* out_xyz, int max_points) {
    if(!nav || !out_xyz || nav->path_count <= 0)
        return 0;
    int n = nav->path_count;
    if(n > max_points)
        n = max_points;
    for(int i = 0; i < n * 3; i++)
        out_xyz[i] = nav->straight[i];
    return n;
}

extern "C" void mob_nav_debug_counts(long* attempts, long* failures) {
    if(attempts)
        *attempts = g_tile_build_attempts;
    if(failures)
        *failures = g_tile_build_failures;
}
