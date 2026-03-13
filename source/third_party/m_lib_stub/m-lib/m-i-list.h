#ifndef M_LIB_I_LIST_H
#define M_LIB_I_LIST_H

#include <stddef.h>
#include <stdbool.h>
#include "m-core.h"

#define ILIST_INTERFACE(name, type) \
	void* name;

#define ILIST_DEF(name, type, oplist) \
	typedef struct name##_s { int dummy; } name##_s; \
	typedef name##_s name##_t[1]; \
	typedef struct name##_it_s { int done; } name##_it_s; \
	typedef name##_it_s name##_it_t[1]; \
	static inline void name##_init(name##_t list) { (void)list; } \
	static inline void name##_init_field(type* item) { (void)item; } \
	static inline void name##_reset(name##_t list) { (void)list; } \
	static inline void name##_clear(name##_t list) { (void)list; } \
	static inline bool name##_empty_p(name##_t list) { (void)list; return true; } \
	static inline size_t name##_size(name##_t list) { (void)list; return 0; } \
	static inline void name##_push_back(name##_t list, type* item) { (void)list; (void)item; } \
	static inline void name##_push_front(name##_t list, type* item) { (void)list; (void)item; } \
	static inline type* name##_pop_front(name##_t list) { (void)list; return NULL; } \
	static inline void name##_it(name##_it_t it, name##_t list) { (void)list; it[0].done = 1; } \
	static inline void name##_it_last(name##_it_t it, name##_t list) { (void)list; it[0].done = 1; } \
	static inline bool name##_end_p(name##_it_t it) { (void)it; return true; } \
	static inline void name##_next(name##_it_t it) { (void)it; } \
	static inline type* name##_ref(name##_it_t it) { (void)it; return NULL; } \
	static inline void name##_remove(name##_t list, name##_it_t it) { (void)list; (void)it; } \
	static inline void name##_unlink(type* item) { (void)item; }

#endif
