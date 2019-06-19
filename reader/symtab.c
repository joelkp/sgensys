/* saugns: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#include "symtab.h"
#include <string.h>
#include <stdlib.h>

#define STRTAB_ALLOC_INITIAL 1024

#ifndef SAU_SYMTAB_STATS
/*
 * Print symbol table statistics for testing?
 */
# define SAU_SYMTAB_STATS 0
#endif
#if SAU_SYMTAB_STATS
static size_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StrTab {
	SAU_SymStr **items;
	size_t count;
	size_t alloc;
} StrTab;

static inline void fini_StrTab(StrTab *restrict o) {
	free(o->items);
}

/*
 * Return the hash of the given string \p key of lenght \p len.
 *
 * \return hash
 */
static size_t StrTab_hash_key(StrTab *restrict o,
		const char *restrict key, size_t len) {
	size_t i;
	size_t hash;
	/*
	 * Calculate DJB2 hash,
	 * varied by adding len.
	 */
	hash = 5381 + (len * 33);
	for (i = 0; i < len; ++i) {
		size_t c = key[i];
		hash = ((hash << 5) + hash) ^ c;
	}
	hash &= (o->alloc - 1);
	return hash;
}

/*
 * Increase the size of the hash table.
 *
 * \return true, or false on allocation failure
 */
static bool StrTab_upsize(StrTab *restrict o) {
	SAU_SymStr **items, **old_items = o->items;
	size_t alloc, old_alloc = o->alloc;
	size_t i;
	alloc = (old_alloc > 0) ?
		(old_alloc << 1) :
		STRTAB_ALLOC_INITIAL;
	items = calloc(alloc, sizeof(SAU_SymStr*));
	if (!items)
		return false;
	o->alloc = alloc;
	o->items = items;

	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_alloc; ++i) {
		SAU_SymStr *item = old_items[i];
		while (item != NULL) {
			SAU_SymStr *prev_item;
			size_t hash;
			hash = StrTab_hash_key(o, item->key, item->key_len);
			/*
			 * Before adding the entry to the new table, set
			 * item->prev to the previous (if any) item with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_item = item->prev;
			item->prev = o->items[hash];
			o->items[hash] = item;
			item = prev_item;
		}
	}
	free(old_items);
	return true;
}

/*
 * Get unique item for key in hash table, adding it if missing.
 * If allocated, \p extra is added to the size of the node; use
 * 1 to add a NULL-byte for a string key.
 *
 * Initializes the hash table if empty.
 *
 * \return SAU_SymStr, or NULL on allocation failure
 */
static SAU_SymStr *StrTab_unique_item(StrTab *restrict o,
		SAU_MemPool *restrict memp,
		const void *restrict key, size_t len, size_t extra) {
	if (!key || len == 0)
		return NULL;
	if (o->count == (o->alloc / 2)) {
		if (!StrTab_upsize(o))
			return NULL;
	}

	size_t hash = StrTab_hash_key(o, key, len);
	SAU_SymStr *item = o->items[hash];
	while (item != NULL) {
		if (item->key_len == len &&
			!memcmp(item->key, key, len)) return item;
		item = item->prev;
#if SAU_SYMTAB_STATS
		++collision_count;
#endif
	}
	item = SAU_MemPool_alloc(memp, sizeof(SAU_SymStr) + (len + extra));
	if (!item)
		return NULL;
	item->prev = o->items[hash];
	o->items[hash] = item;
	item->key_len = len;
	memcpy(item->key, key, len);
	++o->count;
	return item;
}

struct SAU_SymTab {
	SAU_MemPool *memp;
	StrTab strtab;
};

/**
 * Create instance. Requires \p mempool to be a valid instance.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_SymTab *SAU_create_SymTab(SAU_MemPool *restrict mempool) {
	if (!mempool)
		return NULL;
	SAU_SymTab *o = calloc(1, sizeof(SAU_SymTab));
	if (!o)
		return NULL;
	o->memp = mempool;
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_SymTab(SAU_SymTab *restrict o) {
	if (!o)
		return;
#if SAU_SYMTAB_STATS
	printf("collision count: %zd\n", collision_count);
#endif
	fini_StrTab(&o->strtab);
}

/**
 * Get the unique item held for \p str in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique item for \p str, or NULL on allocation failure
 */
SAU_SymStr *SAU_SymTab_get_symstr(SAU_SymTab *restrict o,
		const void *restrict str, size_t len) {
	return StrTab_unique_item(&o->strtab, o->memp, str, len, 1);
}

/**
 * Add the first \p n strings from \p stra to the string pool of the
 * symbol table, except any already present. An array of pointers to
 * the unique string pool copies of all \p stra strings, followed by
 * an extra NULL pointer, is allocated and returned; it is stored in
 * the memory pool used by the symbol table.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return array of pointers to unique strings, or NULL on allocation failure
 */
const char **SAU_SymTab_pool_stra(SAU_SymTab *restrict o,
		const char *const*restrict stra,
		size_t n) {
	const char **res_stra;
	res_stra = SAU_MemPool_alloc(o->memp, sizeof(const char*) * (n + 1));
	if (!res_stra)
		return NULL;
	for (size_t i = 0; i < n; ++i) {
		const char *str = SAU_SymTab_pool_str(o,
				stra[i], strlen(stra[i]));
		if (!str)
			return NULL;
		res_stra[i] = str;
	}
	return res_stra;
}
