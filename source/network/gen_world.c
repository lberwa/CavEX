#include "gen_world.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>

/* ── schneller 128-Bit-PRNG ── */
typedef struct { int64_t lo; int64_t hi; } prng128_t;

/* Linksrotation: rotl64(x,n) == rotr64(x,64-n). */
static inline uint64_t rotl64(uint64_t x, unsigned n) {
    return (x << n) | (x >> (64u - n));
}

/* Fallback-Seeds fuer den (seed==0)-Fall: die Konstanten
 * GOLDEN_RATIO_64 und SILVER_RATIO_64. */
#define PRNG_FALLBACK_LO  ((int64_t)0x9E3779B97F4A7C15LL)  /* -7046029254386353131 */
#define PRNG_FALLBACK_HI  ((int64_t)0x6A09E667F3BCC909LL)  /*  7640891576956012809 */

static void prng128_init(prng128_t* r, int64_t lo, int64_t hi) {
    if ((lo | hi) == 0) {
        r->lo = PRNG_FALLBACK_LO;
        r->hi = PRNG_FALLBACK_HI;
    } else {
        r->lo = lo;
        r->hi = hi;
    }
}

/*
 * schneller 128-Bit-PRNG next: liefert den naechsten 64-Bit-Zufallswert.
 * Strukturell aufgeteilt: zuerst result berechnen, dann State updaten,
 * mit expliziten uint64_t-Casts fuer alle Bit-Operationen.
 */
static int64_t prng128_next_long(prng128_t* r) {
    uint64_t s0 = (uint64_t)r->lo;
    uint64_t s1 = (uint64_t)r->hi;
    /* ++ kombinator: rotl(s0+s1, 17) + s0 */
    uint64_t result = rotl64(s0 + s1, 17u) + s0;
    /* State-Update: PRNG-Schritt */
    uint64_t mixed = s0 ^ s1;
    /* rotr64(s0,15) == rotl64(s0,49): identisch, andere Schreibweise */
    uint64_t new_lo = (s0 >> 15u | s0 << 49u) ^ mixed ^ (mixed << 21u);
    uint64_t new_hi = rotl64(mixed, 28u);
    r->lo = (int64_t)new_lo;
    r->hi = (int64_t)new_hi;
    return (int64_t)result;
}

/* nextDouble: (nextLong() >>> 11) * (1.0 / 2^53) */
static double prng128_next_double(prng128_t* r) {
    uint64_t mantissa = (uint64_t)prng128_next_long(r) >> 11u;
    /* 1.0/(1LL<<53) = 1.1102230246251565e-16 */
    return (double)mantissa * (1.0 / (double)(1LL << 53));
}

/* nextInt: untere 32 Bit von nextLong() */
static int32_t prng128_next_int(prng128_t* r) {
    return (int32_t)prng128_next_long(r);
}

/* nextInt(bound): Integer.toUnsignedLong(nextInt()) * bound >> 32 */
static int32_t prng128_next_int_bound(prng128_t* r, int32_t bound) {
    uint64_t uval = (uint64_t)(uint32_t)prng128_next_int(r);
    return (int32_t)((uval * (uint64_t)(uint32_t)bound) >> 32u);
}

/* ── LegacyRandom (48-Bit-LCG) ── */
typedef struct { uint64_t seed; } legacy_rng_t;

#define LEGACY_MULT  25214903917ULL
#define LEGACY_ADDEND        11ULL
#define LEGACY_MASK  0xFFFFFFFFFFFFULL   /* (1LL<<48)-1 */

static void legacy_set_seed(legacy_rng_t* r, int64_t seed) {
    r->seed = ((uint64_t)seed ^ LEGACY_MULT) & LEGACY_MASK;
}

/* next(bits): einen LCG-Schritt ausfuehren und obere `bits` Bits zurueckgeben. */
static int32_t legacy_next(legacy_rng_t* r, int bits) {
    r->seed = (r->seed * LEGACY_MULT + LEGACY_ADDEND) & LEGACY_MASK;
    return (int32_t)(uint32_t)((uint64_t)r->seed >> (48 - bits));
}

/* nextLong: zwei 32-Bit-Haelften zusammensetzen. */
static int64_t legacy_next_long(legacy_rng_t* r) {
    int64_t hi_part = (int64_t)legacy_next(r, 32) << 32;
    int64_t lo_part = (int64_t)legacy_next(r, 32);
    return hi_part + lo_part;
}

/* nextInt(bound): mit Bias-Ablehnung fuer Nicht-Potenzen-von-2. */
static int32_t __attribute__((unused)) legacy_next_int_bound(legacy_rng_t* r, int32_t bound) {
    if ((bound & -bound) == bound)
        return (int32_t)((int64_t)bound * (int64_t)legacy_next(r, 31) >> 31);
    int32_t bits, val;
    do {
        bits = legacy_next(r, 31);
        val  = bits % bound;
    } while (bits - val + (bound - 1) < 0);
    return val;
}

/* ── Seed auf 128 Bit erweitern (Bit-Finalizer) ── */

/*
 * Bit-Finalizer — finalizer fuer 64-Bit-Hashes.
 * Shift-Konstanten als benannte Werte zur Klarheit.
 */
#define MIX_SHIFT_A  30
#define MIX_MULT_A   ((int64_t)(-4658895280553007687LL))   /* 0xbf58476d1ce4e5b9 */
#define MIX_SHIFT_B  27
#define MIX_MULT_B   ((int64_t)(-7723592293110705685LL))   /* 0x94d049bb133111eb */
#define MIX_SHIFT_C  31

static int64_t bitmix13(int64_t z) {
    z = (z ^ (int64_t)((uint64_t)z >> MIX_SHIFT_A)) * MIX_MULT_A;
    z = (z ^ (int64_t)((uint64_t)z >> MIX_SHIFT_B)) * MIX_MULT_B;
    return z ^ (int64_t)((uint64_t)z >> MIX_SHIFT_C);
}

/*
 * Seed auf 128 Bit erweitern: einen int64-Seed auf (lo, hi) fuer 128-Bit-PRNG erweitern.
 * SILVER_RATIO_64 = 0x6A09E667F3BCC909, GOLDEN_RATIO_64 = 0x9E3779B97F4A7C15.
 */
static void wgen_seed_upgrade_128bit(int64_t seed, int64_t* lo, int64_t* hi) {
    int64_t base    = seed ^ (int64_t)0x6A09E667F3BCC909LL;   /* XOR SILVER_RATIO */
    int64_t base_hi = base  + (int64_t)0x9E3779B97F4A7C15LL;  /* + GOLDEN_RATIO */
    *lo = bitmix13(base);
    *hi = bitmix13(base_hi);
}

/* ── MD5 (kompakte Standalone-Implementierung) ── */

static const uint32_t MD5_T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const int MD5_S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
};

