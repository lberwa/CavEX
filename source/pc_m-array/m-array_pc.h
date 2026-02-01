#ifndef M_ARRAY_PC_H
#define M_ARRAY_PC_H

#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#define M_POD_OPLIST // Dummy, wird von ARRAY_DEF gebraucht

#define MAX_RECIPES 128

#ifndef ARRAY_DEF
#define ARRAY_DEF(name, type, oplist)                 \
typedef struct {                                      \
    type data[MAX_RECIPES];                           \
    size_t size;                                      \
} name##_t;                                          \
                                                      \
/* Init */                                           \
static inline void name##_init(name##_t* a) { a->size = 0; } \
                                                      \
/* Push */                                           \
static inline void name##_push_back(name##_t* a, type val) { \
    assert(a->size < MAX_RECIPES);                   \
    a->data[a->size++] = val;                        \
}                                                     \
                                                      \
/* Iterator */                                       \
typedef size_t name##_it_t;                           \
static inline void name##_it(name##_it_t* it, name##_t* a) { *it = 0; } \
static inline bool name##_end_p(name##_it_t it, name##_t* a) { return it >= a->size; } \
static inline type* name##_ref(name##_it_t it, name##_t* a) { return (it >= a->size) ? NULL : &a->data[it]; } \
static inline void name##_next(name##_it_t* it) { (*it)++; } \
                                                      \
/* Entfernen */                                      \
static inline void name##_remove(name##_t* a, name##_it_t* it) { \
    if(*it >= a->size) return;                        \
    for(size_t i = *it; i < a->size - 1; i++)        \
        a->data[i] = a->data[i+1];                   \
    a->size--;                                        \
}
#endif

#endif
