/* ssndgen: Script scanner module.
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

#include "scanner.h"
#include "../math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STRBUF_LEN 256

/**
 * Create instance.
 *
 * Assigns a modifiable copy of the SSG_Scanner_def_c_filters array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SSG_Scanner *SSG_create_Scanner(SSG_SymTab *restrict symtab) {
	if (!symtab) return NULL;

	SSG_Scanner *o = calloc(1, sizeof(SSG_Scanner));
	if (!o) return NULL;
	o->f = SSG_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t filters_size = sizeof(SSG_ScanCFilter_f) * SSG_SCAN_CFILTERS;
	o->c_filters = SSG_memdup(SSG_Scanner_def_c_filters, filters_size);
	if (!o->c_filters) goto ERROR;
	o->strbuf = calloc(1, STRBUF_LEN);
	if (!o->strbuf) goto ERROR;
	return o;

ERROR:
	SSG_destroy_Scanner(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SSG_destroy_Scanner(SSG_Scanner *restrict o) {
	SSG_destroy_File(o->f);
	free(o->strbuf);
	free(o->c_filters);
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
bool SSG_Scanner_open(SSG_Scanner *restrict o,
		const char *restrict script, bool is_path) {
	if (!is_path) {
		SSG_File_stropenrb(o->f, "<string>", script);
	} else if (!SSG_File_fopenrb(o->f, script)) {
		SSG_error(NULL,
"couldn't open script file \"%s\" for reading", script);
		return false;
	}

	o->cf.line_num = 1; // not increased upon first read
	o->cf.char_num = 0;
	return true;
}

/**
 * Close file (if open).
 */
void SSG_Scanner_close(SSG_Scanner *restrict o) {
	SSG_File_close(o->f);
}

enum {
	PRINT_FILE_INFO = 1<<0
};

