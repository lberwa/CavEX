/* test_world.c — wird nur mit -DWGEN_TESTING=1 kompiliert (PC-Build). */
typedef int test_wgen_dummy_t; /* verhindert "empty translation unit" auf Wii */
#ifdef WGEN_TESTING
#include "gen_world.h"
#include <stdio.h>
#include <inttypes.h>

/* Reproduzierbarkeits- und Plausibilitaetstests fuer den Weltgenerator. */
extern int64_t  wgen_prng_next_long_test(int64_t seed); /* Testhelfer */
extern int64_t  wgen_legacy_next_long_test(int64_t seed);
extern void     wgen_seed128_test(int64_t seed, int64_t* lo, int64_t* hi);
extern double   wgen_perlin_sample_test(int64_t seed_lo, double x, double y, double z);
extern double   wgen_layered_noise_test_cont(int64_t seed, double x, double z);
extern double   wgen_gen_offset_test(wgen_gen_t* g, double wx, double wz);
extern int64_t  wgen_decoration_seed_test(int64_t world_seed, int chunk_x, int chunk_z);

static int test_prng(void) {
    /* Seed 0: 128-Bit-PRNG initialisiert mit Fallback-Werten */
    int64_t v = wgen_prng_next_long_test(0);
    printf("prng(0) first  = %"PRId64"\n", v);
    return 1;
}

static int test_legacy(void) {
    int64_t v = wgen_legacy_next_long_test(0);
    printf("legacy(0) long = %"PRId64"\n", v);
    return 1;
}

static int test_seed128(void) {
    int64_t lo, hi;
    wgen_seed128_test(0, &lo, &hi);
    printf("seed128(0): lo=%"PRId64" hi=%"PRId64"\n", lo, hi);
    wgen_seed128_test(12345, &lo, &hi);
    printf("seed128(12345): lo=%"PRId64" hi=%"PRId64"\n", lo, hi);
    return 1;
}

static int test_perlin(void) {
    double v = wgen_perlin_sample_test(0, 1.0, 0.0, 0.5);
    printf("perlin(seed=0, 1,0,0.5) = %.10f\n", v);
    return 1;
}

static int test_layered_noise(void) {
    double v = wgen_layered_noise_test_cont(0, 0.0, 0.0);
    printf("landmass(seed=0, 0,0,0) = %.12f\n", v);
    /* Sanity-Check: Wert muss im plausiblen Noise-Bereich liegen */
    int ok = (v > -2.0 && v < 2.0);
    if (!ok) printf("  FAIL: Wert ausserhalb des erwarteten Bereichs!\n");
    return ok;
}

static int test_climate(void) {
    wgen_gen_t* g = wgen_gen_create(0);
    if (!g) { printf("test_climate: wgen_gen_create failed\n"); return 0; }
    double cont, eros, temp, vege, rugged;
    wgen_gen_climate(g, 0.0, 0.0, &cont, &eros, &temp, &vege, &rugged);
    printf("climate(seed=0, 0,0): cont=%.8f eros=%.8f temp=%.8f vege=%.8f rugged=%.8f\n",
           cont, eros, temp, vege, rugged);
    wgen_gen_destroy(g);
    /* Plausibilitaetspruefung: alle Werte in [-2, 2] */
    int ok = (cont  > -2.0 && cont  < 2.0)
          && (eros  > -2.0 && eros  < 2.0)
          && (temp  > -2.0 && temp  < 2.0)
          && (vege  > -2.0 && vege  < 2.0)
          && (rugged > -2.0 && rugged < 2.0);
    if (!ok) printf("  FAIL: Klimawert ausserhalb [-2, 2]!\n");
    return ok;
}

