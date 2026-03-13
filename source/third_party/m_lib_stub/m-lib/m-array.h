#ifndef M_LIB_ARRAY_H
#define M_LIB_ARRAY_H

#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include "m-core.h"

#ifndef ARRAY_DEF
#define ARRAY_DEF(name, type, oplist)                 \
	typedef struct name##_s {                           \
		type data[256];                                  \
		size_t size;                                     \
	} name##_s;                                         \
	typedef name##_s name##_t[1];                       \
	typedef struct name##_it_s {                        \
		name##_s* a;                                     \
		size_t i;                                        \
	} name##_it_s;                                      \
	typedef name##_it_s name##_it_t[1];                 \
	                                                     \
	static inline void name##_init(name##_t a) { a->size = 0; } \
	static inline void name##_clear(name##_t a) { a->size = 0; } \
	static inline void name##_reset(name##_t a) { a->size = 0; } \
	static inline void name##_push_back(name##_t a, type val) { \
		if(a->size < 256) a->data[a->size++] = val;       \
	}                                                    \
	static inline type* name##_push_new(name##_t a) { \
		if(a->size < 256) return &a->data[a->size++]; \
		return NULL; \
	} \
	static inline void name##_it(name##_it_t it, name##_t a) { it->a = a; it->i = 0; } \
	static inline bool name##_end_p(name##_it_t it) { return (!it->a) || it->i >= it->a->size; } \
	static inline type* name##_ref(name##_it_t it) { return (it->a && it->i < it->a->size) ? &it->a->data[it->i] : NULL; } \
	static inline void name##_next(name##_it_t it) { if(it->a) it->i++; } \
	static inline void name##_remove(name##_t a, name##_it_t it) { \
		if(it->i >= a->size) return;                     \
		for(size_t i = it->i; i + 1 < a->size; i++)       \
			a->data[i] = a->data[i+1];                    \
		a->size--;                                       \
	}
#endif

#ifndef M_ARRAY_DEF_AS
#define M_ARRAY_DEF_AS(name, name_t, it_t, type, oplist) \
	ARRAY_DEF(name, type, oplist)
#endif

#ifndef M_ARRAY_DEF
#define M_ARRAY_DEF(name, type, oplist) \
	ARRAY_DEF(name, type, oplist)
#endif

#endif