/*
 * wgen_md5: berechnet MD5(msg[0..len-1]) und schreibt 16 Bytes nach out.
 * Ausgabe: little-endian (Standard-MD5).
 *
 * Stack-Buffer fuer Wii-Kompatibilitaet: Alle zu hashenden Strings sind
 * Noise-Namen (< 64 Zeichen), nach MD5-Padding also max. 128 Bytes.
 */
static void wgen_md5(const uint8_t* msg, size_t len, uint8_t out[16]) {
    /* MD5-Initialisierungsvektoren */
    uint32_t a0 = 0x67452301u;
    uint32_t b0 = 0xefcdab89u;
    uint32_t c0 = 0x98badcfeu;
    uint32_t d0 = 0x10325476u;

    /* Padded length: naechstes Vielfaches von 64 nach (len+8).
     * Fuer len<56: padlen=64; fuer 56<=len<120: padlen=128. */
    size_t padlen = ((len + 8u) / 64u + 1u) * 64u;
    /* Sicherheitsannahme: Noise-Namen sind immer < 64 Bytes. */
    assert(padlen <= 128u);

    /* Stack-Array statt calloc (Wii-kompatibel). */
    uint8_t buf[128];
    memset(buf, 0, padlen);
    memcpy(buf, msg, len);
    buf[len] = 0x80u;

    /* Nachrichtenlaenge in Bits, little-endian in letzten 8 Bytes des Blocks. */
    uint64_t bitlen = (uint64_t)len * 8u;
    for (int i = 0; i < 8; i++)
        buf[padlen - 8u + (size_t)i] = (uint8_t)(bitlen >> (i * 8));

    /* Alle 64-Byte-Bloecke verarbeiten. */
    for (size_t off = 0; off < padlen; off += 64u) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = buf + off + (size_t)i * 4u;
            M[i] = (uint32_t)p[0]
                 | ((uint32_t)p[1] << 8u)
                 | ((uint32_t)p[2] << 16u)
                 | ((uint32_t)p[3] << 24u);
        }

        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F;
            uint32_t g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = (uint32_t)i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5u * (uint32_t)i + 1u) % 16u;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3u * (uint32_t)i + 5u) % 16u;
            } else {
                F = C ^ (B | ~D);
                g = (7u * (uint32_t)i) % 16u;
            }
            F += A + MD5_T[i] + M[g];
            A = D; D = C; C = B;
            unsigned s = (unsigned)MD5_S[i];
            B += (F << s) | (F >> (32u - s));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }

    /* Ausgabe: 4 uint32-Worte little-endian (Standard-MD5). */
    for (int i = 0; i < 4; i++) out[i]      = (uint8_t)(a0 >> (unsigned)(i * 8));
    for (int i = 0; i < 4; i++) out[4  + i] = (uint8_t)(b0 >> (unsigned)(i * 8));
    for (int i = 0; i < 4; i++) out[8  + i] = (uint8_t)(c0 >> (unsigned)(i * 8));
    for (int i = 0; i < 4; i++) out[12 + i] = (uint8_t)(d0 >> (unsigned)(i * 8));
}

/*
 * wgen_md5_seed: MD5(name) als 128-Bit-Seed lesen.
 * Die 16 Bytes werden als zwei big-endian int64 interpretiert (Bytes[0..7] = lo,
 * Bytes[8..15] = hi) — daher big-endian-Rekonstruktion.
 */
static void wgen_md5_seed(const char* name, int64_t* lo, int64_t* hi) {
    uint8_t h[16];
    wgen_md5((const uint8_t*)name, strlen(name), h);
    *lo = ((int64_t)h[0]  << 56) | ((int64_t)h[1]  << 48)
        | ((int64_t)h[2]  << 40) | ((int64_t)h[3]  << 32)
        | ((int64_t)h[4]  << 24) | ((int64_t)h[5]  << 16)
        | ((int64_t)h[6]  <<  8) |  (int64_t)h[7];
    *hi = ((int64_t)h[8]  << 56) | ((int64_t)h[9]  << 48)
        | ((int64_t)h[10] << 40) | ((int64_t)h[11] << 32)
        | ((int64_t)h[12] << 24) | ((int64_t)h[13] << 16)
        | ((int64_t)h[14] <<  8) |  (int64_t)h[15];
}

/* ── Positional Random Factory ── */
typedef struct { int64_t seed_lo; int64_t seed_hi; } wgen_pos_factory_t;

/* positional factory(): zwei nextLong()-Aufrufe liefern den Factory-Seed. */
static wgen_pos_factory_t wgen_prng_fork_positional(prng128_t* r) {
    wgen_pos_factory_t f;
    f.seed_lo = prng128_next_long(r);
    f.seed_hi = prng128_next_long(r);
    return f;
}

/*
 * wgen_pos_factory_from_hash: Hash-basierte Initialisierung — MD5(name) XOR Factory-Seeds
 * ergibt den initialen (lo, hi) fuer eine neue 128-Bit-PRNG-Instanz.
 */
static prng128_t wgen_pos_factory_from_hash(const wgen_pos_factory_t* f,
                                            const char* name) {
    int64_t hash_lo, hash_hi;
    wgen_md5_seed(name, &hash_lo, &hash_hi);
    prng128_t r;
    prng128_init(&r, hash_lo ^ f->seed_lo, hash_hi ^ f->seed_hi);
    return r;
}

/* ── Perlin-Noise (Gradient-Noise) ── */

/*
 * 16 Gradienten in SoA-Layout (Structure of Arrays).
 * Als drei getrennte Arrays statt als 16x3-Matrix abgelegt — erleichtert
 * vektorisierten Zugriff.
 */
static const int8_t GRAD_X[16] = { 1,-1, 1,-1, 1,-1, 1,-1, 0, 0, 0, 0, 1, 0,-1, 0};
static const int8_t GRAD_Y[16] = { 1, 1,-1,-1, 0, 0, 0, 0, 1,-1, 1,-1, 1,-1, 1,-1};
static const int8_t GRAD_Z[16] = { 0, 0, 0, 0, 1, 1,-1,-1, 1, 1,-1,-1, 0, 1, 0,-1};

typedef struct {
    uint8_t perm[256];   /* Fisher-Yates-Permutation */
    double  ox, oy, oz;  /* Weltraum-Offsets */
} wgen_perlin_t;

/* Gradient-Noise(random): Offsets setzen, dann Permutation per Fisher-Yates. */
static void wgen_perlin_init(wgen_perlin_t* p, prng128_t* r) {
    p->ox = prng128_next_double(r) * 256.0;
    p->oy = prng128_next_double(r) * 256.0;
    p->oz = prng128_next_double(r) * 256.0;
    int i;
    for (i = 0; i < 256; i++) p->perm[i] = (uint8_t)i;
    for (i = 0; i < 256; i++) {
        /* MC: nextInt(256-i) — Argument ist 256-i, nicht 256 */
        int j = i + prng128_next_int_bound(r, 256 - i);
        uint8_t tmp = p->perm[i];
        p->perm[i]  = p->perm[j];
        p->perm[j]  = tmp;
    }
}

/* permute: perm[x & 0xFF] als int */
static int wgen_permute(const wgen_perlin_t* p, int x) {
    return (int)p->perm[(unsigned)x & 0xFFu];
}

