#ifndef GEN_WGEN_H
#define GEN_WGEN_H

#include <stdint.h>

/* Vorwaertsdeklaration — vollstaendige Definition in server_world.h */
struct server_chunk;

typedef struct wgen_gen wgen_gen_t;

/* Initialisierung: seed ist der Welt-Seed. */
wgen_gen_t* wgen_gen_create(int64_t seed);
void        wgen_gen_destroy(wgen_gen_t* g);

/* Terrain-Hoehe (0..127) fuer alle 16x16 Block-Spalten eines Chunks. */
void wgen_gen_surface_heights(wgen_gen_t* g,
                               int chunk_x, int chunk_z,
                               uint8_t heights[16][16]);

/* Features (Baeume, Kakteen, …) in den Chunk schreiben. */
void wgen_gen_place_features(wgen_gen_t* g,
                              struct server_chunk* sc,
                              int chunk_x, int chunk_z,
                              int biome_id,
                              const uint8_t heights[16][16]);

/* Debug: Klimawerte an einem Weltpunkt (nicht skaliert). */
void wgen_gen_climate(wgen_gen_t* g, double wx, double wz,
                      double* cont, double* eros,
                      double* temp, double* vege, double* rugged);

/* Berechne Terrain-Hoehe direkt aus Noise fuer Weltkoordinate */
int wgen_gen_world_height(wgen_gen_t* g, int wx, int wz);

#endif /* GEN_WGEN_H */
