/* ssndgen: Reference item list module.
 * Copyright (c) 2019-2020 Joel K. Pettersson
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

#include "reflist.h"
#include "mempool.h"

#define RL_SHALLOW (1<<0)

/**
 * Create instance using mempool \p mem.
 *
 * \return instance, or NULL on allocation failure
 */
SSG_RefList *SSG_create_RefList(int list_type,
		SSG_MemPool *restrict mem) {
	SSG_RefList *o = SSG_MemPool_alloc(mem, sizeof(SSG_RefList));
	if (!o)
		return NULL;
	o->list_type = list_type;
	return o;
}

/**
 * Create shallow copy of \p src using mempool \p mem.
 * If \p src is NULL, the copy amounts to setting \p dstp to NULL.
 *
 * Modifications to a list with shallowly copied items
 * will un-shallow the copy (except list clearing).
 *
 * \return true, or false on allocation failure
 */
bool SSG_copy_RefList(SSG_RefList **restrict dstp,
		const SSG_RefList *restrict src,
		SSG_MemPool *restrict mem) {
	if (!src) {
		*dstp = NULL;
		return true;
	}
	if (!*dstp) {
		*dstp = SSG_MemPool_memdup(mem, src, sizeof(SSG_RefList));
		if (!*dstp)
			return false;
	}
	(*dstp)->flags |= RL_SHALLOW;
	return true;
}

/**
 * Un-shallow copy using mempool \p mem.
 * Does nothing if the list is not a shallow copy.
 *
 * If \p src_end is not NULL,
 * ends deep copy with the item before \p src_end.
 *
 * Called by functions for adding and removing items.
 *
 * \return true, or false on allocation failure
 */
bool SSG_RefList_unshallow(SSG_RefList *restrict o,
		const SSG_RefItem *restrict src_end,
		SSG_MemPool *restrict mem) {
	if (!(o->flags & RL_SHALLOW))
		return true;
	SSG_RefItem *first_ref = NULL;
	size_t ref_count = 0;
	if (!o->refs || o->refs == src_end) goto DONE;
	first_ref = SSG_MemPool_memdup(mem, o->refs, sizeof(SSG_RefItem));
	if (!first_ref)
		return false;
	++ref_count;
	SSG_RefItem *last_ref = first_ref;
	for (SSG_RefItem *src_ref = first_ref->next;
			src_ref != src_end; src_ref = src_ref->next) {
		SSG_RefItem *dst_ref = SSG_MemPool_memdup(mem,
				src_ref, sizeof(SSG_RefItem));
		if (!dst_ref)
			return false;
		last_ref->next = dst_ref;
		dst_ref->prev = last_ref;
		last_ref = dst_ref;
		++ref_count;
	}
	o->last_ref = last_ref;
	o->last_ref->next = NULL; // in case src_end != NULL
DONE:
	o->refs = first_ref;
	o->ref_count = ref_count;
	o->flags &= ~RL_SHALLOW;
	return true;
}

/**
 * Add reference item to the end of the list, created using \p mem.
 *
 * \return instance, or NULL on allocation failure
 */
SSG_RefItem *SSG_RefList_add(SSG_RefList *restrict o,
		void *restrict data, int ref_type,
		SSG_MemPool *restrict mem) {
	SSG_RefItem *ref = SSG_MemPool_alloc(mem, sizeof(SSG_RefItem));
	if (!ref)
		return NULL;
	ref->data = data;
	if (!o->refs) {
		o->refs = ref;
	} else {
		if (o->flags & RL_SHALLOW) {
			if (!SSG_RefList_unshallow(o, NULL, mem))
				return NULL;
		}
		o->last_ref->next = ref;
		ref->prev = o->last_ref;
	}
	o->last_ref = ref;
	ref->ref_type = ref_type;
	ref->list_type = o->list_type;
	++o->ref_count;
	return ref;
}

/**
 * Drop reference item from the end of the list.
 *
 * \return true, or false on allocation failure
 */
bool SSG_RefList_drop(SSG_RefList *restrict o,
		SSG_MemPool *restrict mem) {
	if (o->flags & RL_SHALLOW)
		return SSG_RefList_unshallow(o, o->last_ref, mem);
	if (!o->refs)
		return true;
	if (!o->refs->next) {
		SSG_RefList_clear(o);
		return true;
	}
	o->last_ref = o->last_ref->prev;
	o->last_ref->next = NULL;
	--o->ref_count;
	return true;
}

/**
 * Remove reference items from the list,
 * leaving fields \a next and \a list_type in place.
 */
void SSG_RefList_clear(SSG_RefList *restrict o) {
	o->refs = NULL;
	o->last_ref = NULL;
	o->ref_count = 0;
	o->flags = 0;
}