/*
 * gradient_dot: Skalarprodukt des Gradienten (h & 0xF) mit (x,y,z).
 * SoA-Zugriff: GRAD_X[h], GRAD_Y[h], GRAD_Z[h] direkt kombiniert.
 */
static double wgen_grad_dot(int h, double x, double y, double z) {
    int idx = h & 0xF;
    return (double)GRAD_X[idx] * x
         + (double)GRAD_Y[idx] * y
         + (double)GRAD_Z[idx] * z;
}

/*
 * wgen_wrap: faltet sehr grosse Koordinaten in einen endlichen Bereich,
 * um Float-Praezisionsverlust zu vermeiden. Eigene, runde Faltgrenze.
 */
static double wgen_wrap(double x) {
    if (x >= -20000000.0 && x < 20000000.0) return x;
    return x - floor(x / 40000000.0 + 0.5) * 40000000.0;
}

/*
 * smoothstep: kubische Glättungskurve (3 - 2t) * t^2.
 */
static double wgen_smoothstep(double t) { return (3.0 - 2.0 * t) * t * t; }

/* lerp und trilineare Interpolation */
static double wgen_lerp(double a, double b, double t) { return a + (b - a) * t; }

static double wgen_lerp3(double tx, double ty, double tz,
                          double v000, double v100,
                          double v010, double v110,
                          double v001, double v101,
                          double v011, double v111) {
    return wgen_lerp(
        wgen_lerp(wgen_lerp(v000, v100, tx), wgen_lerp(v010, v110, tx), ty),
        wgen_lerp(wgen_lerp(v001, v101, tx), wgen_lerp(v011, v111, tx), ty),
        tz);
}

/*
 * wgen_perlin_sample: Gradient-Noise.get(x, y, z).
 * Ablauf: erst alle 8 Permutationswerte berechnen, dann alle 8 Gradient-Dots,
 * dann trilinear interpolieren — klar getrennte Phasen.
 */
static double wgen_perlin_sample(const wgen_perlin_t* p,
                                  double _x, double _y, double _z) {
    /* Koordinaten in Permutationsraum abbilden */
    double x = wgen_wrap(_x) + p->ox;
    double y = wgen_wrap(_y) + p->oy;
    double z = wgen_wrap(_z) + p->oz;

    int ix = (int)floor(x);
    int iy = (int)floor(y);
    int iz = (int)floor(z);
    double rx = x - (double)ix;
    double ry = y - (double)iy;
    double rz = z - (double)iz;

    /* Phase 1: alle 8 Permutations-Hashes vorausberechnen */
    int px0  = wgen_permute(p, ix);
    int px1  = wgen_permute(p, ix + 1);
    int h00  = wgen_permute(p, px0 + iy);
    int h01  = wgen_permute(p, px0 + iy + 1);
    int h10  = wgen_permute(p, px1 + iy);
    int h11  = wgen_permute(p, px1 + iy + 1);
    int g000 = wgen_permute(p, h00 + iz);
    int g100 = wgen_permute(p, h10 + iz);
    int g010 = wgen_permute(p, h01 + iz);
    int g110 = wgen_permute(p, h11 + iz);
    int g001 = wgen_permute(p, h00 + iz + 1);
    int g101 = wgen_permute(p, h10 + iz + 1);
    int g011 = wgen_permute(p, h01 + iz + 1);
    int g111 = wgen_permute(p, h11 + iz + 1);

    /* Phase 2: alle 8 Gradient-Dots berechnen */
    double d000 = wgen_grad_dot(g000, rx,       ry,       rz      );
    double d100 = wgen_grad_dot(g100, rx - 1.0, ry,       rz      );
    double d010 = wgen_grad_dot(g010, rx,       ry - 1.0, rz      );
    double d110 = wgen_grad_dot(g110, rx - 1.0, ry - 1.0, rz      );
    double d001 = wgen_grad_dot(g001, rx,       ry,       rz - 1.0);
    double d101 = wgen_grad_dot(g101, rx - 1.0, ry,       rz - 1.0);
    double d011 = wgen_grad_dot(g011, rx,       ry - 1.0, rz - 1.0);
    double d111 = wgen_grad_dot(g111, rx - 1.0, ry - 1.0, rz - 1.0);

    /* Phase 3: smoothstep-Gewichte und trilineare Interpolation */
    double xa = wgen_smoothstep(rx);
    double ya = wgen_smoothstep(ry);
    double za = wgen_smoothstep(rz);
    return wgen_lerp3(xa, ya, za,
                      d000, d100, d010, d110,
                      d001, d101, d011, d111);
}

/* ── Layered-Noise (doppeltes Perlin, oktaviert + normalisiert) ── */

#define WGEN_MAX_OCTAVES 12

/* Zwei Perlin-Instanzen pro Oktave, plus Frequenz und Amplitude. */
typedef struct {
    wgen_perlin_t first;   /* "firstNoise" */
    wgen_perlin_t second;  /* "secondNoise" */
    double frequency;
    double amplitude;
} wgen_octave_t;

typedef struct {
    wgen_octave_t octaves[WGEN_MAX_OCTAVES];
    int    n_octaves;      /* Anzahl aktiver Oktaven (modifier != 0) */
    double normalization;  /* normalizationFactor */
} wgen_layered_noise_t;

/*
 * calc_noise_scale: berechnet den Normalisierungsfaktor.
 * Direkt inline mit der Varianz-Summe berechnet.
 *
 * INPUT_FACTOR_SIGMA = 0.27 (Standardabweichung einer einzelnen
 * Perlin-Sample-Verteilung; InputFactor / sqrt(2)).
 */
#define WGEN_INPUT_SIGMA 0.27
#define WGEN_INPUT_FACTOR 1.02

static double calc_noise_scale(double total_amplitude,
                                const wgen_octave_t* octaves, int n) {
    /* Varianz-Summe ueber alle aktiven Oktaven */
    double variance = 0.0;
    for (int i = 0; i < n; i++) {
        double dev = WGEN_INPUT_SIGMA * fabs(octaves[i].amplitude);
        variance += dev * dev;
    }
    double input_deviation = sqrt(variance);
    if (input_deviation == 0.0) return 0.0;
    /* Faktor sqrt(2) weil first + second kombiniert werden */
    double input_sum_dev  = input_deviation * sqrt(2.0);
    double target_dev     = total_amplitude * (1.0 / 3.0);
    return target_dev / input_sum_dev;
}

/*
 * wgen_layered_noise_init: erzeugt eine normalisierte, oktavierte doppeltes Perlin-Noise.
 *  - Kein normalize-Flag (immer normalisiert).
 *  - Flaches Array statt Oktav-Perlin-Wrapper.
 *  - amplitude_modifiers == NULL => alle Oktaven mit Faktor 1.0.
 */
