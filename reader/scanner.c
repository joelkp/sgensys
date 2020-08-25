/* ssndgen: Script scanner module.
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

#include "scanner.h"
#include "../math.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STRBUF_LEN 256

#if SSG_SCANNER_STATS
static size_t hits = 0;
static size_t misses = 0;
#endif

/**
 * Create instance.
 *
 * Assigns a modifiable copy of the SSG_Scanner_def_filters array,
 * freed when the instance is destroyed.
 *
 * \return instance, or NULL on failure
 */
SSG_Scanner *SSG_create_Scanner(SSG_SymTab *restrict symtab) {
	if (!symtab)
		return NULL;
	SSG_Scanner *o = calloc(1, sizeof(SSG_Scanner));
	if (!o)
		return NULL;
	o->f = SSG_create_File();
	if (!o->f) goto ERROR;
	o->symtab = symtab;
	size_t filters_size = sizeof(SSG_ScanFilter_f) * SSG_SCAN_FILTER_COUNT;
	o->filters = SSG_memdup(SSG_Scanner_def_filters, filters_size);
	if (!o->filters) goto ERROR;
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
	if (!o)
		return;
#if SSG_SCANNER_STATS
	printf("hits: %zd\nmisses: %zd\n", hits, misses);
#endif
	SSG_destroy_File(o->f);
	free(o->strbuf);
	free(o->filters);
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

	o->sf.line_num = 1; // not increased upon first read
	o->sf.char_num = 0;
	o->s_flags |= SSG_SCAN_S_DISCARD;
	return true;
}

/**
 * Close file (if open).
 */
void SSG_Scanner_close(SSG_Scanner *restrict o) {
	SSG_File_close(o->f);
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
		SSG_Scanner_warning(o, NULL,
			"invalid character (value 0x%02hhX)", c);
		return 0;
	}
	uint8_t status = SSG_File_STATUS(f);
	if ((status & SSG_FILE_ERROR) != 0) {
		SSG_Scanner_error(o, NULL,
			"file reading failed");
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
	o->sf.char_num += SSG_File_skipspace(f);
	return SSG_SCAN_SPACE;
}

/**
 * Get characters until the next is not a linebreak.
 *
 * \return SSG_SCAN_LNBRK
 */
uint8_t SSG_Scanner_filter_linebreaks(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	if (c == '\n') SSG_File_TRYC(f, '\r');
	while (SSG_File_trynewline(f)) {
		++o->sf.line_num;
		o->sf.char_num = 0;
	}
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
	o->sf.char_num += SSG_File_skipline(f);
	return SSG_SCAN_SPACE;
}

/**
 * Get characters until encountering \p check_c followed by match_c.
 * Requires setting the match_c field before calling for a character.
 * (See e.g. SSG_Scanner_filter_slashcomments(), which uses this for
 * C-style comments.)
 *
 * Does not set the linebreak flag. Linebreaks within a block comment
 * are ignored (commented out), apart from in line numbering.
 *
 * \return SSG_SCAN_SPACE or SSG_SCAN_EOF (on unterminated comment)
 */
uint8_t SSG_Scanner_filter_blockcomment(SSG_Scanner *restrict o,
		uint8_t check_c) {
	SSG_File *f = o->f;
	int32_t line_num = o->sf.line_num;
	int32_t char_num = o->sf.char_num;
	for (;;) {
		uint8_t c = SSG_File_GETC(f);
		++char_num;
		if (c == '\n') {
			++line_num;
			char_num = 0;
			SSG_File_TRYC(f, '\r');
		} else if (c == '\r') {
			++line_num;
			char_num = 0;
		} else if (c == check_c) {
			if (SSG_File_TRYC(f, o->match_c)) {
				++char_num;
				break; /* end of block comment */
			}
		} else if (c <= SSG_FILE_MARKER && SSG_File_AFTER_EOF(f)) {
			c = SSG_Scanner_filter_invalid(o, c);
			o->sf.c_flags |= SSG_SCAN_C_ERROR;
			--o->sf.char_num; // print for beginning of comment
			SSG_Scanner_error(o, NULL, "unterminated comment");
			++o->sf.char_num;
			return SSG_SCAN_EOF;
		}
	}
	o->sf.line_num = line_num;
	o->sf.char_num = char_num;
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
		++o->sf.char_num;
		o->match_c = '/';
		return SSG_Scanner_filter_blockcomment(o, next_c);
	}
	if (next_c == '/') {
		++o->sf.char_num;
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
	if (o->sf.char_num == 1)
		return SSG_Scanner_filter_linecomment(o, c);
	return c;
}

