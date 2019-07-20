/* saugns: Reference item list module.
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

#pragma once
#include "common.h"

/**
 * Doubly-linked "reference" item.
 *
 * The \a ref_type field can hold any value
 * to indicate something about \a data held
 * or what is to be done with it.
 */
typedef struct SAU_RefItem {
	struct SAU_RefItem *next, *prev;
	void *data;
	void *meta_data; // manually set, kept in copies
	int ref_type;  // user-defined values
	int list_type; // copied from list
} SAU_RefItem;

/**
 * List of "reference" items, in turn also forward-linked.
 *
 * The \a list_type field can hold any value
 * to differentiate between lists.
 */
typedef struct SAU_RefList {
	SAU_RefItem *refs;
	SAU_RefItem *last_ref;
	struct SAU_RefList *next; // manually set, kept in copies
	size_t ref_count; // maintained by functions
	int list_type;    // user-defined values
	int flags;
} SAU_RefList;

struct SAU_MemPool;

SAU_RefList *SAU_create_RefList(int list_type,
		struct SAU_MemPool *restrict mem) sauMalloclike;
bool SAU_copy_RefList(SAU_RefList **restrict dstp,
		const SAU_RefList *restrict src,
		struct SAU_MemPool *restrict mem);

bool SAU_RefList_unshallow(SAU_RefList *restrict o,
		const SAU_RefItem *restrict src_end,
		struct SAU_MemPool *restrict mem);
SAU_RefItem *SAU_RefList_add(SAU_RefList *restrict o,
		void *restrict data, int ref_type,
		struct SAU_MemPool *restrict mem) sauMalloclike;
bool SAU_RefList_drop(SAU_RefList *restrict o,
		struct SAU_MemPool *restrict mem);
void SAU_RefList_clear(SAU_RefList *restrict o);