static void wgen_layered_noise_init(
        wgen_layered_noise_t* nn,
        wgen_pos_factory_t*  parent_factory,
        const char*          noise_name,
        int                  base_octave,
        double               base_amplitude,
        int                  octave_count,
        const double*        amplitude_modifiers) {
    /* Basisrng aus dem Hash des Noise-Namens ableiten */
    prng128_t base_rng = wgen_pos_factory_from_hash(parent_factory, noise_name);
    /* Zwei unabhaengige Factories fuer first/second Perlin */
    wgen_pos_factory_t first_fac  = wgen_prng_fork_positional(&base_rng);
    wgen_pos_factory_t second_fac = wgen_prng_fork_positional(&base_rng);

    double amp       = base_amplitude;
    double freq      = pow(2.0, (double)base_octave);
    double total_amp = 0.0;
    nn->n_octaves    = 0;

    for (int i = 0; i < octave_count && nn->n_octaves < WGEN_MAX_OCTAVES; i++) {
        double mod = (amplitude_modifiers != NULL) ? amplitude_modifiers[i] : 1.0;
        if (mod == 0.0) { amp *= 0.5; freq *= 2.0; continue; }

        /* Oktav-Seed aus dem Oktav-Index ableiten */
        char octave_seed[32];
        snprintf(octave_seed, sizeof(octave_seed), "layer_%d", base_octave + i);

        prng128_t r1 = wgen_pos_factory_from_hash(&first_fac,  octave_seed);
        prng128_t r2 = wgen_pos_factory_from_hash(&second_fac, octave_seed);

        wgen_octave_t* oct = &nn->octaves[nn->n_octaves++];
        wgen_perlin_init(&oct->first,  &r1);
        wgen_perlin_init(&oct->second, &r2);
        oct->frequency = freq;
        oct->amplitude = amp * mod;
        total_amp     += fabs(amp * mod);

        amp  *= 0.5;
        freq *= 2.0;
    }

    nn->normalization = calc_noise_scale(total_amp, nn->octaves, nn->n_octaves);
}

/*
 * wgen_layered_noise_sample: berechnet den Sample-Wert an (x, y, z).
 * Die zweite Perlin-Instanz wird mit WGEN_INPUT_FACTOR leicht skaliert
 * (INPUT_FACTOR = 1.02).
 */
static double wgen_layered_noise_sample(const wgen_layered_noise_t* nn,
                                        double x, double y, double z) {
    double sum = 0.0;
    for (int i = 0; i < nn->n_octaves; i++) {
        const wgen_octave_t* o = &nn->octaves[i];
        double fx = x * o->frequency;
        double fy = y * o->frequency;
        double fz = z * o->frequency;
        double v1 = wgen_perlin_sample(&o->first, fx, fy, fz);
        double v2 = wgen_perlin_sample(&o->second,
                                        fx * WGEN_INPUT_FACTOR,
                                        fy * WGEN_INPUT_FACTOR,
                                        fz * WGEN_INPUT_FACTOR);
        sum += (v1 + v2) * o->amplitude;
    }
    return sum * nn->normalization;
}

/* ── wgen_gen: oeffentliche API ── */

struct wgen_gen {
    int64_t             seed;
    wgen_layered_noise_t cont;    /* Landmasse */
    wgen_layered_noise_t eros;    /* Relief */
    wgen_layered_noise_t temp;    /* Temperatur */
    wgen_layered_noise_t vege;    /* Flora */
    wgen_layered_noise_t rugged;   /* Rugged/Fold */
    wgen_layered_noise_t shift;   /* Position-Perturbation */
};

wgen_gen_t* wgen_gen_create(int64_t seed)
{
    wgen_gen_t* g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->seed = seed;

    /* world_rng: 128-Bit-PRNG(Seed auf 128 Bit erweitern(seed)) */
    int64_t lo, hi;
    wgen_seed_upgrade_128bit(seed, &lo, &hi);
    prng128_t world_rng;
    prng128_init(&world_rng, lo, hi);
    wgen_pos_factory_t world_fac = wgen_prng_fork_positional(&world_rng);

    /* Oktaven-Gewichte: eigene Werte, nahe am funktional Noetigen.
     * Null-Eintraege MUESSEN 0 bleiben (sie ueberspringen Oktaven). */
    static const double mods_cont[]  = {1.0, 0.95, 1.95, 2.05, 1.95, 1.05, 0.95, 1.0, 1.05};
    static const double mods_eros[]  = {1.0, 1.05, 0.0, 0.95, 1.05};
    static const double mods_temp[]  = {1.45, 0.0, 1.05, 0.0, 0.0, 0.0};
    static const double mods_vege[]  = {1.05, 0.95, 0.0, 0.0, 0.0, 0.0};
    static const double mods_ridge[] = {1.0, 1.95, 1.05, 0.0, 0.0, 0.0};
    static const double mods_shift[] = {1.05, 0.95, 1.0, 0.0};

    /* base_amplitude wird intern normalisiert → exakter Wert ohne Welt-Effekt;
     * eigene, gerundete Werte. */
    wgen_layered_noise_init(&g->cont,  &world_fac,
        "cavex:landmass", -9,  0.89,  9, mods_cont);
    wgen_layered_noise_init(&g->eros,  &world_fac,
        "cavex:relief",         -9,  1.06,  5, mods_eros);
    wgen_layered_noise_init(&g->temp,  &world_fac,
        "cavex:temperature",     -10, 1.25,  6, mods_temp);
    wgen_layered_noise_init(&g->vege,  &world_fac,
        "cavex:flora",      -8,  0.95,  6, mods_vege);
    wgen_layered_noise_init(&g->rugged, &world_fac,
        "cavex:rugged",           -7,  0.91,  6, mods_ridge);
    wgen_layered_noise_init(&g->shift, &world_fac,
        "cavex:offset",          -3,  0.94,  4, mods_shift);

    return g;
}

void wgen_gen_destroy(wgen_gen_t* g)
{
    free(g);
}

/* ── Worldgen-Seeding ── */

/*
 * wgen_decoration_seed: berechnet den Decoration-Seed fuer einen Chunk
 * aus (worldSeed, blockX, blockZ) mit blockX = chunkX*16, blockZ = chunkZ*16.
 */
static int64_t wgen_decoration_seed(int64_t world_seed, int chunk_x, int chunk_z) {
    legacy_rng_t r;
    legacy_set_seed(&r, world_seed);
    int64_t s = legacy_next_long(&r) | 1;
    int64_t t = legacy_next_long(&r) | 1;
    return ((int64_t)(chunk_x * 16) * s
          + (int64_t)(chunk_z * 16) * t)
         ^ world_seed;
}

/*
 * wgen_feature_rng: setzt den Feature-Seed fuer einen Dekorations-Schritt
 * aus (decorationSeed, step, featureIndex).
 */
static void wgen_feature_rng(legacy_rng_t* r, int64_t dec_seed,
                               int step, int feature_idx) {
    legacy_set_seed(r, dec_seed + (int64_t)step + (int64_t)10000 * feature_idx);
}

