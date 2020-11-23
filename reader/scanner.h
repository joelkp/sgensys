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

#pragma once
#include "file.h"
#include "symtab.h"

struct SSG_Scanner;
typedef struct SSG_Scanner SSG_Scanner;

/**
 * Number of values for which character handlers are defined.
 *
 * Values below this are given their own function pointer;
 * SSG_Scanner_get_c_filter() handles mapping of other values.
 */
#define SSG_SCAN_CFILTERS 128

/**
 * Function type used for SSG_Scanner_getc() character handlers.
 * Each Scanner instance uses a table of these.
 *
 * The function takes the unsigned character value, processes it and
 * handles any further reading. Must return value to be used, or 0
 * if another read and corresponding handler call should be done.
 *
 * Handler functions may call other handler functions, and are allowed
 * to alter the table.
 *
 * NULL can be used as a function value, meaning that the character
 * read is to be accepted and used as-is.
 */
typedef uint8_t (*SSG_ScanCFilter_f)(SSG_Scanner *o, uint8_t c);

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
	 * Returned for newlines after filtering.
	 * Also used for comparison with SSG_Scanner_tryc().
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
 * Flags set by character handlers.
 */
enum {
	SSG_SCAN_C_ERROR = 1<<0, // character-level error encountered in script
	SSG_SCAN_C_LNBRK = 1<<1, // linebreak scanned last get character call
};

extern const SSG_ScanCFilter_f SSG_Scanner_def_c_filters[SSG_SCAN_CFILTERS];

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
	SSG_SCAN_S_UNGETC = 1<<1, // ungetc done after last getc
};

/**
 * State for character.
 */
typedef struct SSG_ScanCFrame {
	int32_t line_num;
	int32_t char_num;
	uint8_t c;
	uint8_t match_c; // for use by character handlers
	uint8_t flags;
} SSG_ScanCFrame;

/**
 * Scanner type.
 */
struct SSG_Scanner {
	SSG_File *f;
	SSG_SymTab *symtab;
	SSG_ScanCFilter_f *c_filters; // copy of SSG_Scanner_def_c_filters
	SSG_ScanCFrame cf;
	uint8_t s_flags;
	uint8_t *strbuf;
	void *data; // for use by user
};

SSG_Scanner *SSG_create_Scanner(SSG_SymTab *restrict symtab) SSG__malloclike;
void SSG_destroy_Scanner(SSG_Scanner *restrict o);

bool SSG_Scanner_open(SSG_Scanner *restrict o,
		const char *restrict script, bool is_path);
void SSG_Scanner_close(SSG_Scanner *restrict o);

/**
 * Get character handler to call for character \p c,
 * or NULL if the character is simply to be accepted.
 *
 * Values below SSG_SCAN_CFILTERS are assigned the handler for the raw value;
 * other values are assigned the handler for '\0'.
 *
 * \return SSG_ScanCFilter_f or NULL
 */
static inline SSG_ScanCFilter_f SSG_Scanner_getfilter(SSG_Scanner *restrict o,
		uint8_t c) {
	if (c >= SSG_SCAN_CFILTERS) c = 0;
	return o->c_filters[c];
}

uint8_t SSG_Scanner_getc(SSG_Scanner *restrict o);
uint8_t SSG_Scanner_getc_nospace(SSG_Scanner *restrict o);
void SSG_Scanner_ungetc(SSG_Scanner *restrict o);
bool SSG_Scanner_tryc(SSG_Scanner *restrict o, uint8_t testc);
bool SSG_Scanner_get_symstr(SSG_Scanner *restrict o,
		SSG_SymStr **restrict symstrp);

void SSG_Scanner_warning(SSG_Scanner *restrict o,
		const char *restrict fmt, ...) SSG__printflike(2, 3);
void SSG_Scanner_error(SSG_Scanner *restrict o,
		const char *restrict fmt, ...) SSG__printflike(2, 3);
