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

#pragma once
#include "common.h"

/**
 * Doubly-linked "reference" item.
 *
 * The \a ref_type field can hold any value
 * to indicate something about \a data held
 * or what is to be done with it.
 */
typedef struct SSG_RefItem {
	struct SSG_RefItem *next, *prev;
	void *data;
	void *meta_data; // manually set, kept in copies
	int ref_type;  // user-defined values
	int list_type; // copied from list
} SSG_RefItem;

/**
 * List of "reference" items, in turn also forward-linked.
 *
 * The \a list_type field can hold any value
 * to differentiate between lists.
 */
typedef struct SSG_RefList {
	SSG_RefItem *refs;
	SSG_RefItem *last_ref;
	struct SSG_RefList *next; // manually set, kept in copies
	size_t ref_count; // maintained by functions
	int list_type;    // user-defined values
	int flags;
} SSG_RefList;

struct SSG_MemPool;

SSG_RefList *SSG_create_RefList(int list_type,
		struct SSG_MemPool *restrict mem) SSG__malloclike;
bool SSG_copy_RefList(SSG_RefList **restrict dstp,
		const SSG_RefList *restrict src,
		struct SSG_MemPool *restrict mem);

bool SSG_RefList_unshallow(SSG_RefList *restrict o,
		const SSG_RefItem *restrict src_end,
		struct SSG_MemPool *restrict mem);
SSG_RefItem *SSG_RefList_add(SSG_RefList *restrict o,
		void *restrict data, int ref_type,
		struct SSG_MemPool *restrict mem) SSG__malloclike;
bool SSG_RefList_drop(SSG_RefList *restrict o,
		struct SSG_MemPool *restrict mem);
void SSG_RefList_clear(SSG_RefList *restrict o);
