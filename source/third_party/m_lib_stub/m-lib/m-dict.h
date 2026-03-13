#ifndef M_LIB_DICT_H
#define M_LIB_DICT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "m-core.h"

#define DICT_DEF2(name, key_t, key_oplist, value_t, value_oplist) \
	typedef struct name##_s { int dummy; } name##_s; \
	typedef name##_s name##_t[1]; \
	typedef struct name##_it_s { int done; } name##_it_s; \
	typedef name##_it_s name##_it_t[1]; \
	typedef struct name##_itref_s { key_t key; value_t value; } name##_itref_t; \
	static inline void name##_init(name##_t dict) { (void)dict; } \
	static inline void name##_reset(name##_t dict) { (void)dict; } \
	static inline void name##_clear(name##_t dict) { (void)dict; } \
	static inline size_t name##_size(name##_t dict) { (void)dict; return 0; } \
	static inline value_t* name##_get(name##_t dict, key_t key) { (void)dict; (void)key; return NULL; } \
	static inline value_t* name##_safe_get(name##_t dict, key_t key) { \
		static value_t tmp; (void)dict; (void)key; return &tmp; } \
	static inline void name##_set_at(name##_t dict, key_t key, value_t value) { \
		(void)dict; (void)key; (void)value; } \
	static inline void name##_erase(name##_t dict, key_t key) { (void)dict; (void)key; } \
	static inline void name##_it(name##_it_t it, name##_t dict) { (void)dict; it[0].done = 1; } \
	static inline bool name##_end_p(name##_it_t it) { (void)it; return true; } \
	static inline void name##_next(name##_it_t it) { (void)it; } \
	static inline name##_itref_t* name##_ref(name##_it_t it) { \
		static name##_itref_t ref; (void)it; return &ref; }

#define DICT_SET_DEF(name, key_t) \
	typedef struct name##_s { int dummy; } name##_s; \
	typedef name##_s name##_t[1]; \
	typedef struct name##_it_s { int done; } name##_it_s; \
	typedef name##_it_s name##_it_t[1]; \
	static inline void name##_init(name##_t set) { (void)set; } \
	static inline void name##_reset(name##_t set) { (void)set; } \
	static inline void name##_clear(name##_t set) { (void)set; } \
	static inline size_t name##_size(name##_t set) { (void)set; return 0; } \
	static inline void name##_push(name##_t set, key_t key) { (void)set; (void)key; } \
	static inline void name##_it(name##_it_t it, name##_t set) { (void)set; it[0].done = 1; } \
	static inline bool name##_end_p(name##_it_t it) { (void)it; return true; } \
	static inline void name##_next(name##_it_t it) { (void)it; } \
	static inline key_t* name##_ref(name##_it_t it) { static key_t tmp; (void)it; return &tmp; }

#endif
