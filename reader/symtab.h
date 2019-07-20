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

#pragma once
#include "../mempool.h"

/**
 * Item stored for each unique string associated with the symbol table.
 */
typedef struct SAU_SymStr {
	struct SAU_SymStr *prev;
	void *data;
	size_t key_len;
	char key[];
} SAU_SymStr;

struct SAU_SymTab;
typedef struct SAU_SymTab SAU_SymTab;

SAU_SymTab *SAU_create_SymTab(SAU_MemPool *restrict mempool) sauMalloclike;
void SAU_destroy_SymTab(SAU_SymTab *restrict o);

SAU_SymStr *SAU_SymTab_get_symstr(SAU_SymTab *restrict o,
		const void *restrict str, size_t len);

/**
 * Get the unique copy of \p str held in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique copy of \p str, or NULL on allocation failure
 */
static inline const void *SAU_SymTab_pool_str(SAU_SymTab *restrict o,
		const void *restrict str, size_t len) {
	SAU_SymStr *item = SAU_SymTab_get_symstr(o, str, len);
	return (item != NULL) ? item->key : NULL;
}
const char **SAU_SymTab_pool_stra(SAU_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);
