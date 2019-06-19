/* saugns: Script lexer module.
 * Copyright (c) 2014, 2017-2020 Joel K. Pettersson
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
#include "symtab.h"

/*
 * Token enumerations.
 */
enum {
	SAU_T_INVALID = 0,
	SAU_T_ID_STR,
	SAU_T_VAL_INT,
	SAU_T_VAL_REAL,
	SAU_T_SPECIAL,
};

typedef struct SAU_ScriptToken {
	uint32_t type;
	union {
		const char *id;
		int32_t i;
		float f;
		uint8_t b;
		char c;
	} data;
} SAU_ScriptToken;

struct SAU_Lexer;
typedef struct SAU_Lexer SAU_Lexer;

SAU_Lexer *SAU_create_Lexer(SAU_SymTab *restrict symtab) SAU__malloclike;
void SAU_destroy_Lexer(SAU_Lexer *restrict o);

bool SAU_Lexer_open(SAU_Lexer *restrict o,
		const char *restrict script, bool is_path);
void SAU_Lexer_close(SAU_Lexer *restrict o);

bool SAU_Lexer_get(SAU_Lexer *restrict o, SAU_ScriptToken *restrict t);
bool SAU_Lexer_get_special(SAU_Lexer *restrict o, SAU_ScriptToken *restrict t);
