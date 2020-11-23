/* ssndgen: Symbol table module.
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
typedef struct SSG_SymStr {
	struct SSG_SymStr *prev;
	void *data;
	size_t key_len;
	char key[];
} SSG_SymStr;

struct SSG_SymTab;
typedef struct SSG_SymTab SSG_SymTab;

SSG_SymTab *SSG_create_SymTab(SSG_MemPool *restrict mempool) SSG__malloclike;
void SSG_destroy_SymTab(SSG_SymTab *restrict o);

SSG_SymStr *SSG_SymTab_get_symstr(SSG_SymTab *restrict o,
		const void *restrict str, size_t len);

/**
 * Get the unique copy of \p str held in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique copy of \p str, or NULL on allocation failure
 */
static inline const void *SSG_SymTab_pool_str(SSG_SymTab *restrict o,
		const void *restrict str, size_t len) {
	SSG_SymStr *item = SSG_SymTab_get_symstr(o, str, len);
	return (item != NULL) ? item->key : NULL;
}
const char **SSG_SymTab_pool_stra(SSG_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);
