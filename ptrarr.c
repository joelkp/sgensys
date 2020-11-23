/* ssndgen: Pointer array module.
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

#include "ptrarr.h"
#include "mempool.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add a pointer to the end of the given array.
 *
 * If allocation fails, the array will remain unaltered.
 *
 * \return true unless allocation failed
 */
bool SSG_PtrArr_add(SSG_PtrArr *restrict o, void *restrict item) {
	if (!o->asize) {
		if (o->count == 0) {
			o->items = (void**) item;
			o->count = 1;
		} else {
			size_t asize = 2 * sizeof(void*);
			void **a = malloc(asize);
			if (!a)
				return false;
			a[0] = (void*) o->items;
			a[1] = item;
			o->items = a;
			o->count = 2;
			o->asize = asize;
		}
		return true;
	}

	size_t isize = o->count * sizeof(void*);
	size_t asize = o->asize;
	if (o->count == o->old_count) {
		if (asize == isize) asize <<= 1;
		void **a = malloc(asize);
		if (!a)
			return false;
		memcpy(a, o->items, isize);
		o->items = a;
		o->asize = asize;
	} else if (asize == isize) {
		asize <<= 1;
		void **a = realloc(o->items, asize);
		if (!a)
			return false;
		o->items = a;
		o->asize = asize;
	}
	o->items[o->count] = item;
	++o->count;
	return true;
}

/**
 * Clear the given array.
 */
void SSG_PtrArr_clear(SSG_PtrArr *restrict o) {
	if (o->count > o->old_count && o->asize > 0) {
		free(o->items);
	}
	o->items = 0;
	o->count = 0;
	o->old_count = 0;
	o->asize = 0;
}

/**
 * Memdup function for the contents of the given array.
 *
 * \p dst will be set to point to the new allocation,
 * or to NULL if the array was empty. If the array was
 * non-empty and allocation failed, \p dst will remain
 * unaltered.
 *
 * \return true unless allocation failed
 */
bool SSG_PtrArr_memdup(SSG_PtrArr *restrict o, void ***restrict dst) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * sizeof(void*);
	void **src = SSG_PtrArr_ITEMS(o);
	void **a = SSG_memdup(src, size);
	if (!a)
		return false;
	*dst = a;
	return true;
}

/**
 * Mempool-using variant of the
 * memdup function for the contents of the given array.
 *
 * \p dst will be set to point to the new allocation,
 * or to NULL if the array was empty. If the array was
 * non-empty and allocation failed, \p dst will remain
 * unaltered.
 *
 * \return true unless allocation failed
 */
bool SSG_PtrArr_mpmemdup(SSG_PtrArr *restrict o, void ***restrict dst,
		SSG_MemPool *restrict mempool) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * sizeof(void*);
	void **src = SSG_PtrArr_ITEMS(o);
	void **a = SSG_MemPool_memdup(mempool, src, size);
	if (!a)
		return false;
	*dst = a;
	return true;
}

/**
 * Copy the array \p src to \p dst (clearing dst first if needed);
 * to save memory, dst will actually merely reference the data in
 * src unless/until added to. It is assumed that after copying,
 * src will no longer be added to (unless dst is first cleared
 * or further added to).
 *
 * \a old_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 *
 * Regardless of the order of array clearing, once clearing is
 * taking place, by only accessing items (pointer dereferencing)
 * through iteration between \a old_count and \a count, all
 * accessing of freed memory is avoided.
 */
void SSG_PtrArr_soft_copy(SSG_PtrArr *restrict dst,
		const SSG_PtrArr *restrict src) {
	SSG_PtrArr_clear(dst);
	dst->items = src->items;
	dst->count = src->count;
	dst->old_count = src->count;
	dst->asize = src->asize;
}
