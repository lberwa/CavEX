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

#ifndef SERVER_LOCAL
#define SERVER_LOCAL

#include "../m-lib/m-string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../item/inventory.h"
#include "../world.h"
#include "level_archive.h"
#include "server_world.h"
#include "../entity/entity.h"

#define MAX_REGIONS 4
/* obere Grenze in Chunks. Auf der Wii war 5 fuer 2-Spieler-Splitscreen NICHT
 * tragbar: der Governor kletterte bis 5, dessen Working-Set-Peak beanspruchte
 * die gesamte (nie zurueckkehrende) MEM2-Arena -> Absturz. Bei cap=96 sehen zwei
 * getrennte Spieler ohnehin schon bei vd=3 je ~49 Chunks (98 > cap), hoeher
 * bringt getrennt nichts. Der PC hat kein solches Limit -> dort der urspr. Wert. */
#ifdef PLATFORM_WII
#define MAX_VIEW_DISTANCE 7 // in chunks, obere Grenze (Wii-RAM)
#else
#define MAX_VIEW_DISTANCE 15 // in chunks, obere Grenze (PC)
#endif
/* Untere Grenze der Sichtweite. vd=1 (nur der eigene + 4 Nachbar-Chunks) ist ein
 * kaputter Grenzfall: der Spieler ueberrennt beim Laufen das Nachladen sofort und
 * das Spiel stuerzt still ins HBC. Bei 1 ist ohnehin nur ~16 Bloecke Sicht, also
 * unspielbar -> Minimum 2 (= MAX_HIGH_DETAIL_VIEW_DISTANCE). */
#define MIN_VIEW_DISTANCE 2
#define MAX_HIGH_DETAIL_VIEW_DISTANCE 2

/* Harte Obergrenze gleichzeitig geladener Server-Chunks (Wii-RAM-Schutz).
 * Jeder Chunk kostet ~80KB Server + Client-Mesh; empirisch geht ab ~140 Chunks
 * der MEM2-Speicher aus. 96 lässt komfortablen Puffer und ist die einzige
 * ZUVERLÄSSIGE Grenze (Speicher-Messungen lügen bei Fragmentierung). */
#ifndef SERVER_CHUNK_HARD_CAP
#define SERVER_CHUNK_HARD_CAP 96
#endif

/* Dynamisch reduzierte Sichtweite (Wii-RAM-Schutz). Startet bei
 * MAX_VIEW_DISTANCE, wird vom Haupt-Thread bei RAM-Druck verkleinert.
 * Volatile reicht: ein Schreiber (Main-Thread), ein Leser (Server-Thread). */
extern volatile int g_effective_view_distance;

/* Debug-Nachrichten aus dem Server-Thread an den Main-Thread übertragen.
 * Pro Frame aus dem Main-Thread aufrufen (thread-safe über Ring-Buffer). */
void cdbg_flush(void);
#define MAX_CHUNKS ((MAX_VIEW_DISTANCE * 2 + 2) * (MAX_VIEW_DISTANCE * 2 + 2))
#define MAX_HIGH_DETAIL_CHUNKS ((MAX_HIGH_DETAIL_VIEW_DISTANCE * 2 + 2) * (MAX_HIGH_DETAIL_VIEW_DISTANCE * 2 + 2))
#define MAX_CHESTS 256
#define MAX_CHEST_SLOTS 54
#define MAX_FURNACES 256
#define MAX_BREWING_STANDS 256
#define MAX_ENCHANTING_TABLES 256
#define MAX_SIGNS 256
#define MAX_BUTTONS 256
#define MAX_REPEATERS 256

#define MAX_OXYGEN 351
#define OXYGEN_THRESHOLD 0

/* Max fluid (water/lava) cell changes buffered per world tick. Fluid spreading
 * reads the OLD world state and queues changes here; they are applied all at
 * once after the tick (double-buffer / cellular-automaton). If a single step
 * produces more changes than this, the surplus is simply handled next step. */
#define MAX_FLUID_CHANGES 4096

/* Max fluid cells scheduled for a flow update in one round. Only water next to
 * a recent block change is scheduled, so this bounds the active "wavefront". */
#define MAX_FLUID_UPDATES 8192
/* Open-addressing dedup table for the schedule; power of two, > MAX_FLUID_UPDATES
 * so it never fills past ~50% load. Without dedup the buffer overflows with
 * duplicate wakes during recession and real cells get dropped (never dry up). */
#define FLUID_HASH_SIZE 16384

struct complex_block_pos {
	int x, y, z;
};

struct fluid_change {
	w_coord_t x, y, z;
	struct block_data blk;
};

struct fluid_pos {
	w_coord_t x, y, z;
};

struct furnace_data {
	struct complex_block_pos pos;
	struct item_data items[FURNACE_SIZE_STORAGE];
	uint16_t burn_time;
	uint16_t burn_total;
	uint16_t cook_time;
	uint16_t cook_total;
};

