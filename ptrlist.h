/* ssndgen: Pointer list module.
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
 * Pointer list type using an array with resizing.
 *
 * A soft copy (SSG_PtrList_soft_copy()) references
 * the original underlying array instead of duplicating
 * it, unless/until added to.
 */
typedef struct SSG_PtrList {
	void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SSG_PtrList;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an array if at most
 * 1 item is held.
 */
#define SSG_PtrList_ITEMS(o) \
	((o)->count > 1 ? \
		(o)->items : \
		((void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SSG_PtrList_GET(o, i) \
	((void*) SSG_PtrList_ITEMS(o)[i])

struct SSG_MemPool;

bool SSG_PtrList_add(SSG_PtrList *restrict o, void *restrict item);
void SSG_PtrList_clear(SSG_PtrList *restrict o);
bool SSG_PtrList_memdup(SSG_PtrList *restrict o, void ***restrict dst);
bool SSG_PtrList_mpmemdup(SSG_PtrList *restrict o, void ***restrict dst,
		struct SSG_MemPool *restrict mempool);
void SSG_PtrList_soft_copy(SSG_PtrList *restrict dst,
		const SSG_PtrList *restrict src);
