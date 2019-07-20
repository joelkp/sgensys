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

#include "lexer.h"
#include "scanner.h"
#include "../math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Read helper definitions & functions.
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))
#define IS_ALNUM(c) (IS_ALPHA(c) || IS_DIGIT(c))
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')

/* Valid characters in identifiers. */
#define IS_SYMCHAR(c) (IS_ALNUM(c) || (c) == '_')

/* Visible ASCII character. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

/*
 * Lexer implementation.
 */

#define STRBUF_LEN 1024

struct SAU_Lexer {
	SAU_Scanner *sc;
	SAU_SymTab *symtab;
	SAU_ScriptToken token;
};

/**
 * Create instance for the given symbol table.
 *
 * \return instance, or NULL on failure.
 */
SAU_Lexer *SAU_create_Lexer(SAU_SymTab *restrict symtab) {
	if (!symtab)
		return NULL;
	SAU_Lexer *o = calloc(1, sizeof(SAU_Lexer));
	if (!o)
		return NULL;
	o->sc = SAU_create_Scanner(symtab);
	if (!o->sc) goto ERROR;
	o->symtab = symtab;
#if SAU_LEXER_QUIET
	o->sc->s_flags |= SAU_SCAN_S_QUIET;
#endif
	SAU_Scanner_setws_level(o->sc, SAU_SCAN_WS_NONE);
	return o;
ERROR:
	SAU_destroy_Lexer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Lexer(SAU_Lexer *restrict o) {
	if (!o)
		return;
	SAU_destroy_Scanner(o->sc);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around SAU_Scanner functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool SAU_Lexer_open(SAU_Lexer *restrict o,
		const char *restrict script, bool is_path) {
	return SAU_Scanner_open(o->sc, script, is_path);
}

/**
 * Close file (if open).
 */
void SAU_Lexer_close(SAU_Lexer *restrict o) {
	SAU_Scanner_close(o->sc);
}

static void handle_invalid(SAU_Lexer *o, uint8_t c sauMaybeUnused) {
	SAU_ScriptToken *t = &o->token;
	t->type = SAU_T_INVALID;
	t->data.b = 0;
}

static void handle_eof(SAU_Lexer *restrict o,
		uint8_t c sauMaybeUnused) {
	SAU_Scanner *sc = o->sc;
	SAU_ScriptToken *t = &o->token;
	t->type = SAU_T_INVALID;
	t->data.b = SAU_File_STATUS(sc->f);
	//puts("EOF");
}

static void handle_special(SAU_Lexer *restrict o, uint8_t c) {
	SAU_ScriptToken *t = &o->token;
	t->type = SAU_T_SPECIAL;
	t->data.c = c;
	//putchar(c);
}

static void handle_numeric_value(SAU_Lexer *restrict o,
		uint8_t c sauMaybeUnused) {
	SAU_Scanner *sc = o->sc;
	SAU_ScriptToken *t = &o->token;
	double d;
	SAU_Scanner_ungetc(sc);
	SAU_Scanner_getd(sc, &d, false, NULL, NULL);
	t->type = SAU_T_VAL_REAL;
	t->data.f = d;
	//printf("num == %f\n", d);
}

static void handle_identifier(SAU_Lexer *restrict o,
		uint8_t c sauMaybeUnused) {
	SAU_Scanner *sc = o->sc;
	SAU_ScriptToken *t = &o->token;
	SAU_SymStr *symstr;
	SAU_Scanner_ungetc(sc);
	SAU_Scanner_get_symstr(sc, &symstr);
	t->type = SAU_T_ID_STR;
	t->data.id = symstr ? symstr->key : NULL;
	//printf("str == %s\n", str);
}

/**
 * Get the next token from the current file.
 *
 * Upon end of file, an SAU_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SAU_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SAU_Lexer_get(SAU_Lexer *restrict o, SAU_ScriptToken *restrict t) {
	SAU_Scanner *sc = o->sc;
	uint8_t c;
REGET:
	c = SAU_Scanner_getc(sc);
	switch (c) {
	case 0x00:
		handle_eof(o, c);
		break;
	case SAU_SCAN_LNBRK:
	case SAU_SCAN_SPACE:
		goto REGET;
	case '!':
	case '"':
	case '#':
	case '$':
	case '%':
	case '&':
	case '\'':
	case '(':
	case ')':
	case '*':
	case '+':
	case ',':
	case '-':
	case '.':
	case '/':
		handle_special(o, c);
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		handle_numeric_value(o, c);
		break;
	case ':':
	case ';':
	case '<':
	case '=':
	case '>':
	case '?':
	case '@':
		handle_special(o, c);
		break;
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
	case 'G':
	case 'H':
	case 'I':
	case 'J':
	case 'K':
	case 'L':
	case 'M':
	case 'N':
	case 'O':
	case 'P':
	case 'Q':
	case 'R':
	case 'S':
	case 'T':
	case 'U':
	case 'V':
	case 'W':
	case 'X':
	case 'Y':
	case 'Z':
		handle_identifier(o, c);
		break;
	case '[':
	case '\\':
	case ']':
	case '^':
	case '_':
	case '`':
		handle_special(o, c);
		break;
	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	case 'g':
	case 'h':
	case 'i':
	case 'j':
	case 'k':
	case 'l':
	case 'm':
	case 'n':
	case 'o':
	case 'p':
	case 'q':
	case 'r':
	case 's':
	case 't':
	case 'u':
	case 'v':
	case 'w':
	case 'x':
	case 'y':
	case 'z':
		handle_identifier(o, c);
		break;
	case '{':
	case '|':
	case '}':
	case '~':
		handle_special(o, c);
		break;
	default:
		handle_invalid(o, c);
		break;
	}
	if (t != NULL) {
		*t = o->token;
	}
	return (c != 0);
}

/**
 * Get the next token from the current file. Interprets any visible ASCII
 * character as a special token character.
 *
 * Upon end of file, an SAU_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SAU_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SAU_Lexer_get_special(SAU_Lexer *restrict o,
		SAU_ScriptToken *restrict t) {
	SAU_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		c = SAU_Scanner_getc(sc);
		if (c == 0) {
			handle_eof(o, c);
			break;
		}
		if (IS_VISIBLE(c)) {
			handle_special(o, c);
			break;
		}
	}
	if (t != NULL) {
		*t = o->token;
	}
	return (c != 0);
}
