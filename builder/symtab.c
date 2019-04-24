/* saugns: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2019 Joel K. Pettersson
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
#include "../mempool.h"
#include <string.h>
#include <stdlib.h>

#define STRTAB_ALLOC_INITIAL 1024

#if SAU_HASHTAB_STATS
static size_t collision_count = 0;
#include <stdio.h>
#endif

typedef struct StrEntry {
	struct StrEntry *prev;
	void *symbol_data;
	size_t len;
	char str[1]; /* sized to actual length */
} StrEntry;

#define GET_STRING_ENTRY_SIZE(str_len) \
	(offsetof(StrEntry, str) + (str_len))

struct SAU_SymTab {
	SAU_MemPool *malc;
	StrEntry **strtab;
	size_t strtab_count;
	size_t strtab_alloc;
};

/**
 * Create instance.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_SymTab *SAU_create_SymTab(void) {
	SAU_SymTab *o = calloc(1, sizeof(SAU_SymTab));
	if (o == NULL) return NULL;
	o->malc = SAU_create_MemPool(0);
	if (o->malc == NULL) {
		free(o);
		return NULL;
	}
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_SymTab(SAU_SymTab *restrict o) {
#if SAU_HASHTAB_STATS
	printf("collision count: %zd\n", collision_count);
#endif
	SAU_destroy_MemPool(o->malc);
	free(o->strtab);
}

/*
 * Return the hash of the given string \p str of lenght \p len.
 *
 * \return hash
 */
static size_t hash_string(SAU_SymTab *restrict o,
		const char *restrict str, size_t len) {
	size_t i;
	size_t hash;
	/*
	 * Calculate hash.
	 */
	hash = len;
	for (i = 0; i < len; ++i) {
		size_t c = str[i];
		hash = 37 * hash + c;
	}
	hash &= (o->strtab_alloc - 1); /* strtab_alloc is a power of 2 */
	return hash;
}

/*
 * Increase the size of the hash table for the string pool.
 *
 * \return true, or false on allocation failure
 */
static bool extend_strtab(SAU_SymTab *restrict o) {
	StrEntry **old_strtab = o->strtab;
	size_t old_strtab_alloc = o->strtab_alloc;
	size_t i;
	o->strtab_alloc = (o->strtab_alloc > 0) ?
		(o->strtab_alloc << 1) :
		STRTAB_ALLOC_INITIAL;
	o->strtab = calloc(o->strtab_alloc, sizeof(StrEntry*));
	if (o->strtab == NULL)
		return false;
	/*
	 * Rehash entries
	 */
	for (i = 0; i < old_strtab_alloc; ++i) {
		StrEntry *entry = old_strtab[i];
		while (entry) {
			StrEntry *prev_entry;
			size_t hash;
			hash = hash_string(o, entry->str, entry->len);
			/*
			 * Before adding the entry to the new table, set
			 * entry->prev to the previous (if any) entry with
			 * the same hash in the new table. Done repeatedly,
			 * the links are rebuilt, though not necessarily in
			 * the same order.
			 */
			prev_entry = entry->prev;
			entry->prev = o->strtab[hash];
			o->strtab[hash] = entry;
			entry = prev_entry;
		}
	}
	free(old_strtab);
	return true;
}

/*
 * Get unique entry for string in symbol table, or NULL if missing.
 *
 * Initializes the string table if empty.
 *
 * \return StrEntry, or NULL if none
 */
static StrEntry *unique_entry(SAU_SymTab *restrict o,
		const void *restrict str, size_t len) {
	size_t hash;
	StrEntry *entry;
	if (o->strtab_count == (o->strtab_alloc / 2)) {
		if (!extend_strtab(o)) return NULL;
	}
	if (str == NULL || len == 0) return NULL;
	hash = hash_string(o, str, len);
	entry = o->strtab[hash];
	if (!entry) goto ADD_ENTRY; /* missing */
	for (;;) {
		if (entry->len == len &&
			!strcmp(entry->str, str)) return entry; /* found */
		entry = entry->prev;
		if (entry == NULL) break; /* missing */
	}
#if SAU_HASHTAB_STATS
	++collision_count;
#endif
ADD_ENTRY:
	entry = SAU_MemPool_alloc(o->malc, GET_STRING_ENTRY_SIZE(len + 1));
	if (entry == NULL) return NULL;
	entry->prev = o->strtab[hash];
	o->strtab[hash] = entry;
	entry->symbol_data = NULL;
	entry->len = len;
	memcpy(entry->str, str, len);
	entry->str[len] = '\0';
	++o->strtab_count;
	return entry;
}

/**
 * Add \p str to the string pool of the symbol table, unless already
 * present. Return the copy of \p str unique to the symbol table.
 *
 * \return unique copy of \p str for instance, or NULL on allocation failure
 */
const void *SAU_SymTab_pool_str(SAU_SymTab *restrict o,
		const void *restrict str, size_t len) {
	StrEntry *entry = unique_entry(o, str, len);
	return (entry) ? entry->str : NULL;
}

/**
 * Add the first \p n strings from \p stra to the string pool of the
 * symbol table, except any already present. An array of pointers to
 * the unique string pool copies of all \p stra strings is allocated
 * and returned; it will be freed when the symbol table is destroyed.
 *
 * All strings in \p stra need to be null-terminated.
 *
 * \return array of pointers to unique strings, or NULL on allocation failure
 */
const char **SAU_SymTab_pool_stra(SAU_SymTab *restrict o,
		const char *const*restrict stra, size_t n) {
	const char **res_stra;
	res_stra = SAU_MemPool_alloc(o->malc, sizeof(const char*) * n);
	if (!res_stra) return NULL;
	for (size_t i = 0; i < n; ++i) {
		const char *str = SAU_SymTab_pool_str(o,
				stra[i], strlen(stra[i]));
		if (!str) return NULL;
		res_stra[i] = str;
	}
	return res_stra;
}

/**
 * Return value associated with string.
 *
 * \return value, or NULL if none
 */
void *SAU_SymTab_get(SAU_SymTab *restrict o,
		const void *restrict key, size_t len) {
	StrEntry *entry;
	entry = unique_entry(o, key, len);
	if (!entry) return NULL;
	return entry->symbol_data;
}

/**
 * Set value associated with string.
 *
 * \return previous value, or NULL if none
 */
void *SAU_SymTab_set(SAU_SymTab *restrict o,
		const void *restrict key, size_t len, void *restrict value) {
	StrEntry *entry;
	entry = unique_entry(o, key, len);
	if (!entry) return NULL;
	void *old_value = entry->symbol_data;
	entry->symbol_data = value;
	return old_value;
}