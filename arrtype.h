/* saugns: Generic array module.
 * Copyright (c) 2018-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once
#include "common.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 */

struct SAU_MemPool;

/**
 * Declare array type using \p Name, with \p ElementType.
 *
 * Only declares type, not methods. See SAU_DEF_ArrType().
 */
#define SAU_DEF_ArrType_TYPE(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * Only declares methods, not type. See SAU_DEF_ArrType().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SAU_DEF_ArrType_METHODS(Name, ElementType, MethodPrefix) \
static inline ElementType SAU__maybe_unused \
*MethodPrefix##Name##_add(Name *restrict o, \
		const ElementType *restrict item) { \
	return SAU_ArrType_add(o, item, sizeof(ElementType)); \
} \
static inline bool SAU__maybe_unused \
MethodPrefix##Name##_upsize(Name *restrict o, size_t count) { \
	return SAU_ArrType_upsize(o, count, sizeof(ElementType)); \
} \
static inline void SAU__maybe_unused \
MethodPrefix##Name##_clear(Name *restrict o) { \
	SAU_ArrType_clear(o); \
} \
static inline bool SAU__maybe_unused \
MethodPrefix##Name##_memdup(Name *restrict o, \
		ElementType **restrict dst) { \
	return SAU_ArrType_memdup(o, (void**) dst, sizeof(ElementType)); \
} \
static inline bool SAU__maybe_unused \
MethodPrefix##Name##_mpmemdup(Name *restrict o, \
		ElementType **restrict dst, \
		struct SAU_MemPool *restrict mempool) { \
	return SAU_ArrType_mpmemdup(o, (void**) dst, \
		sizeof(ElementType), mempool); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * Combines SAU_DEF_ArrType_TYPE() and SAU_DEF_ArrType_METHODS().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SAU_DEF_ArrType(Name, ElementType, MethodPrefix) \
SAU_DEF_ArrType_TYPE(Name, ElementType) \
SAU_DEF_ArrType_METHODS(Name, ElementType, MethodPrefix)

void *SAU_ArrType_add(void *restrict o,
		const void *restrict item, size_t item_size);
bool SAU_ArrType_upsize(void *restrict o,
		size_t count, size_t item_size);
void SAU_ArrType_clear(void *restrict o);
bool SAU_ArrType_memdup(void *restrict o,
		void **restrict dst, size_t item_size);
bool SAU_ArrType_mpmemdup(void *restrict o,
		void **restrict dst, size_t item_size,
		struct SAU_MemPool *mempool);

/** Byte (uint8_t) array type. */
SAU_DEF_ArrType(SAU_ByteArr, uint8_t, )
