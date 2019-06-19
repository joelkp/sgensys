/* saugns: Help data and printout code.
 * Copyright (c) 2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once
#include "common.h"
#include <stdio.h>

/**
 * Named help types.
 */
enum {
	SAU_HELP_HELP = 0,
	SAU_HELP_RAMP,
	SAU_HELP_WAVE,
	SAU_HELP_TYPES
};

/** Names of help types, with an extra NULL pointer at the end. */
extern const char *const SAU_Help_names[SAU_HELP_TYPES + 1];

const char *const *SAU_find_help(const char *restrict str);

/*
 * Name array functions of more general use.
 */

bool SAU_find_name(const char *const *restrict namearr,
		const char *restrict str, size_t *restrict id);
bool SAU_print_names(const char *const *restrict namearr,
		const char *restrict headstr,
		FILE *restrict out);