struct brewing_stand_data {
	struct complex_block_pos pos;
	struct item_data items[BREWING_STAND_SIZE_STORAGE];
	uint16_t brew_time;
	uint16_t brew_total;
};

struct enchanting_table_data {
	struct complex_block_pos pos;
	struct item_data items[ENCHANTING_TABLE_SIZE_STORAGE];
};

struct button_state {
	struct complex_block_pos pos;
	uint8_t timer;
};

struct repeater_state {
	struct complex_block_pos pos;
	uint8_t timer;
	uint8_t target_type;
};

struct server_player {
	double x, y, z;
	float rx, ry;
	enum world_dim dimension;
	bool has_pos;
	bool finished_loading;
	struct inventory inventory;
	struct inventory* active_inventory;
	struct item_data ender_chest_items[CHEST_SIZE_STORAGE];
	short health;
	int oxygen;
	int spawn_x, spawn_y, spawn_z;
	float vel_y, old_vel_y;
	float fall_distance;
	bool creative;
};

struct server_local {
	struct random_gen rand_src;
#define MAX_SERVER_PLAYERS 4
	struct server_player players[MAX_SERVER_PLAYERS];
	// Player id of the RPC currently being processed (used by block/inventory
	// callbacks that don't carry player context yet).
	uint8_t active_player_id;
	struct server_world world;
	bool world_initialized;
	/* set for a freshly created world: snap the spawn/player onto solid ground
	 * once the spawn area has been generated */
	bool find_spawn;
	/* true while the spawn area is still being generated/loaded -> the server
	 * thread ticks without the idle delay for maximum generation speed */
	bool loading;
	dict_entity_t entities;
	struct complex_block_pos chest_pos[MAX_CHESTS];
	struct item_data chest_items[MAX_CHESTS][MAX_CHEST_SLOTS];
	struct furnace_data furnaces[MAX_FURNACES];
	struct brewing_stand_data brewing_stands[MAX_BREWING_STANDS];
	struct enchanting_table_data enchanting_tables[MAX_ENCHANTING_TABLES];
	struct button_state buttons[MAX_BUTTONS];
	struct repeater_state repeaters[MAX_REPEATERS];
	struct complex_block_pos sign_pos[MAX_SIGNS];
	char sign_texts[MAX_SIGNS][SIGN_SIZE];
	uint64_t world_time;
	string_t level_name;
	struct level_archive level;
	bool paused;
	ptime_t last_tick;
	/* pending fluid changes for this tick (see MAX_FLUID_CHANGES) */
	struct fluid_change fluid_changes[MAX_FLUID_CHANGES];
	int fluid_change_count;
	/* water cells scheduled for a flow update (see MAX_FLUID_UPDATES) */
	struct fluid_pos fluid_sched[MAX_FLUID_UPDATES];
	int fluid_sched_count;
	/* dedup table: fluid_sched index + 1 per slot, 0 = empty (see FLUID_HASH_SIZE) */
	int32_t fluid_hash[FLUID_HASH_SIZE];
};

void server_local_create(struct server_local* s);
bool server_local_try_portal(struct server_local* s, int x, int y, int z);
void server_local_collapse_portal(struct server_local* s, int x, int y, int z);
struct entity* server_local_spawn_minecart(vec3 pos, struct server_local* s);
struct entity* server_local_spawn_boat(vec3 pos, float yaw,
                                       struct server_local* s);
// Cast a fishing hook from pos in the direction given by the player look
// angles rx (pitch, degrees) and ry (yaw, degrees). owner_id must be
// (player_entity_id + 1) to avoid the id=0 sentinel clash.
struct entity* server_local_spawn_fishing_hook(vec3 pos, float rx, float ry,
                                               uint32_t owner_id,
                                               struct server_local* s);
struct entity* server_local_spawn_item(vec3 pos, struct item_data* it,
									   bool throw, struct server_local* s);
struct entity* server_local_spawn_monster(vec3 pos, int monster_id,
									   struct server_local* s);
void server_local_spawn_block_drops(struct server_local* s,
									struct block_info* blk_info);
void server_local_send_inv_changes(uint8_t player_id, set_inv_slot_t changes,
								   struct inventory* inv, uint8_t window);
void server_local_set_player_health(struct server_local* s, int player_id, short new_health);
/* queue a fluid cell change to be applied after the current world tick */
void server_local_queue_fluid_change(struct server_local* s, w_coord_t x,
									 w_coord_t y, w_coord_t z,
									 struct block_data blk);
/* apply all queued fluid changes and reset the buffer */
void server_local_flush_fluid_changes(struct server_local* s);
/* wake a water cell so it re-evaluates its flow on the next fluid round */
void server_local_schedule_fluid(struct server_local* s, w_coord_t x,
								 w_coord_t y, w_coord_t z);
/* process one round of scheduled water flow updates */
void server_local_tick_fluids(struct server_local* s);
extern bool place_block;
#endif
