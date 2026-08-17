/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If we meet some day, and you think this stuff is worth
 * it, you can buy us a beer in return.
 * -----------------------------------------------------------------------------
 */
#include "nbt.h"

#include "buffer.h"
#include "list.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/*
 * zlib resources:
 *
 * http://zlib.net/manual.html
 * http://zlib.net/zlib_how.html
 * http://www.gzip.org/zlib/zlib_faq.html
 */

/* The number of bytes to process at a time */
#define CHUNK_SIZE 4096

/* --- Vorab reservierte Arena für den zlib-Deflate-Zustand --------------------
 * Warum das nötig ist: das Komprimieren beim Chunk-Speichern braucht ~256KB
 * Arbeitsspeicher (Fenster + Hash-Tabellen). Mit zalloc=Z_NULL holt zlib die
 * per malloc — und unter Speicherdruck (voller MEM2) scheitert das, der Chunk
 * lässt sich nicht speichern -> Datenverlust oder Deadlock. Das eigentliche
 * SD-Schreiben braucht dagegen fast keinen RAM.
 * Lösung: EINE feste Arena, die pro Save wiederverwendet wird. Saves laufen
 * sequenziell im Server-Thread, also genügt ein Puffer. Damit braucht das
 * Komprimieren nie wieder malloc und kann nicht mehr an RAM-Mangel scheitern. */
#define NBT_ZARENA_SIZE (320 * 1024)
static unsigned char nbt_zarena[NBT_ZARENA_SIZE];
static size_t nbt_zarena_off;

static voidpf nbt_zarena_alloc(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    size_t need = ((size_t)items * (size_t)size + 15u) & ~(size_t)15u;
    if(nbt_zarena_off + need > NBT_ZARENA_SIZE)
        return Z_NULL; /* Arena zu klein -> zlib fällt sauber auf Fehler zurück */
    void* p = nbt_zarena + nbt_zarena_off;
    nbt_zarena_off += need;
    return p;
}

static void nbt_zarena_free(voidpf opaque, voidpf address) {
    (void)opaque;
    (void)address; /* Bump-Allokator: alles wird pro Save auf einmal freigegeben */
}

/* --- NBT-Parse-Arena (churn-freies Chunk-Laden) -----------------------------
 * Beim Erkunden generierter Welten wird pro Disk-Chunk der Rohpuffer, der
 * dekomprimierte Puffer, der NBT-Baum und dessen Byte-Arrays alloziert und
 * gleich wieder freigegeben. Diese grossen transienten malloc/free lassen den
 * newlib-Heap in die MEM2-Arena wachsen (die er NIE zurueckgibt) -> mem2arena
 * (Anzeige oben rechts) kriecht auf 0, dann laden keine Chunks mehr.
 * Loesung: waehrend des Chunk-Parsens laufen ALLE NBT-Allokationen durch einen
 * festen Bump-Puffer; nbt_free wird zum No-Op, die Arena wird pro Chunk per
 * nbt_arena_begin() zurueckgesetzt -> NULL Heap-Wachstum, mem2arena bleibt
 * konstant. Chunk-Loads laufen sequenziell im Server-Thread -> ein Puffer
 * genuegt. Ist das Flag aus, verhaelt sich alles exakt wie zuvor (echtes
 * malloc/free), damit level.dat & Co. unveraendert bleiben. */
#define NBT_PARENA_SIZE (1024 * 1024)
static unsigned char nbt_parena[NBT_PARENA_SIZE];
static size_t nbt_parena_off;
static bool nbt_parena_active = false;

void nbt_arena_begin(void) {
    nbt_parena_off = 0;
    nbt_parena_active = true;
}

void nbt_arena_end(void) {
    nbt_parena_active = false;
}

bool nbt_arena_is_active(void) {
    return nbt_parena_active;
}

void* nbt_malloc(size_t n) {
    if(!nbt_parena_active)
        return malloc(n);
    size_t need = (n + 15u) & ~(size_t)15u;
    if(nbt_parena_off + need > NBT_PARENA_SIZE)
        return NULL; /* Arena zu klein -> Parse scheitert sauber (NULL-Check) */
    void* p = nbt_parena + nbt_parena_off;
    nbt_parena_off += need;
    return p;
}