/**
 * Default array of character filter functions for SSG_Scanner_getc().
 * Each Scanner instance is assigned a copy for which entries may be changed.
 *
 * NULL when the character is simply accepted.
 */
const SSG_ScanFilter_f SSG_Scanner_def_filters[SSG_SCAN_FILTER_COUNT] = {
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
 * Assign scan frame from undo buffer.
 */
static void restore_frame(SSG_Scanner *restrict o, uint32_t offset) {
	uint32_t i = (o->undo_pos - offset) & SSG_SCAN_UNGET_MAX;
	o->sf = o->undo[i];
}

/*
 * Perform pending updates before a get call.
 */
static void prepare_frame(SSG_Scanner *restrict o) {
	if (o->unget_num > 0) {
		/*
		 * Start from frame after the one ungotten to.
		 */
		restore_frame(o, --o->unget_num);
		return;
	}
	if ((o->s_flags & SSG_SCAN_S_DISCARD) != 0) {
		o->s_flags &= ~SSG_SCAN_S_DISCARD;
	} else {
		o->undo_pos = (o->undo_pos + 1) & SSG_SCAN_UNGET_MAX;
	}
	o->undo[o->undo_pos] = o->sf;
	if ((o->sf.c_flags & SSG_SCAN_C_LNBRK) != 0) {
		o->sf.c_flags &= ~SSG_SCAN_C_LNBRK;
		++o->sf.line_num;
		o->sf.char_num = 0;
	}
}

/*
 * Set character used after filtering.
 *
 * Sets the file buffer character before the current to \p c,
 * so that a new get after an undo arrives at \p c.
 */
static void set_usedc(SSG_Scanner *restrict o, uint8_t c) {
	SSG_File *f = o->f;
	size_t r_pos = f->pos;
	o->sf.c = c;
	SSG_File_DECP(f);
	SSG_File_FIXP(f);
	SSG_File_SETC_NC(f, c);
	f->pos = r_pos;
}

/*
 * Perform updates after reading a sequence of characters,
 * e.g. a string or number. Prepares a temporary post-get scan frame.
 */
static void advance_frame(SSG_Scanner *o, size_t strlen, uint8_t c) {
	if (strlen == 0)
		return;
	uint32_t reget_count = (strlen > o->unget_num) ?
		o->unget_num :
		strlen;
	uint32_t char_inc = strlen;
	if (reget_count > 0) {
		/*
		 * Advance past ungets prior to frame to restore to.
		 */
		o->unget_num -= (reget_count - 1);
	}
	prepare_frame(o);
	o->sf.char_num += char_inc;
	o->sf.c = c;
	o->s_flags |= SSG_SCAN_S_DISCARD;
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
	prepare_frame(o);
	for (;;) {
		++o->sf.char_num;
		c = SSG_File_GETC(f);
		SSG_ScanFilter_f filter = SSG_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SSG_SCAN_SPACE) {
			skipped_space = true;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SSG_SCAN_EOF)
		return 0;
	set_usedc(o, c);
	if (skipped_space) {
		/*
		 * Unget a character and store skipped space
		 * before returning it.
		 */
		SSG_File_UNGETC(f);
		--o->sf.char_num;
		set_usedc(o, SSG_SCAN_SPACE);
		return SSG_SCAN_SPACE;
	}
	if (c == SSG_SCAN_LNBRK) {
		o->sf.c_flags |= SSG_SCAN_C_LNBRK;
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
	SSG_ScanFilter_f filter;
	bool skipped_lnbrk = false;
	int32_t old_char_num;
	prepare_frame(o);
	for (;;) {
		++o->sf.char_num;
		c = SSG_File_GETC(f);
		filter = SSG_Scanner_getfilter(o, c);
		if (!filter) break;
		c = filter(o, c);
		if (c == SSG_SCAN_SPACE) continue;
		if (c == SSG_SCAN_LNBRK) {
			skipped_lnbrk = true;
			old_char_num = o->sf.char_num;
			++o->sf.line_num;
			o->sf.char_num = 0;
			continue;
		}
		if (c != 0) break;
	}
	if (c == SSG_SCAN_EOF)
		return 0;
	set_usedc(o, c);
	if (skipped_lnbrk) {
		/*
		 * Unget a character and store skipped linebreak
		 * before returning it.
		 */
		SSG_File_UNGETC(f);
		--o->sf.line_num;
		o->sf.char_num = old_char_num;
		o->sf.c_flags |= SSG_SCAN_C_LNBRK;
		set_usedc(o, SSG_SCAN_LNBRK);
		return SSG_SCAN_LNBRK;
	}
	return c;
}

/**
 * Get next character if it matches \p testc,
 * filtering whitespace like SSG_Scanner_getc().
 *
 * For filtered characters, does a get followed by SSG_Scanner_ungetc()
 * if the characters do not match; characters skipped or changed by the
 * filtering remain skipped or changed for future gets.
 *
 * \return true if character matched \p testc
 */
bool SSG_Scanner_tryc(SSG_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SSG_File_RETC(o->f);
	/*
	 * Use quick handling for unfiltered characters.
	 */
	if (!SSG_Scanner_getfilter(o, c)) {
		if (c != testc)
			return false;
		prepare_frame(o);
		++o->sf.char_num;
		SSG_File_INCP(o->f);
		o->sf.c = c;
		return true;
	}
	c = SSG_Scanner_getc(o);
	if (c != testc) {
		o->s_flags |= SSG_SCAN_S_DISCARD;
		SSG_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Get next character if it matches \p testc,
 * filtering whitespace like SSG_Scanner_getc_nospace().
 *
 * For filtered characters, does a get followed by SSG_Scanner_ungetc()
 * if the characters do not match; characters skipped or changed by the
 * filtering remain skipped or changed for future gets.
 *
 * \return true if character matched \p testc
 */
bool SSG_Scanner_tryc_nospace(SSG_Scanner *restrict o, uint8_t testc) {
	uint8_t c = SSG_File_RETC(o->f);
	/*
	 * Use quick handling for unfiltered characters.
	 */
	if (!SSG_Scanner_getfilter(o, c)) {
		if (c != testc)
			return false;
		prepare_frame(o);
		++o->sf.char_num;
		SSG_File_INCP(o->f);
		o->sf.c = c;
		return true;
	}
	c = SSG_Scanner_getc_nospace(o);
	if (c != testc) {
		o->s_flags |= SSG_SCAN_S_DISCARD;
		SSG_Scanner_ungetc(o);
		return false;
	}
	return true;
}

/**
 * Unget one character and jump to the previous scan frame.
 * The next get will jump back and begin with the last character got.
 *
 * The scan position is assigned from the undo buffer, with up to
 * SSG_SCAN_UNGET_MAX ungets allowed in a row.
 *
 * Allows revisiting a character using a different scanning method.
 *
 * \return number of unget
 */
uint32_t SSG_Scanner_ungetc(SSG_Scanner *restrict o) {
	if (o->unget_num >= SSG_SCAN_UNGET_MAX) {
		SSG_error("scanner",
"Unget function called >%d times in a row; return without action",
			SSG_SCAN_UNGET_MAX);
		return o->unget_num;
	}
	restore_frame(o, ++o->unget_num);
	SSG_File *f = o->f;
	SSG_File_UNGETC(f);
	set_usedc(o, o->sf.c);
	return o->unget_num;
}

/**
 * Read 32-bit signed integer into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SSG_Scanner_geti(SSG_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len) {
	SSG_File *f = o->f;
	size_t read_len;
	prepare_frame(o);
	o->sf.c = SSG_File_RETC(f);
	++o->sf.char_num;
	bool truncated = !SSG_File_geti(f, var, allow_sign, &read_len);
	if (read_len == 0) {
		o->s_flags |= SSG_SCAN_S_DISCARD;
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		SSG_Scanner_warning(o, NULL,
			"value truncated, too large for signed 32-bit int");
	}
	advance_frame(o, read_len - 1, SSG_File_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * If \p str_len is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * If \p numconst_f is not NULL, then after possibly reading a numerical
 * sign, it will be tried, the normal number reading used as fallback if
 * zero returned.
 *
 * \return true unless number too large and result truncated
 */
bool SSG_Scanner_getd(SSG_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		SSG_ScanNumConst_f numconst_f) {
	SSG_File *f = o->f;
	uint8_t c;
	bool sign = false, minus = false;
	size_t read_len;
	prepare_frame(o);
	o->sf.c = c = SSG_File_RETC(f);
	++o->sf.char_num;
	/*
	 * Handle sign here to allow it before named constant too.
	 * Otherwise the same behavior as SSG_File_getd().
	 */
	if (allow_sign && (c == '+' || c == '-')) {
		SSG_File_INCP(f);
		if (c == '-') minus = true;
		c = SSG_File_RETC(f);
		sign = true;
	}
	bool truncated;
	if (numconst_f && (read_len = numconst_f(o, var)) > 0) {
		truncated = false;
	} else {
		truncated = !SSG_File_getd(f, var, false, &read_len);
	}
	if (read_len == 0) {
		if (sign)
			SSG_File_DECP(f);
		o->s_flags |= SSG_SCAN_S_DISCARD;
		if (str_len) *str_len = 0;
		return true;
	}
	if (truncated) {
		SSG_Scanner_warning(o, NULL,
			"value truncated, too large for 64-bit float");
	}
	if (sign) ++read_len;
	if (minus) *var = - *var;
	advance_frame(o, read_len - 1, SSG_File_RETC_NC(f));
	if (str_len) *str_len = read_len;
	return !truncated;
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
	prepare_frame(o);
	o->sf.c = SSG_File_RETC(f);
	++o->sf.char_num;
	truncated = !read_symstr(f, o->strbuf, STRBUF_LEN, &len);
	if (len == 0) {
		o->s_flags |= SSG_SCAN_S_DISCARD;
		*symstrp = NULL;
		return true;
	}

	size_t read_len = len;
	if (truncated) {
		SSG_Scanner_warning(o, NULL,
"limiting identifier to %d characters", (STRBUF_LEN - 1));
		read_len += SSG_File_skipstr(f, filter_symchar);
	}
	advance_frame(o, read_len - 1, SSG_File_RETC_NC(f));

	SSG_SymStr *symstr = SSG_SymTab_get_symstr(o->symtab, o->strbuf, len);
	if (!symstr) {
		SSG_Scanner_error(o, NULL, "failed to register string '%s'",
				o->strbuf);
	}
	*symstrp = symstr;
	return !truncated;
}

static void print_stderr(const SSG_Scanner *restrict o,
		const SSG_ScanFrame *restrict sf,
		const char *restrict prefix, const char *restrict fmt,
		va_list ap) {
	SSG_File *f = o->f;
	if (sf != NULL) {
		fprintf(stderr, "%s:%d:%d: ",
			f->path, sf->line_num, sf->char_num);
	}
	if (prefix != NULL) {
		fprintf(stderr, "%s: ", prefix);
	}
	vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
}

/**
 * Print warning message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 */
void SSG_Scanner_warning(const SSG_Scanner *restrict o,
		const SSG_ScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	if ((o->s_flags & SSG_SCAN_S_QUIET) != 0)
		return;
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, (sf != NULL ? sf : &o->sf),
		"warning", fmt, ap);
	va_end(ap);
}

/**
 * Print error message including file path and position.
 * If \p sf is not NULL, it will be used for position;
 * otherwise, the current position is used.
 *
 * Sets the scanner state error flag.
 */
void SSG_Scanner_error(SSG_Scanner *restrict o,
		const SSG_ScanFrame *restrict sf,
		const char *restrict fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print_stderr(o, (sf != NULL ? sf : &o->sf),
		"error", fmt, ap);
	o->s_flags |= SSG_SCAN_S_ERROR;
	va_end(ap);
}