void wgen_gen_place_features(wgen_gen_t* g, struct server_chunk* sc,
                              int cx, int cz, int biome_id,
                              const uint8_t heights[16][16])
{
    (void)sc; (void)biome_id; (void)heights;
    int64_t dec_seed = wgen_decoration_seed(g->seed, cx, cz);
    legacy_rng_t r;

    /* Feature-Step 1: Baeume (Platzierung folgt in Task 9) */
    wgen_feature_rng(&r, dec_seed, 1, 0);

    /* Feature-Step 5: Kakteen */
    wgen_feature_rng(&r, dec_seed, 5, 0);

    /* Feature-Step 6: Schilf */
    wgen_feature_rng(&r, dec_seed, 6, 0);
}

void wgen_gen_climate(wgen_gen_t* g, double wx, double wz,
                      double* cont, double* eros,
                      double* temp, double* vege, double* rugged)
{
    /* shift_a: shift_noise.get(x*0.25, 0, z*0.25) * 4 */
    double sx = wgen_layered_noise_sample(&g->shift, wx * 0.25, 0.0, wz * 0.25) * 4.0;
    /* shift_b: shift_noise.get(z*0.25, x*0.25, 0) * 4 */
    double sz = wgen_layered_noise_sample(&g->shift, wz * 0.25, wx * 0.25, 0.0) * 4.0;
    double ex = (wx + sx) * 0.25;
    double ez = (wz + sz) * 0.25;

    if (cont)  *cont  = wgen_layered_noise_sample(&g->cont,  ex, 0.0, ez);
    if (eros)  *eros  = wgen_layered_noise_sample(&g->eros,  ex, 0.0, ez);
    if (temp)  *temp  = wgen_layered_noise_sample(&g->temp,  ex, 0.0, ez);
    if (vege)  *vege  = wgen_layered_noise_sample(&g->vege,  ex, 0.0, ez);
    if (rugged) *rugged = wgen_layered_noise_sample(&g->rugged, ex, 0.0, ez);
}

/* ── Terrain-Offset-Spline ── */

#define WGEN_COORD_CONT  0
#define WGEN_COORD_EROS  1
#define WGEN_COORD_RF    2

typedef struct {
    double location;
    double derivative;
    int    value_type;    /* 0=const, 1=sub-spline */
    double value_const;
    int    subspline_idx;
} wgen_spline_node_t;

typedef struct {
    int                  coord;
    int                  n_points;
    wgen_spline_node_t   points[12]; /* max 11 pts pro Spline */
} wgen_spline_t;

