/*
* -----------------------------------------------------------------------------
* "THE BEER-WARE LICENSE" (Revision 42):
* Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
* wrote this file. As long as you retain this notice you can do whatever you
* want with this stuff. If we meet some day, and you think this stuff is worth
* it, you can buy us a beer in return.
* -----------------------------------------------------------------------------
*/
#include "buffer.h"
#include "nbt.h" /* nbt_malloc/nbt_realloc/nbt_free_mem + nbt_arena_is_active */

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* Im Arena-Modus (churn-freies Chunk-Laden) wird der Dekomprimierungs-Puffer
 * EINMAL fest gross belegt (kein realloc-Wachstum, das den Heap fragmentiert).
 * Reicht fuer einen dekomprimierten Chunk-NBT. */
#define BUFFER_ARENA_CAP (384 * 1024)

#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(  (x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

static int lazy_init(struct buffer* b)
{
    assert(b->data == NULL);

    /* Arena-Modus: einmalig gross belegen -> nie realloc (fragmentierungsfrei). */
    size_t cap = nbt_arena_is_active() ? BUFFER_ARENA_CAP : 1024;

    *b = (struct buffer) {
        .data = nbt_malloc(cap),
        .len  = 0,
        .cap  = cap
    };

    if(unlikely(b->data == NULL))
        return 1;

    return 0;
}

void buffer_free(struct buffer* b)
{
    assert(b);

    nbt_free_mem(b->data); /* im Arena-Modus No-Op */

    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int buffer_reserve(struct buffer* b, size_t reserved_amount)
{
    assert(b);

    if(unlikely(b->data == NULL) &&
       unlikely(lazy_init(b)))
        return 1;

    if(likely(b->cap >= reserved_amount))
        return 0;

    /* Arena-Modus: die feste Kapazitaet kann nicht wachsen. Reicht sie nicht,
     * sauber scheitern (Chunk-Load faellt dann fehl -> naechster Tick). */
    if(nbt_arena_is_active())
        return 1;

    size_t old_cap = b->cap;
    while(b->cap < reserved_amount)
        b->cap *= 2;

    unsigned char* temp = nbt_realloc(b->data, old_cap, b->cap);

    if(unlikely(temp == NULL))
        return buffer_free(b), 1;

    b->data = temp;

    return 0;
}

int buffer_append(struct buffer* b, const void* data, size_t n)
{
    assert(b);

    if(unlikely(b->data == NULL) &&
       unlikely(lazy_init(b)))
        return 1;

    if(unlikely(buffer_reserve(b, b->len + n)))
        return 1;

    memcpy(b->data + b->len, data, n);
    b->len += n;

    return 0;
}

