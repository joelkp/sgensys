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

#pragma once
#include "file.h"
#include "symtab.h"

struct SSG_Scanner;
typedef struct SSG_Scanner SSG_Scanner;

/**
 * Number of values for which character filters are defined.
 *
 * Values below this are given their own function pointer;
 * SSG_Scanner_get_filter() handles mapping of other values.
 */
#define SSG_SCAN_FILTER_COUNT 128

/**
 * Number of old scan positions which can be returned to.
 */
#define SSG_SCAN_UNGET_MAX 63

/**
 * Function type used for filtered character getting.
 * Each Scanner instance uses a table of these.
 *
 * The function takes the raw character value, processes it and
 * may read further (updating the current scan frame) before
 * returning the character to use. May instead return 0 to
 * skip the character and prompt another read (and possibly a
 * corresponding filter call).
 *
 * NULL can be used as a function value, meaning that the character
 * should be used without filtering.
 *
 * Filter functions may call other filter functions,
 * and are allowed to alter the table.
 */
typedef uint8_t (*SSG_ScanFilter_f)(SSG_Scanner *o, uint8_t c);

/**
 * Special character values.
 */
enum {
	/**
	 * Returned for spaces and tabs after filtering.
	 * Also used for comparison with SSG_Scanner_tryc().
	 */
	SSG_SCAN_SPACE = ' ',
	/**
	 * Returned for linebreaks after filtering.
	 * Also used for comparison with SSG_Scanner_tryc()
	 * and SSG_Scanner_tryc_nospace().
	 */
	SSG_SCAN_LNBRK = '\n',
	/**
	 * Used internally. Returned by character filter
	 * to indicate that EOF is reached, error-checking done,
	 * and scanning complete for the file.
	 */
	SSG_SCAN_EOF = 0xFF,
};

/**
 * Flags set by character filters.
 */
enum {
	SSG_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SSG_SCAN_C_LNBRK = 1<<1, // linebreak scanned last get character call
};

extern const SSG_ScanFilter_f SSG_Scanner_def_filters[SSG_SCAN_FILTER_COUNT];

uint8_t SSG_Scanner_filter_invalid(SSG_Scanner *restrict o, uint8_t c);
uint8_t SSG_Scanner_filter_space(SSG_Scanner *restrict o, uint8_t c);
uint8_t SSG_Scanner_filter_linebreaks(SSG_Scanner *restrict o, uint8_t c);
uint8_t SSG_Scanner_filter_linecomment(SSG_Scanner *restrict o, uint8_t c);
uint8_t SSG_Scanner_filter_blockcomment(SSG_Scanner *restrict o,
		uint8_t check_c);
uint8_t SSG_Scanner_filter_slashcomments(SSG_Scanner *restrict o, uint8_t c);
uint8_t SSG_Scanner_filter_char1comments(SSG_Scanner *restrict o, uint8_t c);

/**
 * Scanner state flags for current file.
 */
enum {
	SSG_SCAN_S_ERROR = 1<<0, // true if at least one error has been printed
	SSG_SCAN_S_DISCARD = 1<<1, // don't save scan frame next get
	SSG_SCAN_S_QUIET = 1<<2, // suppress warnings (but still print errors)
};

/**
 * Scan frame with character-level information for a get.
 */
typedef struct SSG_ScanFrame {
	int32_t line_num;
	int32_t char_num;
	uint8_t c;
	uint8_t c_flags;
} SSG_ScanFrame;

/**
 * Scanner type.
 */
struct SSG_Scanner {
	SSG_File *f;
	SSG_SymTab *symtab;
	SSG_ScanFilter_f *filters; // copy of SSG_Scanner_def_filters
	SSG_ScanFrame sf;
	uint8_t undo_pos;
	uint8_t unget_num;
	uint8_t s_flags;
	uint8_t match_c; // for use by character filters
	uint8_t *strbuf;
	void *data; // for use by user
	SSG_ScanFrame undo[SSG_SCAN_UNGET_MAX + 1];
};

SSG_Scanner *SSG_create_Scanner(SSG_SymTab *restrict symtab) SSG__malloclike;
void SSG_destroy_Scanner(SSG_Scanner *restrict o);

bool SSG_Scanner_open(SSG_Scanner *restrict o,
		const char *restrict script, bool is_path);
void SSG_Scanner_close(SSG_Scanner *restrict o);

/**
 * Get character filter to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SSG_SCAN_FILTER_COUNT are assigned the filter for
 * the raw value; other values are assigned the filter for '\0'.
 *
 * \return SSG_ScanFilter_f or NULL
 */
static inline SSG_ScanFilter_f SSG_Scanner_getfilter(SSG_Scanner *restrict o,
		uint8_t c) {
	if (c >= SSG_SCAN_FILTER_COUNT) c = 0;
	return o->filters[c];
}

/**
 * Callback type allowing reading of named constants using SSG_Scanner_getd().
 * Should return non-zero length if number read and \p var set.
 */
typedef size_t (*SSG_ScanNumConst_f)(SSG_Scanner *restrict o,
		double *restrict var);

uint8_t SSG_Scanner_getc(SSG_Scanner *restrict o);
uint8_t SSG_Scanner_getc_nospace(SSG_Scanner *restrict o);
bool SSG_Scanner_tryc(SSG_Scanner *restrict o, uint8_t testc);
bool SSG_Scanner_tryc_nospace(SSG_Scanner *restrict o, uint8_t testc);
uint32_t SSG_Scanner_ungetc(SSG_Scanner *restrict o);
bool SSG_Scanner_geti(SSG_Scanner *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict str_len);
bool SSG_Scanner_getd(SSG_Scanner *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict str_len,
		SSG_ScanNumConst_f numconst_f);
bool SSG_Scanner_get_symstr(SSG_Scanner *restrict o,
		SSG_SymStr **restrict symstrp);

/**
 * Advance past space on the same line.
 * Equivalent to calling SSG_Scanner_tryc() for SSG_SCAN_SPACE.
 */
static inline void SSG_Scanner_skipspace(SSG_Scanner *restrict o) {
	SSG_Scanner_tryc(o, SSG_SCAN_SPACE);
}

/**
 * Advance past whitespace, including linebreaks.
 * Equivalent to calling SSG_Scanner_tryc_nospace() for SSG_SCAN_LNBRK.
 */
static inline void SSG_Scanner_skipws(SSG_Scanner *restrict o) {
	SSG_Scanner_tryc_nospace(o, SSG_SCAN_LNBRK);
}

void SSG_Scanner_warning(const SSG_Scanner *restrict o,
		const SSG_ScanFrame *restrict sf,
		const char *restrict fmt, ...) SSG__printflike(3, 4);
void SSG_Scanner_error(SSG_Scanner *restrict o,
		const SSG_ScanFrame *restrict sf,
		const char *restrict fmt, ...) SSG__printflike(3, 4);
