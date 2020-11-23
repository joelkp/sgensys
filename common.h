/* ssndgen: Common definitions.
 * Copyright (c) 2011-2012, 2019-2020 Joel K. Pettersson
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

/*
 * Basic types.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Keyword-like macros.
 */

#if defined(__GNUC__) || defined(__clang__)
# define SSG__malloclike __attribute__((malloc))
# define SSG__maybe_unused __attribute__((unused))
# define SSG__noinline __attribute__((noinline))
# define SSG__printflike(string_index, first_to_check) \
	__attribute__((format(printf, string_index, first_to_check)))
#else
# define SSG__malloclike
# define SSG__maybe_unused
# define SSG__noinline
# define SSG__printflike(string_index, first_to_check)
#endif

/*
 * Utility macros.
 */

/** Turn \p arg into string literal before macro-expanding it. */
#define SSG_STRLIT(arg) #arg

/** Turn \p arg into string literal after macro-expanding it. */
#define SSG_STREXP(arg) SSG_STRLIT(arg)

/*
 * Utility functions.
 */

void SSG_warning(const char *restrict label, const char *restrict fmt, ...)
	SSG__printflike(2, 3);
void SSG_error(const char *restrict label, const char *restrict fmt, ...)
	SSG__printflike(2, 3);

void *SSG_memdup(const void *restrict src, size_t size) SSG__malloclike;

/*
 * Debugging options.
 */

/* Debug-friendly memory handling? (Slower.) */
//#define SSG_MEM_DEBUG 1

/* Print symbol table statistics for testing? */
//#define SSG_SYMTAB_STATS 0

/* Make test lexer quiet enough to time it. */
#define SSG_LEXER_QUIET 1

/* Run scanner directly instead of lexer in 'test-scan' program. */
#define SSG_TEST_SCANNER 0
