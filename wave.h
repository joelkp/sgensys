/* saugns: Wave module.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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

#define SAU_Wave_LENBITS 11
#define SAU_Wave_LEN     (1<<SAU_Wave_LENBITS) /* 2048 */
#define SAU_Wave_LENMASK (SAU_Wave_LEN - 1)

#define SAU_Wave_MAXVAL 1.f
#define SAU_Wave_MINVAL (-SAU_Wave_MAXVAL)

#define SAU_Wave_SCALEBITS (32-SAU_Wave_LENBITS)
#define SAU_Wave_SCALE     (1<<SAU_Wave_SCALEBITS)
#define SAU_Wave_SCALEMASK (SAU_Wave_SCALE - 1)

/**
 * Wave types.
 */
enum {
	SAU_WAVE_SIN = 0,
	SAU_WAVE_SQR,
	SAU_WAVE_TRI,
	SAU_WAVE_SAW,
	SAU_WAVE_SHA,
	SAU_WAVE_SZH,
	SAU_WAVE_SSR,
	SAU_WAVE_TYPES
};

/** LUTs for wave types. */
extern float SAU_Wave_luts[SAU_WAVE_TYPES][SAU_Wave_LEN];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const SAU_Wave_names[SAU_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SAU_Wave_INDEX(phase) \
	(((uint32_t)(phase)) >> SAU_Wave_SCALEBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline float SAU_Wave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = SAU_Wave_INDEX(phase);
	float s = lut[ind];
	s += (lut[(ind + 1) & SAU_Wave_LENMASK] - s) *
		((phase & SAU_Wave_SCALEMASK) * (1.f / SAU_Wave_SCALE));
	return s;
}

void SAU_global_init_Wave(void);

void SAU_Wave_print(uint8_t id);
