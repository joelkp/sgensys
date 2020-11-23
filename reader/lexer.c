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
#include "file.h"
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

static uint8_t filter_symchar(SSG_File *restrict o SSG__maybe_unused,
               uint8_t c) {
	return IS_SYMCHAR(c) ? c : 0;
}

/*
 * Read identifier string into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be used to set the string length.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
static bool read_symstr(SSG_File *restrict f,
		uint8_t *restrict buf, size_t buf_len,
		size_t *restrict lenp) {
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SSG_File_GETC(f);
		if (!IS_SYMCHAR(c)) {
			SSG_File_DECP(f);
			break;
		}
		buf[i++] = c;
	}
	buf[i] = '\0';
	if (lenp) *lenp = i;
	return !truncate;
}

/*
 * Lexer implementation.
 */

#define STRBUF_LEN 1024

struct SSG_Lexer {
	SSG_File *f;
	SSG_SymTab *symtab;
	uint32_t line_num, char_num;
	SSG_ScriptToken token;
	uint8_t *strbuf;
};

/**
 * Create instance for the given file and symbol table.
 *
 * \return instance, or NULL on failure.
 */
SSG_Lexer *SSG_create_Lexer(SSG_SymTab *restrict symtab) {
	if (!symtab) return NULL;

	SSG_Lexer *o = calloc(1, sizeof(SSG_Lexer));
	if (!o) return NULL;
	o->f = SSG_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	o->strbuf = calloc(1, STRBUF_LEN);
	if (!o->strbuf) goto ERROR;
	return o;

ERROR:
	SSG_destroy_Lexer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SSG_destroy_Lexer(SSG_Lexer *restrict o) {
	SSG_destroy_File(o->f);
	free(o->strbuf);
	free(o);
}

/**
 * Open file for reading.
 *
 * Wrapper around SSG_File functions. \p script may be
 * either a file path or a string, depending on \p is_path.
 *
 * \return true on success
 */
bool SSG_Lexer_open(SSG_Lexer *restrict o,
		const char *restrict script, bool is_path) {
	if (!is_path) {
		SSG_File_stropenrb(o->f, "<string>", script);
	} else if (!SSG_File_fopenrb(o->f, script)) {
		SSG_error(NULL,
"couldn't open script file \"%s\" for reading", script);
		return false;
	}

	o->line_num = 1; // not increased upon first read
	o->char_num = 0;
	return true;
}

/**
 * Close file (if open).
 */
void SSG_Lexer_close(SSG_Lexer *restrict o) {
	SSG_File_close(o->f);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SSG_Lexer *restrict o,
		uint32_t options, const char *restrict prefix,
		const char *restrict fmt, va_list ap) {
	SSG_File *f = o->f;
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, o->line_num, o->char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file name and current position.
 */
void SSG_Lexer_warning(SSG_Lexer *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file name and current position.
 */
void SSG_Lexer_error(SSG_Lexer *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	va_end(ap);
}

/*
 * Print warning message for an invalid character.
 */
static void warning_character(SSG_Lexer *restrict o, uint8_t c) {
	if (IS_VISIBLE(c)) {
		SSG_Lexer_warning(o, "invalid character: '%c'", (char) c);
	} else {
		SSG_Lexer_warning(o, "invalid character (value 0x%02hhX)", c);
	}
}

typedef uint8_t (*HandleValue_f)(SSG_Lexer *restrict o, uint8_t c);

static uint8_t handle_invalid(SSG_Lexer *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_ScriptToken *t = &o->token;
	t->type = SSG_T_INVALID;
	if (!SSG_File_AFTER_EOF(o->f)) {
		t->data.b = 0;
#if !SSG_LEXER_QUIET
		warning_character(o, c);
#endif
		return 0;
	}
	uint8_t status = SSG_File_STATUS(o->f);
	t->data.b = status;
	if ((status & SSG_FILE_ERROR) != 0) {
		SSG_Lexer_error(o, "file reading failed");
	}
	return 0;
}

static uint8_t handle_blanks(SSG_Lexer *restrict o, uint8_t c) {
	do {
		++o->char_num;
		c = SSG_File_GETC(o->f);
	} while (IS_SPACE(c));
	return c;
}

static uint8_t handle_linebreaks(SSG_Lexer *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	++o->line_num;
	if (c == '\n') SSG_File_TRYC(f, '\r');
	while (SSG_File_trynewline(f)) {
		++o->line_num;
	}
	o->char_num = 1;
	return SSG_File_GETC_NC(f);
}

static uint8_t handle_linecomment(SSG_Lexer *restrict o,
		uint8_t SSG__maybe_unused c) {
	o->char_num += SSG_File_skipline(o->f);
	return SSG_File_GETC_NC(o->f);
}

//static uint8_t handle_block_comment(SSG_Lexer *restrict o, uint8_t c) {
//	SSG_Lexer_warning(o, "cannot yet handle comment marked by '%c'", c);
//	return 0;
//}

static uint8_t handle_special(SSG_Lexer *restrict o, uint8_t c) {
	SSG_ScriptToken *t = &o->token;
	t->type = SSG_T_SPECIAL;
	t->data.c = c;
	return 0;
}

static uint8_t handle_numeric_value(SSG_Lexer *restrict o,
		uint8_t SSG__maybe_unused c) {
	SSG_ScriptToken *t = &o->token;
	double num;
	size_t read_len;
	SSG_File_DECP(o->f);
	SSG_File_getd(o->f, &num, false, &read_len);
	t->type = SSG_T_VAL_REAL;
	t->data.f = num;
	return 0;
}

static uint8_t handle_identifier(SSG_Lexer *restrict o,
		uint8_t SSG__maybe_unused c) {
	SSG_File *f = o->f;
	SSG_ScriptToken *t = &o->token;
	size_t len;
	SSG_File_DECP(f);
	if (!read_symstr(f, o->strbuf, STRBUF_LEN, &len)) {
		SSG_Lexer_warning(o,
"identifier length limited to %d characters", (STRBUF_LEN - 1));
		o->char_num += SSG_File_skipstr(f, filter_symchar);
	}
	const char *pool_str;
	pool_str = SSG_SymTab_pool_str(o->symtab, o->strbuf, len);
	if (pool_str == NULL) {
		SSG_Lexer_error(o, "failed to register string '%s'", o->strbuf);
	}
	o->char_num += len - 1;
	t->type = SSG_T_ID_STR;
	t->data.id = pool_str;
	return 0;
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
	uint8_t c;
	++o->char_num;
	c = SSG_File_GETC(o->f);
	do {
		switch (c) {
		case /* NUL */ 0x00:
		case /* SOH */ 0x01:
		case /* STX */ 0x02:
		case /* ETX */ 0x03:
		case /* EOT */ 0x04:
		case /* ENQ */ 0x05:
		case /* ACK */ 0x06:
		case /* BEL */ '\a': // SSG_FILE_MARKER
		case /* BS  */ '\b':
			c = handle_invalid(o, c);
			break;
		case /* HT  */ '\t':
			c = handle_blanks(o, c);
			break;
		case /* LF  */ '\n':
			c = handle_linebreaks(o, c);
			break;
		case /* VT  */ '\v':
		case /* FF  */ '\f':
			c = handle_invalid(o, c);
			break;
		case /* CR  */ '\r':
			c = handle_linebreaks(o, c);
			break;
		case /* SO  */ 0x0E:
		case /* SI  */ 0x0F:
		case /* DLE */ 0x10:
		case /* DC1 */ 0x11:
		case /* DC2 */ 0x12:
		case /* DC3 */ 0x13:
		case /* DC4 */ 0x14:
		case /* NAK */ 0x15:
		case /* SYN */ 0x16:
		case /* ETB */ 0x17:
		case /* CAN */ 0x18:
		case /* EM  */ 0x19:
		case /* SUB */ 0x1A:
		case /* ESC */ 0x1B:
		case /* FS  */ 0x1C:
		case /* GS  */ 0x1D:
		case /* RS  */ 0x1E:
		case /* US  */ 0x1F:
			c = handle_invalid(o, c);
			break;
		case ' ':
			c = handle_blanks(o, c);
			break;
		case '!':
		case '"':
			c = handle_special(o, c);
			break;
		case '#':
			c = handle_linecomment(o, c);
			break;
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
			c = handle_special(o, c);
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
			c = handle_numeric_value(o, c);
			break;
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '@':
			c = handle_special(o, c);
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
			c = handle_identifier(o, c);
			break;
		case '[':
		case '\\':
		case ']':
		case '^':
		case '_':
		case '`':
			c = handle_special(o, c);
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
			c = handle_identifier(o, c);
			break;
		case '{':
		case '|':
		case '}':
		case '~':
			c = handle_special(o, c);
			break;
		case /* DEL */ 0x7F:
		default:
			c = handle_invalid(o, c);
			break;
		}
	} while (c != 0);
	if (t != NULL) {
		*t = o->token;
	}
	return (o->token.type == SSG_T_INVALID) ?
		(o->token.data.b == 0) :
		true;
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
	uint8_t c;
	++o->char_num;
	c = SSG_File_GETC(o->f);
	do {
		if (IS_VISIBLE(c)) c = handle_special(o, c);
		else c = handle_invalid(o, c);
	} while (c != 0);
	if (t != NULL) {
		*t = o->token;
	}
	return (o->token.type == SSG_T_INVALID) ?
		(o->token.data.b == 0) :
		true;
}
