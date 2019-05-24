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

struct SSG_Lexer {
	SSG_Scanner *sc;
	SSG_SymTab *symtab;
	SSG_ScriptToken token;
};

/**
 * Create instance for the given symbol table.
 *
 * \return instance, or NULL on failure.
 */
SSG_Lexer *SSG_create_Lexer(SSG_SymTab *restrict symtab) {
	if (!symtab)
		return NULL;
	SSG_Lexer *o = calloc(1, sizeof(SSG_Lexer));
	if (!o)
		return NULL;
	o->sc = SSG_create_Scanner(symtab);
	if (!o->sc) goto ERROR;
	o->symtab = symtab;
#if SSG_LEXER_QUIET
	o->sc->s_flags |= SSG_SCAN_S_QUIET;
#endif
	return o;
ERROR:
	SSG_destroy_Lexer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SSG_destroy_Lexer(SSG_Lexer *restrict o) {
	if (!o)
		return;
	SSG_destroy_Scanner(o->sc);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around SSG_Scanner functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool SSG_Lexer_open(SSG_Lexer *restrict o,
		const char *restrict script, bool is_path) {
	return SSG_Scanner_open(o->sc, script, is_path);
}

/**
 * Close file (if open).
 */
void SSG_Lexer_close(SSG_Lexer *restrict o) {
	SSG_Scanner_close(o->sc);
}

static void handle_invalid(SSG_Lexer *o, uint8_t c SSG__maybe_unused) {
	SSG_ScriptToken *t = &o->token;
	t->type = SSG_T_INVALID;
	t->data.b = 0;
}

static void handle_eof(SSG_Lexer *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_Scanner *sc = o->sc;
	SSG_ScriptToken *t = &o->token;
	t->type = SSG_T_INVALID;
	t->data.b = SSG_File_STATUS(sc->f);
	//puts("EOF");
}

static void handle_special(SSG_Lexer *restrict o, uint8_t c) {
	SSG_ScriptToken *t = &o->token;
	t->type = SSG_T_SPECIAL;
	t->data.c = c;
	//putchar(c);
}

static void handle_numeric_value(SSG_Lexer *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_Scanner *sc = o->sc;
	SSG_ScriptToken *t = &o->token;
	double d;
	SSG_Scanner_ungetc(sc);
	SSG_Scanner_getd(sc, &d, false, NULL);
	t->type = SSG_T_VAL_REAL;
	t->data.f = d;
	//printf("num == %f\n", d);
}

static void handle_identifier(SSG_Lexer *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_Scanner *sc = o->sc;
	SSG_ScriptToken *t = &o->token;
	SSG_SymStr *symstr;
	SSG_Scanner_ungetc(sc);
	SSG_Scanner_get_symstr(sc, &symstr);
	t->type = SSG_T_ID_STR;
	t->data.id = symstr ? symstr->key : NULL;
	//printf("str == %s\n", str);
}

/**
 * Get the next token from the current file.
 *
 * Upon end of file, an SSG_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SSG_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SSG_Lexer_get(SSG_Lexer *restrict o, SSG_ScriptToken *restrict t) {
	SSG_Scanner *sc = o->sc;
	uint8_t c;
REGET:
	c = SSG_Scanner_getc_nospace(sc);
	switch (c) {
	case 0x00:
		handle_eof(o, c);
		break;
	case SSG_SCAN_LNBRK:
	case SSG_SCAN_SPACE:
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
 * Upon end of file, an SSG_T_INVALID token is set and false is
 * returned. The field data.b is assigned the file reading status.
 * (If true is returned, an SSG_T_INVALID token simply means that
 * invalid input was successfully registered in the current file.)
 *
 * \return true if a token was successfully read from the file
 */
bool SSG_Lexer_get_special(SSG_Lexer *restrict o,
		SSG_ScriptToken *restrict t) {
	SSG_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		c = SSG_Scanner_getc_nospace(sc);
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
