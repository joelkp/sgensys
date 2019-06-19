/* saugns: Time parameter module.
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
#include "math.h"

/**
 * Time parameter type.
 *
 * Holds data for a generic time parameter.
 */
typedef struct SAU_Time {
	uint32_t v_ms;
	uint8_t flags;
} SAU_Time;

/**
 * Time parameter flags.
 */
enum {
	SAU_TIMEP_SET    = 1<<0, // the \a time_ms value is to be used
	SAU_TIMEP_LINKED = 1<<1, // a linked/"infinite" value is to be used
};

/**
 * Convert time in ms to time in samples for a sample rate.
 */
#define SAU_MS_IN_SAMPLES(ms, srate) \
	lrintf(((ms) * .001f) * (srate))