static int test_offset(void) {
    wgen_gen_t* g = wgen_gen_create(0);
    if (!g) { printf("test_offset: wgen_gen_create failed\n"); return 0; }
    double off = wgen_gen_offset_test(g, 0.0, 0.0);
    printf("offset(seed=0, 0,0) = %.8f\n", off);
    /* Erwarteter Bereich: -0.50375 + spline_result ∈ [-1.0, 1.0] */
    int ok = (off >= -1.0 && off <= 1.0);
    if (!ok) printf("  FAIL: offset %.8f ausserhalb [-1, 1]!\n", off);
    /* Stichprobe: einige weitere Koordinaten im plausiblen Bereich */
    for (int i = 0; i < 5; i++) {
        double wx = (double)(i * 1000);
        double wz = (double)(i * 777);
        double v = wgen_gen_offset_test(g, wx, wz);
        printf("  offset(%.0f, %.0f) = %.8f\n", wx, wz, v);
        if (v < -1.5 || v > 1.5) { ok = 0; printf("    FAIL: out of range!\n"); }
    }
    wgen_gen_destroy(g);
    return ok;
}

static int test_features(void) {
    /* Reproduzierbarkeitstest: gleicher Input muss gleichen Output liefern */
    int64_t ds1 = wgen_decoration_seed_test(0, 0, 0);
    int64_t ds2 = wgen_decoration_seed_test(0, 0, 0);
    printf("decoration_seed(world=0, chunk 0,0) = %"PRId64"\n", ds1);
    printf("decoration_seed reproduced: %s\n", ds1 == ds2 ? "OK" : "FAIL");

    /* Verschiedene Chunks muessen verschiedene Seeds erzeugen */
    int64_t ds3 = wgen_decoration_seed_test(0, 1, 0);
    int64_t ds4 = wgen_decoration_seed_test(0, 0, 1);
    printf("decoration_seed(world=0, chunk 1,0) = %"PRId64"\n", ds3);
    printf("decoration_seed(world=0, chunk 0,1) = %"PRId64"\n", ds4);

    /* Verschiedene World-Seeds muessen verschiedene Seeds erzeugen */
    int64_t ds5 = wgen_decoration_seed_test(12345, 0, 0);
    printf("decoration_seed(world=12345, chunk 0,0) = %"PRId64"\n", ds5);

    int ok = (ds1 == ds2)       /* reproduzierbar */
          && (ds1 != ds3)       /* chunk_x aendert den Seed */
          && (ds1 != ds4)       /* chunk_z aendert den Seed */
          && (ds1 != ds5);      /* world_seed aendert den Seed */
    if (!ok) printf("  FAIL: decoration_seed nicht reproduzierbar oder nicht eindeutig!\n");
    return ok;
}

static int test_surface_heights(void) {
    wgen_gen_t* g = wgen_gen_create(0);
    if (!g) { printf("test_surface_heights: wgen_gen_create failed\n"); return 0; }
    uint8_t h[16][16];
    wgen_gen_surface_heights(g, 0, 0, h);
    printf("Heights chunk(0,0) seed=0, x=0 row:\n  ");
    for (int z = 0; z < 16; z++) printf("%3d", h[0][z]);
    printf("\n");
    /* alle Werte muessen im Bereich 1..123 liegen */
    int ok = 1;
    for (int x = 0; x < 16 && ok; x++)
        for (int z = 0; z < 16 && ok; z++)
            ok = (h[x][z] >= 1 && h[x][z] <= 123);
    if (!ok) printf("  FAIL: Hoehenwert ausserhalb [1, 123]!\n");
    wgen_gen_destroy(g);
    return ok;
}

int main(void) {
    int ok = 1;
    ok &= test_prng();
    ok &= test_legacy();
    ok &= test_seed128();
    ok &= test_perlin();
    ok &= test_layered_noise();
    ok &= test_climate();
    ok &= test_offset();
    ok &= test_surface_heights();
    ok &= test_features();
    printf(ok ? "ALL: OK\n" : "SOME: FAIL\n");
    return ok ? 0 : 1;
}
#endif /* WGEN_TESTING */