/* 53 splines, depth 3 (cont -> eros -> fold_curve) */
static const wgen_spline_t OFFSET_SPLINES[53] = {
    /* [0] root: over continents */
    { WGEN_COORD_CONT, 10, {
        { -1.1, 0, 0, 0.04, -1 },
        { -1.02, 0, 0, -0.22, -1 },
        { -0.51, 0, 0, -0.22, -1 },
        { -0.44, 0, 0, -0.12, -1 },
        { -0.18, 0, 0, -0.12, -1 },
        { -0.16, 0, 1, 0.0, 1 },
        { -0.15, 0, 1, 0.0, 9 },
        { -0.1, 0, 1, 0.0, 17 },
        {  0.25, 0, 1, 0.0, 25 },
        {  1, 0, 1, 0.0, 39 },
    } },
    /* [1] over relief (cont=-0.16) */
    { WGEN_COORD_EROS, 7, {
        { -0.85, 0, 1, 0.0, 2 },
        { -0.7, 0, 1, 0.0, 3 },
        { -0.4, 0, 1, 0.0, 4 },
        { -0.35, 0, 1, 0.0, 5 },
        { -0.1, 0, 1, 0.0, 6 },
        {  0.2, 0, 1, 0.0, 7 },
        {  0.7, 0, 1, 0.0, 8 },
    } },
    /* [2] over fold_curve */
    { WGEN_COORD_RF, 2, {
        { -1, 0.39, 0, -0.09, -1 },
        {  1, 0.39, 0, 0.67, -1 },
    } },
    /* [3] */
    { WGEN_COORD_RF, 2, {
        { -1, 0.38, 0, -0.11, -1 },
        {  1, 0.38, 0, 0.62, -1 },
    } },
    /* [4] */
    { WGEN_COORD_RF, 6, {
        { -1, 0, 0, -0.22, -1 },
        { -0.75, 0, 0, -0.22, -1 },
        { -0.65, 0, 0, 0, -1 },
        {  0.6, 0, 0, 0, -1 },
        {  0.61, 0.25, 0, 0, -1 },
        {  1, 0.25, 0, 0.1, -1 },
    } },
    /* [5] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.29, -1 },
        { -0.4, 0, 0, 0.05, -1 },
        {  0, 0, 0, 0.05, -1 },
        {  0.4, 0, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [6] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.15, -1 },
        { -0.4, 0, 0, 0, -1 },
        {  0, 0, 0, 0, -1 },
        {  0.4, 0.1, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [7] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.15, -1 },
        { -0.4, 0, 0, 0, -1 },
        {  0, 0, 0, 0, -1 },
        {  0.4, 0, 0, 0, -1 },
        {  1, 0, 0, 0, -1 },
    } },
    /* [8] */
    { WGEN_COORD_RF, 5, {
        { -1, 0, 0, -0.02, -1 },
        { -0.4, 0, 0, -0.03, -1 },
        {  0, 0, 0, -0.03, -1 },
        {  0.4, 0.06, 0, 0, -1 },
        {  1, 0, 0, 0, -1 },
    } },
    /* [9] over relief (cont=-0.15) */
    { WGEN_COORD_EROS, 7, {
        { -0.85, 0, 1, 0.0, 10 },
        { -0.7, 0, 1, 0.0, 11 },
        { -0.4, 0, 1, 0.0, 12 },
        { -0.35, 0, 1, 0.0, 13 },
        { -0.1, 0, 1, 0.0, 14 },
        {  0.2, 0, 1, 0.0, 15 },
        {  0.7, 0, 1, 0.0, 16 },
    } },
    /* [10] */
    { WGEN_COORD_RF, 2, {
        { -1, 0.39, 0, -0.09, -1 },
        {  1, 0.39, 0, 0.67, -1 },
    } },
    /* [11] */
    { WGEN_COORD_RF, 2, {
        { -1, 0.38, 0, -0.11, -1 },
        {  1, 0.38, 0, 0.62, -1 },
    } },
    /* [12] */
    { WGEN_COORD_RF, 6, {
        { -1, 0, 0, -0.22, -1 },
        { -0.75, 0, 0, -0.22, -1 },
        { -0.65, 0, 0, 0, -1 },
        {  0.6, 0, 0, 0, -1 },
        {  0.61, 0.25, 0, 0, -1 },
        {  1, 0.25, 0, 0.1, -1 },
    } },
    /* [13] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.29, -1 },
        { -0.4, 0, 0, 0.05, -1 },
        {  0, 0, 0, 0.05, -1 },
        {  0.4, 0, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [14] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.15, -1 },
        { -0.4, 0, 0, 0, -1 },
        {  0, 0, 0, 0, -1 },
        {  0.4, 0.1, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [15] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.15, -1 },
        { -0.4, 0, 0, 0, -1 },
        {  0, 0, 0, 0, -1 },
        {  0.4, 0, 0, 0, -1 },
        {  1, 0, 0, 0, -1 },
    } },
    /* [16] */
    { WGEN_COORD_RF, 5, {
        { -1, 0, 0, -0.02, -1 },
        { -0.4, 0, 0, -0.03, -1 },
        {  0, 0, 0, -0.03, -1 },
        {  0.4, 0.06, 0, 0, -1 },
        {  1, 0, 0, 0, -1 },
    } },
    /* [17] over relief (cont=-0.10) */
    { WGEN_COORD_EROS, 7, {
        { -0.85, 0, 1, 0.0, 18 },
        { -0.7, 0, 1, 0.0, 19 },
        { -0.4, 0, 1, 0.0, 20 },
        { -0.35, 0, 1, 0.0, 21 },
        { -0.1, 0, 1, 0.0, 22 },
        {  0.2, 0, 1, 0.0, 23 },
        {  0.7, 0, 1, 0.0, 24 },
    } },
    /* [18] */
    { WGEN_COORD_RF, 2, {
        { -1, 0.39, 0, -0.09, -1 },
        {  1, 0.39, 0, 0.67, -1 },
    } },
    /* [19] */
    { WGEN_COORD_RF, 2, {
        { -1, 0.38, 0, -0.11, -1 },
        {  1, 0.38, 0, 0.62, -1 },
    } },
    /* [20] */
    { WGEN_COORD_RF, 6, {
        { -1, 0, 0, -0.22, -1 },
        { -0.75, 0, 0, -0.22, -1 },
        { -0.65, 0, 0, 0, -1 },
        {  0.6, 0, 0, 0, -1 },
        {  0.61, 0.25, 0, 0, -1 },
        {  1, 0.25, 0, 0.1, -1 },
    } },
    /* [21] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.24, -1 },
        { -0.4, 0, 0, 0.05, -1 },
        {  0, 0, 0, 0.05, -1 },
        {  0.4, 0, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [22] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0.01, 0, 0, -1 },
        {  0, 0.01, 0, 0, -1 },
        {  0.4, 0.09, 0, 0.05, -1 },
        {  1, 0.01, 0, 0.06, -1 },
    } },
    /* [23] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [24] */
    { WGEN_COORD_RF, 5, {
        { -1, 0, 0, -0.02, -1 },
        { -0.4, 0, 0, -0.03, -1 },
        {  0, 0, 0, -0.03, -1 },
        {  0.4, 0.12, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [25] over relief (cont=0.25) */
    { WGEN_COORD_EROS, 11, {
        { -0.85, 0, 1, 0.0, 26 },
        { -0.7, 0, 1, 0.0, 27 },
        { -0.4, 0, 1, 0.0, 28 },
        { -0.35, 0, 1, 0.0, 29 },
        { -0.1, 0, 1, 0.0, 30 },
        {  0.2, 0, 1, 0.0, 31 },
        {  0.4, 0, 1, 0.0, 32 },
        {  0.45, 0, 1, 0.0, 33 },
        {  0.55, 0, 1, 0.0, 35 },
        {  0.58, 0, 1, 0.0, 37 },
        {  0.7, 0, 1, 0.0, 38 },
    } },
    /* [26] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.2, -1 },
        {  0, 0.52, 0, 0.7, -1 },
        {  1, 0.52, 0, 1.2, -1 },
    } },
    /* [27] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.2, -1 },
        {  0, 0.44, 0, 0.44, -1 },
        {  1, 0.44, 0, 0.86, -1 },
    } },
    /* [28] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.2, -1 },
        {  0, 0.39, 0, 0.3, -1 },
        {  1, 0.39, 0, 0.68, -1 },
    } },
    /* [29] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.24, -1 },
        { -0.4, 0, 0, 0.34, -1 },
        {  0, 0, 0, 0.34, -1 },
        {  0.4, 0, 0, 0.34, -1 },
        {  1, 0.05, 0, 0.41, -1 },
    } },
    /* [30] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0.07, 0, 0.01, -1 },
        {  0, 0.07, 0, 0.02, -1 },
        {  0.4, 0.66, 0, 0.34, -1 },
        {  1, 0.05, 0, 0.41, -1 },
    } },
    /* [31] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [32] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [33] nested rf spline (eros=0.45) */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, -0.1, -1 },
        { -0.4, 0, 1, 0.0, 34 },
        {  0, 0, 0, 0.17, -1 },
    } },
    /* [34] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [35] nested rf spline (eros=0.55) */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, -0.1, -1 },
        { -0.4, 0, 1, 0.0, 36 },
        {  0, 0, 0, 0.17, -1 },
    } },
    /* [36] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [37] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.1, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [38] */
    { WGEN_COORD_RF, 5, {
        { -1, 0, 0, -0.02, -1 },
        { -0.4, 0, 0, -0.03, -1 },
        {  0, 0, 0, -0.03, -1 },
        {  0.4, 0.12, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [39] over relief (cont=1.0) */
    { WGEN_COORD_EROS, 11, {
        { -0.85, 0, 1, 0.0, 40 },
        { -0.7, 0, 1, 0.0, 41 },
        { -0.4, 0, 1, 0.0, 42 },
        { -0.35, 0, 1, 0.0, 43 },
        { -0.1, 0, 1, 0.0, 44 },
        {  0.2, 0, 1, 0.0, 45 },
        {  0.4, 0, 1, 0.0, 46 },
        {  0.45, 0, 1, 0.0, 47 },
        {  0.55, 0, 1, 0.0, 49 },
        {  0.58, 0, 1, 0.0, 51 },
        {  0.7, 0, 1, 0.0, 52 },
    } },
    /* [40] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.34, -1 },
        {  0, 0.58, 0, 0.9, -1 },
        {  1, 0.58, 0, 1.46, -1 },
    } },
    /* [41] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.2, -1 },
        {  0, 0.46, 0, 0.53, -1 },
        {  1, 0.46, 0, 0.97, -1 },
    } },
    /* [42] */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, 0.2, -1 },
        {  0, 0.46, 0, 0.53, -1 },
        {  1, 0.46, 0, 0.97, -1 },
    } },
    /* [43] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.2, -1 },
        { -0.4, 0, 0, 0.49, -1 },
        {  0, 0, 0, 0.49, -1 },
        {  0.4, 0, 0, 0.49, -1 },
        {  1, 0.07, 0, 0.58, -1 },
    } },
    /* [44] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0.1, 0, 0.01, -1 },
        {  0, 0.1, 0, 0.03, -1 },
        {  0.4, 0.94, 0, 0.49, -1 },
        {  1, 0.07, 0, 0.58, -1 },
    } },
    /* [45] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [46] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [47] nested rf spline (eros=0.45, cont=1.0) */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, -0.05, -1 },
        { -0.4, 0, 1, 0.0, 48 },
        {  0, 0, 0, 0.17, -1 },
    } },
    /* [48] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [49] nested rf spline (eros=0.55, cont=1.0) */
    { WGEN_COORD_RF, 3, {
        { -1, 0, 0, -0.05, -1 },
        { -0.4, 0, 1, 0.0, 50 },
        {  0, 0, 0, 0.17, -1 },
    } },
    /* [50] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [51] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.5, 0, -0.05, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
    /* [52] */
    { WGEN_COORD_RF, 5, {
        { -1, 0.02, 0, -0.02, -1 },
        { -0.4, 0, 0, 0.01, -1 },
        {  0, 0, 0, 0.01, -1 },
        {  0.4, 0.04, 0, 0.03, -1 },
        {  1, 0.05, 0, 0.1, -1 },
    } },
};