void* nbt_realloc(void* old, size_t oldn, size_t n) {
    if(!nbt_parena_active)
        return realloc(old, n);
    /* Bump-Arena kann nicht in-place wachsen -> neu belegen + kopieren. Der alte
     * Block bleibt in der Arena liegen (wird pro Chunk eh zurueckgesetzt). */
    void* p = nbt_malloc(n);
    if(p && old && oldn)
        memcpy(p, old, oldn < n ? oldn : n);
    return p;
}

void nbt_free_mem(void* p) {
    if(nbt_parena_active)
        return; /* Bump-Arena: Freigabe pauschal per nbt_arena_begin()-Reset */
    free(p);
}

/*
 * Reads a whole file into a buffer. Returns a NULL buffer and sets errno on
 * error.
 */
static struct buffer read_file(FILE* fp)
{
    struct buffer ret = BUFFER_INIT;

    size_t bytes_read;

    do {
        if(buffer_reserve(&ret, ret.len + CHUNK_SIZE))
            return (errno = NBT_EMEM), buffer_free(&ret), BUFFER_INIT;

        bytes_read = fread(ret.data + ret.len, 1, CHUNK_SIZE, fp);
        ret.len += bytes_read;

        if(ferror(fp))
            return (errno = NBT_EIO), buffer_free(&ret), BUFFER_INIT;

    } while(!feof(fp));

    return ret;
}

static nbt_status write_file(FILE* fp, const void* data, size_t len)
{
    const char* cdata = data;
    size_t bytes_left = len;

    size_t bytes_written;

    do {
        bytes_written = fwrite(cdata, 1, bytes_left, fp);
        if(ferror(fp)) return NBT_EIO;

        bytes_left -= bytes_written;
        cdata      += bytes_written;

    } while(bytes_left > 0);

    return NBT_OK;
}

/*
 * Reads in uncompressed data and returns a buffer with the $(strat)-compressed
 * data within. Returns a NULL buffer on failure, and sets errno appropriately.
 */
static struct buffer __compress(const void* mem,
                                size_t len,
                                nbt_compression_strategy strat)
{
    struct buffer ret = BUFFER_INIT;

    errno = NBT_OK;

    /* Deflate-Zustand aus der vorab reservierten Arena bedienen (kein malloc). */
    nbt_zarena_off = 0;

    z_stream stream = {
        .zalloc   = nbt_zarena_alloc,
        .zfree    = nbt_zarena_free,
        .opaque   = Z_NULL,
        .next_in  = (void*)mem,
        .avail_in = len
    };

    /* "The default value is 15"... */
    int windowbits = 15;

    /* ..."Add 16 to windowBits to write a simple gzip header and trailer around
     * the compressed data instead of a zlib wrapper." */
    if(strat == STRAT_GZIP)
        windowbits += 16;

    if(deflateInit2(&stream,
                    Z_DEFAULT_COMPRESSION,
                    Z_DEFLATED,
                    windowbits,
                    8,
                    Z_DEFAULT_STRATEGY
                   ) != Z_OK)
    {
        errno = NBT_EZ;
        return BUFFER_INIT;
    }

    assert(stream.avail_in == len); /* I'm not sure if zlib will clobber this */

    do {
        if(buffer_reserve(&ret, ret.len + CHUNK_SIZE))
        {
            errno = NBT_EMEM;
            goto compression_error;
        }

        stream.next_out  = ret.data + ret.len;
        stream.avail_out = CHUNK_SIZE;

        if(deflate(&stream, Z_FINISH) == Z_STREAM_ERROR)
            goto compression_error;

        ret.len += CHUNK_SIZE - stream.avail_out;

    } while(stream.avail_out == 0);

    (void)deflateEnd(&stream);
    return ret;

compression_error:
    if(errno == NBT_OK)
        errno = NBT_EZ;

    (void)deflateEnd(&stream);
    buffer_free(&ret);
    return BUFFER_INIT;
}