static void print_stderr(SSG_Scanner *restrict o,
		uint32_t options, const char *restrict prefix,
		const char *restrict fmt, va_list ap) {
	SSG_File *f = o->f;
	if (options & PRINT_FILE_INFO) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, o->cf.line_num, o->cf.char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file path and current position.
 */
void SSG_Scanner_warning(SSG_Scanner *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file path and current position.
 *
 * Sets the scanner state error flag.
 */
void SSG_Scanner_error(SSG_Scanner *restrict o,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, PRINT_FILE_INFO, "error", fmt, ap);
	o->s_flags |= SSG_SCAN_S_ERROR;
	va_end(ap);
}

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

/**
 * Handle invalid character, or the end of the file. Prints
 * an invalid character warning unless the file has ended.
 *
 * Checks file status, returning SSG_SCAN_EOF if the file has ended,
 * and printing a warning upon error.
 *
 * \return 0 or SSG_SCAN_EOF
 */
uint8_t SSG_Scanner_filter_invalid(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	if (!SSG_File_AFTER_EOF(f)) {
		SSG_Scanner_warning(o, "invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = SSG_File_STATUS(f);
	if ((status & SSG_FILE_ERROR) != 0) {
		SSG_Scanner_error(o, "file reading failed");
	}
	return SSG_SCAN_EOF;
}

/**
 * Get characters until the next is neither a space nor a tab.
 *
 * \return SSG_SCAN_SPACE
 */
uint8_t SSG_Scanner_filter_space(SSG_Scanner *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_File *f = o->f;
	o->cf.char_num += SSG_File_skipspace(f);
	return SSG_SCAN_SPACE;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return SSG_SCAN_LNBRK
 */
uint8_t SSG_Scanner_filter_linebreaks(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	++o->cf.line_num;
	if (c == '\n') SSG_File_TRYC(f, '\r');
	while (SSG_File_trynewline(f)) {
		++o->cf.line_num;
	}
	o->cf.char_num = 0;
	return SSG_SCAN_LNBRK;
}

/**
 * Get characters until the next character ends the line (or file).
 *
 * Call for a character to use it as a line comment opener.
 *
 * \return SSG_SCAN_SPACE
 */
uint8_t SSG_Scanner_filter_linecomment(SSG_Scanner *restrict o,
		uint8_t c SSG__maybe_unused) {
	SSG_File *f = o->f;
	o->cf.char_num += SSG_File_skipline(f);
	return SSG_SCAN_SPACE;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. SSG_Scanner_filter_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the newline field, even if the comment contains newlines;
 * any newlines within a block comment are ignored (commented out), apart
 * from in line numbering.
 *
 * \return SSG_SCAN_SPACE or SSG_SCAN_EOF (on unterminated comment)
 */
uint8_t SSG_Scanner_filter_blockcomment(SSG_Scanner *restrict o,
		uint8_t check_c) {
	SSG_File *f = o->f;
	int32_t line_num = o->cf.line_num;
	int32_t char_num = o->cf.char_num;
	for (;;) {
		uint8_t c = SSG_File_GETC(f);
		++char_num;
		if (c == '\n') {
			++line_num;
			SSG_File_TRYC(f, '\r');
			char_num = 0;
		} else if (c == '\r') {
			++line_num;
			char_num = 0;
		} else if (c == check_c) {
			c = SSG_File_GETC(f);
			if (c == o->cf.match_c) {
				++char_num;
				break; /* end of block comment */
			} else {
				SSG_File_DECP(f);
			}
		} else if (c <= SSG_FILE_MARKER && SSG_File_AFTER_EOF(f)) {
			c = SSG_Scanner_filter_invalid(o, c);
			o->cf.flags |= SSG_SCAN_C_ERROR;
			--o->cf.char_num; // print for beginning of comment
			SSG_Scanner_error(o, "unterminated comment");
			++o->cf.char_num;
			return SSG_SCAN_EOF;
		}
	}
	o->cf.line_num = line_num;
	o->cf.char_num = char_num;
	return SSG_SCAN_SPACE;
}

/**
 * Use for '/' (slash) to handle C-style and C++-style comments.
 *
 * Checks the next character for C-style or C++-style comment opener,
 * handling comment if present, otherwise simply returning the first
 * character.
 *
 * \return \p c, SSG_SCAN_SPACE, or SSG_SCAN_EOF (on unterminated comment)
 */
uint8_t SSG_Scanner_filter_slashcomments(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	uint8_t next_c = SSG_File_GETC(f);
	if (next_c == '*') {
		++o->cf.char_num;
		o->cf.match_c = '/';
		return SSG_Scanner_filter_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->cf.char_num;
		return SSG_Scanner_filter_linecomment(o, next_c);
	}
	SSG_File_DECP(f);
	return c;
}

/**
 * If at the beginning of a line, handle line comment.
 * Otherwise, simply return the character.
 *
 * Call for a character to use it as a line comment opener
 * for the first character position only. (For example,
 * git-style comments, or old Fortran comments.)
 *
 * \return \p c or SSG_SCAN_SPACE
 */
uint8_t SSG_Scanner_filter_char1comments(SSG_Scanner *restrict o, uint8_t c) {
	if (o->cf.char_num == 1) return SSG_Scanner_filter_linecomment(o, c);
	return c;
}

/**
 * Default array of character filter functions for SSG_Scanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const SSG_ScanCFilter_f SSG_Scanner_def_c_filters[SSG_SCAN_CFILTERS] = {
	/* NUL 0x00 */ SSG_Scanner_filter_invalid, // also for values above 127
	/* SOH 0x01 */ SSG_Scanner_filter_invalid,
	/* STX 0x02 */ SSG_Scanner_filter_invalid,
	/* ETX 0x03 */ SSG_Scanner_filter_invalid,
	/* EOT 0x04 */ SSG_Scanner_filter_invalid,
	/* ENQ 0x05 */ SSG_Scanner_filter_invalid,
	/* ACK 0x06 */ SSG_Scanner_filter_invalid,
	/* BEL '\a' */ SSG_Scanner_filter_invalid, // SSG_FILE_MARKER
	/* BS  '\b' */ SSG_Scanner_filter_invalid,
	/* HT  '\t' */ SSG_Scanner_filter_space,
	/* LF  '\n' */ SSG_Scanner_filter_linebreaks,
	/* VT  '\v' */ SSG_Scanner_filter_invalid,
	/* FF  '\f' */ SSG_Scanner_filter_invalid,
	/* CR  '\r' */ SSG_Scanner_filter_linebreaks,
	/* SO  0x0E */ SSG_Scanner_filter_invalid,
	/* SI  0x0F */ SSG_Scanner_filter_invalid,
	/* DLE 0x10 */ SSG_Scanner_filter_invalid,
	/* DC1 0x11 */ SSG_Scanner_filter_invalid,
	/* DC2 0x12 */ SSG_Scanner_filter_invalid,
	/* DC3 0x13 */ SSG_Scanner_filter_invalid,
	/* DC4 0x14 */ SSG_Scanner_filter_invalid,
	/* NAK 0x15 */ SSG_Scanner_filter_invalid,
	/* SYN 0x16 */ SSG_Scanner_filter_invalid,
	/* ETB 0x17 */ SSG_Scanner_filter_invalid,
	/* CAN 0x18 */ SSG_Scanner_filter_invalid,
	/* EM  0x19 */ SSG_Scanner_filter_invalid,
	/* SUB 0x1A */ SSG_Scanner_filter_invalid,
	/* ESC 0x1B */ SSG_Scanner_filter_invalid,
	/* FS  0x1C */ SSG_Scanner_filter_invalid,
	/* GS  0x1D */ SSG_Scanner_filter_invalid,
	/* RS  0x1E */ SSG_Scanner_filter_invalid,
	/* US  0x1F */ SSG_Scanner_filter_invalid,
	/*     ' '  */ SSG_Scanner_filter_space,
	/*     '!'  */ NULL,
	/*     '"'  */ NULL,
	/*     '#'  */ SSG_Scanner_filter_linecomment,
	/*     '$'  */ NULL,
	/*     '%'  */ NULL,
	/*     '&'  */ NULL,
	/*     '\'' */ NULL,
	/*     '('  */ NULL,
	/*     ')'  */ NULL,
	/*     '*'  */ NULL,
	/*     '+'  */ NULL,
	/*     ','  */ NULL,
	/*     '-'  */ NULL,
	/*     '.'  */ NULL,
	/*     '/'  */ SSG_Scanner_filter_slashcomments,
	/* num '0'  */ NULL,
	/* num '1'  */ NULL,
	/* num '2'  */ NULL,
	/* num '3'  */ NULL,
	/* num '4'  */ NULL,
	/* num '5'  */ NULL,
	/* num '6'  */ NULL,
	/* num '7'  */ NULL,
	/* num '8'  */ NULL,
	/* num '9'  */ NULL,
	/*     ':'  */ NULL,
	/*     ';'  */ NULL,
	/*     '<'  */ NULL,
	/*     '='  */ NULL,
	/*     '>'  */ NULL,
	/*     '?'  */ NULL,
	/*     '@'  */ NULL,
	/* AL  'A'  */ NULL,
	/* AL  'B'  */ NULL,
	/* AL  'C'  */ NULL,
	/* AL  'D'  */ NULL,
	/* AL  'E'  */ NULL,
	/* AL  'F'  */ NULL,
	/* AL  'G'  */ NULL,
	/* AL  'H'  */ NULL,
	/* AL  'I'  */ NULL,
	/* AL  'J'  */ NULL,
	/* AL  'K'  */ NULL,
	/* AL  'L'  */ NULL,
	/* AL  'M'  */ NULL,
	/* AL  'N'  */ NULL,
	/* AL  'O'  */ NULL,
	/* AL  'P'  */ NULL,
	/* AL  'Q'  */ NULL,
	/* AL  'R'  */ NULL,
	/* AL  'S'  */ NULL,
	/* AL  'T'  */ NULL,
	/* AL  'U'  */ NULL,
	/* AL  'V'  */ NULL,
	/* AL  'W'  */ NULL,
	/* AL  'X'  */ NULL,
	/* AL  'Y'  */ NULL,
	/* AL  'Z'  */ NULL,
	/*     '['  */ NULL,
	/*     '\\' */ NULL,
	/*     ']'  */ NULL,
	/*     '^'  */ NULL,
	/*     '_'  */ NULL,
	/*     '`'  */ NULL,
	/* al  'a'  */ NULL,
	/* al  'b'  */ NULL,
	/* al  'c'  */ NULL,
	/* al  'd'  */ NULL,
	/* al  'e'  */ NULL,
	/* al  'f'  */ NULL,
	/* al  'g'  */ NULL,
	/* al  'h'  */ NULL,
	/* al  'i'  */ NULL,
	/* al  'j'  */ NULL,
	/* al  'k'  */ NULL,
	/* al  'l'  */ NULL,
	/* al  'm'  */ NULL,
	/* al  'n'  */ NULL,
	/* al  'o'  */ NULL,
	/* al  'p'  */ NULL,
	/* al  'q'  */ NULL,
	/* al  'r'  */ NULL,
	/* al  's'  */ NULL,
	/* al  't'  */ NULL,
	/* al  'u'  */ NULL,
	/* al  'v'  */ NULL,
	/* al  'w'  */ NULL,
	/* al  'x'  */ NULL,
	/* al  'y'  */ NULL,
	/* al  'z'  */ NULL,
	/*     '{'  */ NULL,
	/*     '|'  */ NULL,
	/*     '}'  */ NULL,
	/*     '~'  */ NULL,
	/* DEL 0x7F */ SSG_Scanner_filter_invalid,
};

/*
 * Perform pending updates before a get call.
 */
static void update_cframe(SSG_Scanner *restrict o) {
	if ((o->cf.flags & SSG_SCAN_C_LNBRK) != 0) {
		++o->cf.line_num;
		o->cf.char_num = 0;
		o->cf.flags &= ~SSG_SCAN_C_LNBRK;
	}
	o->s_flags &= ~SSG_SCAN_S_UNGETC;
}

/*
 * Set previous character to \p c.
 *
 * Used to make getting after an undo use the character.
 */
static void set_prevc(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	size_t r_pos = f->pos;
	SSG_File_DECP(f);
	SSG_File_FIXP(f);
	SSG_File_SETC_NC(f, c);
	f->pos = r_pos;
}

/**
 * Get next character. Reduces whitespace, returning one space marker
 * for spaces, tabs, and/or comments, and one linebreak marker
 * for linebreaks.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character or 0 upon end of file
 */
uint8_t SSG_Scanner_getc(SSG_Scanner *restrict o) {
	SSG_File *f = o->f;
	uint8_t c;
	bool skipped_space = false;
	update_cframe(o);
	for (;;) {
		++o->cf.char_num;
		c = SSG_File_GETC(f);
		SSG_ScanCFilter_f filter = SSG_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SSG_SCAN_SPACE) {
			skipped_space = true;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SSG_SCAN_EOF) return 0;
	set_prevc(o, c);
	if (skipped_space) {
		/*
		 * Unget a character and store skipped space
		 * before returning it.
		 */
		SSG_File_UNGETC(f);
		--o->cf.char_num;
		set_prevc(o, SSG_SCAN_SPACE);
		return SSG_SCAN_SPACE;
	}
	if (c == SSG_SCAN_LNBRK) {
		o->cf.flags |= SSG_SCAN_C_LNBRK;
	}
	return c;
}

/**
 * Get next character. Removes whitespace, except for a single linebreak
 * marker if linebreaks were filtered.
 *
 * Upon end of file, 0 will be returned. A 0 value in the
 * input is otherwise moved past, printing a warning.
 *
 * \return character or 0 upon end of file
 */
uint8_t SSG_Scanner_getc_nospace(SSG_Scanner *restrict o) {
	SSG_File *f = o->f;
	uint8_t c;
	SSG_ScanCFilter_f filter;
	int32_t old_char_num;
	bool skipped_lnbrk = false;
	update_cframe(o);
	for (;;) {
		++o->cf.char_num;
		c = SSG_File_GETC(f);
		filter = SSG_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SSG_SCAN_SPACE) continue;
		if (c == SSG_SCAN_LNBRK) {
			skipped_lnbrk = true;
			old_char_num = o->cf.char_num;
			++o->cf.line_num;
			o->cf.char_num = 0;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SSG_SCAN_EOF) return 0;
	set_prevc(o, c);
	if (skipped_lnbrk) {
		/*
		 * Unget a character and store skipped linebreak
		 * before returning it.
		 */
		SSG_File_UNGETC(f);
		--o->cf.line_num;
		o->cf.char_num = old_char_num;
		set_prevc(o, SSG_SCAN_LNBRK);
		return SSG_SCAN_LNBRK;
	}
	return c;
}

/**
 * Unget the last character read. This only moves the reading position
 * back one step; any skipped characters (whitespace, etc.) will not be
 * processed again.
 *
 * Useful after getting and examining a character and deciding on a
 * different scanning method, e.g. reading a string.
 *
 * Only meant to be called once in a row; an error is printed without
 * further action if called several times in succession. (Allowing
 * several character ungets would risk parsing errors when moving back
 * past syntactic end markers.)
 */
void SSG_Scanner_ungetc(SSG_Scanner *restrict o) {
	if ((o->s_flags & SSG_SCAN_S_UNGETC) != 0) {
		SSG_Scanner_error(o, "scanner ungetc repeated; return without action");
		return;
	}
	SSG_File *f = o->f;
	SSG_File_UNGETC(f);
	--o->cf.char_num;
	o->s_flags |= SSG_SCAN_S_UNGETC;
}

/**
 * Get next character (filtering whitespace, etc.) if it matches \p testc.
 *
 * Calls SSG_Scanner_ungetc() and returns false if the characters do not
 * match, meaning a new get or try will immediately arrive at the same
 * character. Note that SSG_Scanner_ungetc() cannot be called multiple
 * times in a row, so if false is returned, do not make a direct call to
 * it before further scanning is done.
 *
 * \return true if character matched \p testc
 */
bool SSG_Scanner_tryc(SSG_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SSG_Scanner_getc(o);
	if (c != testc) {
		SSG_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Get identifier string. If a valid symbol string was read,
 * then \a symstr will be set to the unique item
 * stored in the symbol table, otherwise to NULL.
 *
 * \return true if string was short enough to be read in full
 */
bool SSG_Scanner_get_symstr(SSG_Scanner *restrict o,
		SSG_SymStr **restrict symstrp) {
	SSG_File *f = o->f;
	size_t len;
	bool truncated;
	update_cframe(o);
	SSG_File_DECP(f);
	truncated = !read_symstr(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		*symstrp = NULL;
		return true;
	}
	o->cf.char_num += len - 1;
	if (truncated) {
		SSG_Scanner_warning(o,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		o->cf.char_num += SSG_File_skipstr(f, filter_symchar);
	}
	SSG_SymStr *symstr = SSG_SymTab_get_symstr(o->symtab, o->strbuf, len);
	if (!symstr) {
		SSG_Scanner_error(o, "failed to register string '%s'",
				o->strbuf);
	}
	*symstrp = symstr;
	return !truncated;
}