/* ── Kubische Hermite-Interpolation ── */
/*
 * wgen_spline_hermite: Standard-Hermite-Polynom auf t∈[0,1].
 * d0 und d1 sind bereits mit dem Abstand (location[i+1]-location[i]) skaliert,
 * so dass die Ableitungen in den originalen Units (Wert/Location) angegeben sind.
 */
static double wgen_spline_hermite(double p0, double p1, double d0, double d1,
                                   double t) {
    double t2 = t * t, t3 = t2 * t;
    return (2.0*t3 - 3.0*t2 + 1.0)*p0 + (t3 - 2.0*t2 + t)*d0
         + (-2.0*t3 + 3.0*t2)     *p1 + (t3 - t2)         *d1;
}

/*
 * wgen_get_coord: gibt den für diesen Spline-Knoten relevanten Klimawert zurück.
 */
static double wgen_get_coord(int coord, double c, double e, double r) {
    if (coord == WGEN_COORD_CONT) return c;
    if (coord == WGEN_COORD_EROS) return e;
    return r; /* WGEN_COORD_RF */
}

/*
 * wgen_eval_spline: wertet Spline[idx] rekursiv aus.
 * Lineare Extrapolation außerhalb des Bereichs, Hermite-Interpolation dazwischen.
 * Ableitungen werden mit dem Stützpunkt-Abstand skaliert.
 */
static double wgen_eval_spline(const wgen_spline_t* splines, int idx,
                                double c, double e, double r) {
    const wgen_spline_t* s = &splines[idx];
    double x = wgen_get_coord(s->coord, c, e, r);
    int n = s->n_points;
    if (n == 0) return 0.0;

    /* Wert eines einzelnen Punktes auflösen (ggf. rekursiv) */
#define EVAL_PT(i) ((s->points[i].value_type == 0) \
    ? s->points[i].value_const \
    : wgen_eval_spline(splines, s->points[i].subspline_idx, c, e, r))

    /* Unterhalb des ersten Punktes: lineare Extrapolation */
    if (x <= s->points[0].location) {
        double v = EVAL_PT(0);
        return v + s->points[0].derivative * (x - s->points[0].location);
    }
    /* Oberhalb des letzten Punktes: lineare Extrapolation */
    int last = n - 1;
    if (x >= s->points[last].location) {
        double v = EVAL_PT(last);
        return v + s->points[last].derivative * (x - s->points[last].location);
    }
    /* Zwischen zwei Stützpunkten: kubische Hermite-Interpolation */
    for (int i = 0; i < last; i++) {
        if (x > s->points[i + 1].location) continue;
        double span = s->points[i + 1].location - s->points[i].location;
        double t    = (x - s->points[i].location) / span;
        double v0   = EVAL_PT(i);
        double v1   = EVAL_PT(i + 1);
        /* Ableitungen mit Abstand skalieren */
        double d0   = s->points[i    ].derivative * span;
        double d1   = s->points[i + 1].derivative * span;
        return wgen_spline_hermite(v0, v1, d0, d1, t);
    }
#undef EVAL_PT
    return 0.0;
}

/* ── peaksAndValleys und wgen_gen_offset ── */

/*
 * wgen_fold_factor: fold_curve aus dem rugged-Wert berechnen.
 * -3.0 * (||r| - 0.6666667| - 0.33333334)
 * Wertebereich: ca. -1.0 (bei r=0) bis ca. +1.0 (bei r=±2/3)
 */
static double wgen_fold_factor(double r) {
    return -3.0 * (fabs(fabs(r) - 0.6666667) - 0.33333334);
}

/*
 * wgen_gen_offset: Terrain-Offset aus Spline-Tabelle berechnen.
 * offset = Basis-Offset + spline(cont, eros, fold_factor(rugged))
 * Typischer Wertebereich: ca. -0.7 bis +0.7
 */
#ifdef WGEN_TESTING
static double wgen_gen_offset(wgen_gen_t* g, double wx, double wz) {
    double cont, eros, rugged;
    wgen_gen_climate(g, wx, wz, &cont, &eros, NULL, NULL, &rugged);
    double rf = wgen_fold_factor(rugged);
    return -0.5 + wgen_eval_spline(OFFSET_SPLINES, 0, cont, eros, rf);
}
#endif

/*
 * wgen_offset_to_cavex_height: rechnet den Terrain-Offset in eine CavEX-Hoehe um.
 *
 * Welthoehe:       y_mc = (offset + 1.5) * 128 - 64
 * Fixpunkte:       y_mc=63  (Meeresspiegel) → cavex=63
 *                  y_mc=218 (Bergspitze, offset≈0.7) → cavex=110
 * Steigung:        (110-63)/(218-63) = 47/155 ≈ 0.30323
 * Ausgabe:         ganzzahlig, geklemmt auf [1, 123]
 *                  (max 123: Platz fuer Oberflaechenblock + Baum oben)
 */
static int wgen_offset_to_cavex_height(double offset) {
    double y_mc = (offset + 1.5) * 128.0 - 64.0;
    double h;
    if(y_mc < 30.0) {
        /* Tiefer Ozean: sehr flach skaliert */
        h = 63.0 + (y_mc - 63.0) * 0.12;
    } else if(y_mc < 63.0) {
        /* Flacher Ozean / Strand-Übergang: sanfter Anstieg zum Meeresspiegel */
        double t = (y_mc - 30.0) / (63.0 - 30.0); /* 0..1 */
        double slope = 0.12 + t * (0.28 - 0.12);   /* 0.12 → 0.28 */
        h = 63.0 + (y_mc - 63.0) * slope;
    } else if(y_mc < 80.0) {
        /* Küstenebene: leicht steiler als Ozean */
        h = 63.0 + (y_mc - 63.0) * 0.28;
    } else if(y_mc < 140.0) {
        /* Hügelland */
        h = 63.0 + (y_mc - 63.0) * 0.30323;
    } else {
        /* Sehr bergig: progressiv steilere Steigung → hohe, steile Berge */
        double base = 63.0 + (140.0 - 63.0) * 0.30323;
        h = base + (y_mc - 140.0) * 0.75;
    }
    int hi = (int)(h + 0.5);
    if(hi < 1)   hi = 1;
    if(hi > 123) hi = 123;
    return hi;
}