/*
 * Reads in zlib-compressed data, and returns a buffer with the decompressed
 * data within. Returns a NULL buffer on failure, and sets errno appropriately.
 */
static struct buffer __decompress(const void* mem, size_t len)
{
    struct buffer ret = BUFFER_INIT;

    errno = NBT_OK;

    z_stream stream = {
        .zalloc   = Z_NULL,
        .zfree    = Z_NULL,
        .opaque   = Z_NULL,
        .next_in  = (void*)mem,
        .avail_in = len
    };

    /* "Add 32 to windowBits to enable zlib and gzip decoding with automatic
     * header detection" */
    if(inflateInit2(&stream, 15 + 32) != Z_OK)
    {
        errno = NBT_EZ;
        return BUFFER_INIT;
    }

    int zlib_ret;

    do {
        if(buffer_reserve(&ret, ret.len + CHUNK_SIZE))
        {
            errno = NBT_EMEM;
            goto decompression_error;
        }

        stream.avail_out = CHUNK_SIZE;
        stream.next_out  = (unsigned char*)ret.data + ret.len;

        switch((zlib_ret = inflate(&stream, Z_NO_FLUSH)))
        {
        case Z_MEM_ERROR:
            errno = NBT_EMEM;
            /* fall through */

        case Z_DATA_ERROR: case Z_NEED_DICT:
            goto decompression_error;

        default:
            /* update our buffer length to reflect the new data */
            ret.len += CHUNK_SIZE - stream.avail_out;
        }

    } while(stream.avail_out == 0);

    /*
     * If we're at the end of the input data, we'd sure as hell be at the end
     * of the zlib stream.
     */
    if(zlib_ret != Z_STREAM_END) goto decompression_error;
    (void)inflateEnd(&stream);

    return ret;

decompression_error:
    if(errno == NBT_OK)
        errno = NBT_EZ;

    (void)inflateEnd(&stream);

    buffer_free(&ret);
    return BUFFER_INIT;
}

/*
 * No incremental parsing goes on. We just dump the whole compressed file into
 * memory then pass the job off to nbt_parse_chunk.
 */
nbt_node* nbt_parse_file(FILE* fp)
{
    errno = NBT_OK;

    struct buffer compressed = read_file(fp);

    if(compressed.data == NULL)
        return NULL;

    nbt_node* ret = nbt_parse_compressed(compressed.data, compressed.len);

    buffer_free(&compressed);
    return ret;
}

nbt_node* nbt_parse_path(const char* filename)
{
    FILE* fp = fopen(filename, "rb");

    if(fp == NULL)
    {
        errno = NBT_EIO;
        return NULL;
    }

    nbt_node* r = nbt_parse_file(fp);
    fclose(fp);
    return r;
}

nbt_node* nbt_parse_compressed(const void* chunk_start, size_t length)
{
    struct buffer decompressed = __decompress(chunk_start, length);

    if(decompressed.data == NULL)
        return NULL;

    nbt_node* ret = nbt_parse(decompressed.data, decompressed.len);

    buffer_free(&decompressed);
    return ret;
}

/*
 * Once again, all we're doing is handing the actual compression off to
 * nbt_dump_compressed, then dumping it into the file.
 */
nbt_status nbt_dump_file(const nbt_node* tree, FILE* fp, nbt_compression_strategy strat)
{
    struct buffer compressed = nbt_dump_compressed(tree, strat);

    if(compressed.data == NULL)
        return (nbt_status)errno;

    nbt_status ret = write_file(fp, compressed.data, compressed.len);

    buffer_free(&compressed);
    return ret;
}

struct buffer nbt_dump_compressed(const nbt_node* tree, nbt_compression_strategy strat)
{
    struct buffer uncompressed = nbt_dump_binary(tree);

    if(uncompressed.data == NULL)
        return BUFFER_INIT;

    struct buffer compressed = __compress(uncompressed.data, uncompressed.len, strat);

    buffer_free(&uncompressed);
    return compressed;
}
