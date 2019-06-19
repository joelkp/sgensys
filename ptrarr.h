/* saugns: Pointer array module.
 * Copyright (c) 2011-2012, 2018-2020 Joel K. Pettersson
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

/**
 * Dynamically sized pointer array. Only supports appending.
 *
 * A soft copy (SAU_PtrArr_soft_copy()) references
 * the original items instead of duplicating them,
 * unless/until the array is added to.
 */
typedef struct SAU_PtrArr {
	void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SAU_PtrArr;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an allocation
 * if only 1 item is held.
 */
#define SAU_PtrArr_ITEMS(o) \
	((o)->count > 1 ? \
		((void**) (o)->items) : \
		((void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SAU_PtrArr_GET(o, i) \
	((void*) SAU_PtrArr_ITEMS(o)[i])

struct SAU_MemPool;

bool SAU_PtrArr_add(SAU_PtrArr *restrict o, void *restrict item);
void SAU_PtrArr_clear(SAU_PtrArr *restrict o);
bool SAU_PtrArr_memdup(SAU_PtrArr *restrict o, void ***restrict dst);
bool SAU_PtrArr_mpmemdup(SAU_PtrArr *restrict o, void ***restrict dst,
		struct SAU_MemPool *restrict mempool);
void SAU_PtrArr_soft_copy(SAU_PtrArr *restrict dst,
		const SAU_PtrArr *restrict src);