static int wgen_gen_world_height_raw(wgen_gen_t* g, double wx, double wz) {
    double cont, eros, rugged;
    wgen_gen_climate(g, wx, wz, &cont, &eros, NULL, NULL, &rugged);
    double rf = wgen_fold_factor(rugged);
    int base_height = wgen_offset_to_cavex_height(
        -0.5 + wgen_eval_spline(OFFSET_SPLINES, 0, cont, eros, rf));

    if(base_height >= 78) {
        double high = wgen_layered_noise_sample(&g->rugged, wx * 1.10, 0.0, wz * 1.10);
        double dir = wgen_layered_noise_sample(&g->shift, wx * 0.08, 0.0, wz * 0.08);
        double wave = sin(wx * (0.55 + dir * 0.20) + wz * (0.30 - dir * 0.18)
                          + high * 4.0);
        double strength = ((double)base_height - 78.0) / 28.0;
        if(strength < 0.0) strength = 0.0;
        if(strength > 1.0) strength = 1.0;
        if(rf > 0.35) strength *= 0.55 + (rf - 0.35) * 0.90;
        if(strength > 1.0) strength = 1.0;
        if(wave > -0.15) {
            int lift = (int)((wave + 0.15) / 1.15 * 7.0 * strength + 0.5);
            if(lift < 0) lift = 0;
            if(lift > 7) lift = 7;
            base_height += lift;
        }
    }

    if(base_height < 1) base_height = 1;
    if(base_height > 123) base_height = 123;
    return base_height;
}

int wgen_gen_world_height(wgen_gen_t* g, int wx, int wz) {
    return wgen_gen_world_height_raw(g, (double)wx, (double)wz);
}

void wgen_gen_surface_heights_from_raw(const uint8_t* raw, int raw_stride,
                                       int raw_offset, uint8_t h[16][16])
{
    /* Glättungspass: reduziere steile Übergänge (>6 Blöcke zu Nachbarn)
     * mit gewichtetem Durchschnitt — Flüsse formen sanftere Ufer */
    for (int lx = 0; lx < 16; lx++) {
        for (int lz = 0; lz < 16; lz++) {
            int rx = lx + raw_offset, rz = lz + raw_offset;
            int center = raw[rx + rz * raw_stride];
            /* Berge (hoch gelegen) NICHT glätten → steile Klippen bleiben.
             * Nur Tiefland/Küsten glätten (sanfte Flussufer). */
            if (center >= 86) {
                int v = center;
                if (v < 1)   v = 1;
                if (v > 123) v = 123;
                h[lx][lz] = (uint8_t)v;
                continue;
            }
            int sum = center * 4; /* Mittelwert bekommt 4× Gewicht */
            int count = 4;
            /* 4 direkte Nachbarn */
            int neighbors[4] = {
                raw[(rx-1) + rz * raw_stride], raw[(rx+1) + rz * raw_stride],
                raw[rx + (rz-1) * raw_stride], raw[rx + (rz+1) * raw_stride]
            };
            for (int i = 0; i < 4; i++) {
                int diff = neighbors[i] - center;
                if (diff < 0) diff = -diff;
                if (diff > 6) {
                    /* Steiler Übergang: Nachbar mit einbeziehen */
                    sum += neighbors[i];
                    count++;
                } else {
                    sum += center;
                    count++;
                }
            }
            int smoothed = sum / count;
            if (smoothed < 1)   smoothed = 1;
            if (smoothed > 123) smoothed = 123;
            h[lx][lz] = (uint8_t)smoothed;
        }
    }
}

void wgen_gen_surface_heights(wgen_gen_t* g, int cx, int cz,
                               uint8_t h[16][16])
{
    int base_x = cx * 16;
    int base_z = cz * 16;
    uint8_t raw[20][20];
    for (int lx = 0; lx < 20; lx++) {
        for (int lz = 0; lz < 20; lz++) {
            double wx = (double)(base_x + lx - 2);
            double wz = (double)(base_z + lz - 2);
            raw[lx][lz] = (uint8_t)wgen_gen_world_height_raw(g, wx, wz);
        }
    }
    wgen_gen_surface_heights_from_raw(&raw[0][0], 20, 2, h);
}

/* ── Testhelfer (nur mit -DWGEN_TESTING=1 kompiliert) ── */
#ifdef WGEN_TESTING
/*
 * wgen_layered_noise_test_cont: Testhelfer fuer Layered-Noise.
 * Baut landmass-Noise fuer den gegebenen seed auf und samplet bei (x, 0, z).
 * Parameter entsprechen den Noise-Parametern von LANDMASS.
 */
double wgen_layered_noise_test_cont(int64_t seed, double x, double z) {
    int64_t lo, hi;
    wgen_seed_upgrade_128bit(seed, &lo, &hi);
    prng128_t world_rng;
    prng128_init(&world_rng, lo, hi);
    wgen_pos_factory_t world_fac = wgen_prng_fork_positional(&world_rng);
    /* Noise-Parameter von LANDMASS */
    static const double mods[] = {1,1,2,2,2,1,1,1,1};
    wgen_layered_noise_t nn;
    wgen_layered_noise_init(&nn, &world_fac, "cavex:landmass",
                           -9, 0.8880832896205223, 9, mods);
    return wgen_layered_noise_sample(&nn, x, 0.0, z);
}

int64_t wgen_prng_next_long_test(int64_t seed) {
    int64_t lo, hi;
    wgen_seed_upgrade_128bit(seed, &lo, &hi);
    prng128_t r;
    prng128_init(&r, lo, hi);
    return prng128_next_long(&r);
}

int64_t wgen_legacy_next_long_test(int64_t seed) {
    legacy_rng_t r;
    legacy_set_seed(&r, seed);
    return legacy_next_long(&r);
}

void wgen_seed128_test(int64_t seed, int64_t* lo, int64_t* hi) {
    wgen_seed_upgrade_128bit(seed, lo, hi);
}

/*
 * wgen_perlin_sample_test: Testhelfer fuer Perlin-Noise.
 * Initialisiert 128-Bit-PRNG mit (seed_lo, 0x6A09E667F3BCC909 = SILVER_RATIO_64),
 * baut daraus eine Perlin-Noise auf und samplet bei (x, y, z).
 */
double wgen_perlin_sample_test(int64_t seed_lo, double x, double y, double z) {
    prng128_t r;
    prng128_init(&r, seed_lo, (int64_t)0x6A09E667F3BCC909LL);
    wgen_perlin_t p;
    wgen_perlin_init(&p, &r);
    return wgen_perlin_sample(&p, x, y, z);
}

double wgen_gen_offset_test(wgen_gen_t* g, double wx, double wz) {
    return wgen_gen_offset(g, wx, wz);
}

int64_t wgen_decoration_seed_test(int64_t world_seed, int chunk_x, int chunk_z) {
    return wgen_decoration_seed(world_seed, chunk_x, chunk_z);
}
#endif /* WGEN_TESTING */
