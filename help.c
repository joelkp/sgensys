/* ssndgen: Help data and printout code.
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

#include "help.h"
#include "ramp.h"
#include "wave.h"
#include <string.h>

const char *const SSG_Help_names[SSG_HELP_TYPES + 1] = {
	"help",
	"ramp",
	"wave",
	NULL
};

/**
 * Get name array for \p str help category.
 *
 * \return predefined array or NULL if none
 */
const char *const *SSG_find_help(const char *restrict str) {
	size_t i;
	if (!SSG_find_name(SSG_Help_names, str, &i))
		return NULL;
	switch (i) {
	case SSG_HELP_HELP:
		return SSG_Help_names;
	case SSG_HELP_RAMP:
		return SSG_Ramp_names;
	case SSG_HELP_WAVE:
		return SSG_Wave_names;
	}
	return NULL;
}

/**
 * Find \p str in \p namearr, setting \p id to the index.
 * If not found, simply return false.
 *
 * \return true if found
 */
bool SSG_find_name(const char *const *restrict namearr,
		const char *restrict str, size_t *restrict id) {
	if (!str)
		return false;
	for (size_t i = 0; namearr[i] != NULL; ++i) {
		if (!strcmp(namearr[i], str)) {
			*id = i;
			return true;
		}
	}
	return false;
}

/**
 * Print strings from \p namearr until a NULL entry is reached.
 *
 * The format for items printed is a comma-separated list. If
 * any items are printed, \p headstr is printed before them,
 * and a newline after them. If no items are printed, nothing
 * is. (A NULL \p headstr is interpreted as an empty string.)
 *
 * \return true if any items printed
 */
bool SSG_print_names(const char *const *restrict namearr,
		const char *restrict headstr,
		FILE *restrict out) {
	if (!namearr[0])
		return false;
	size_t i = 0;
	if (!headstr)
		headstr = "";
	fprintf(out, "%s%s", headstr, namearr[i++]);
	while (namearr[i] != NULL) {
		fprintf(out, ", %s", namearr[i++]);
	}
	putc('\n', out);
	return true;
}
