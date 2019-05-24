/* ssndgen: Script lexer module.
 * Copyright (c) 2014, 2017-2019 Joel K. Pettersson
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
	SSG_T_INVALID = 0,
	SSG_T_ID_STR,
	SSG_T_VAL_INT,
	SSG_T_VAL_REAL,
	SSG_T_SPECIAL,
};

typedef struct SSG_ScriptToken {
	uint32_t type;
	union {
		const char *id;
		int32_t i;
		float f;
		uint8_t b;
		char c;
	} data;
} SSG_ScriptToken;

struct SSG_Lexer;
typedef struct SSG_Lexer SSG_Lexer;

SSG_Lexer *SSG_create_Lexer(SSG_SymTab *restrict symtab) SSG__malloclike;
void SSG_destroy_Lexer(SSG_Lexer *restrict o);

bool SSG_Lexer_open(SSG_Lexer *restrict o,
		const char *restrict script, bool is_path);
void SSG_Lexer_close(SSG_Lexer *restrict o);

bool SSG_Lexer_get(SSG_Lexer *restrict o, SSG_ScriptToken *restrict t);
bool SSG_Lexer_get_special(SSG_Lexer *restrict o, SSG_ScriptToken *restrict t);
