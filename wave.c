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

#include "wave.h"
#include "math.h"
#include <stdio.h>

#define HALFLEN (SAU_Wave_LEN>>1)

float SAU_Wave_luts[SAU_WAVE_TYPES][SAU_Wave_LEN];

const char *const SAU_Wave_names[SAU_WAVE_TYPES + 1] = {
	"sin",
	"sqr",
	"tri",
	"saw",
	"sha",
	"szh",
	"ssr",
	NULL
};

/*
 * Replacement tanh-based stEP. Square wave version.
 *
 * Replace value range in \p dst with result of
 * tanh for upscaled values from \p src.
 *
 * For use with a sine wave \p src LUT.
 */
static void rep_range_sqr(float *restrict dst, const float *restrict src,
		size_t from, size_t num) {
	/*
	 * Twice the scale factor giving the greatest
	 * aliasing reduction per number of values.
	 *
	 * Used with twice the number of values,
	 * gives a higher-quality result with
	 * very similar frequency roll-off.
	 */
	const float scale = (float) (SAU_Wave_LEN / num);
	for (size_t i = from, end = from + num; i < end; ++i) {
		dst[i] = tanhf(src[i] * scale);
	}
}

/*
 * Replacement tanh-based stEP. Sawtooth wave version.
 *
 * Replace value range in \p dst with result of
 * tanh for upscaled values from \p src.
 *
 * The sample \p skip number reduces the length, and should
 * be chosen to begin filling with the lowest >= 0.f value.
 * This number of samples needs to be zero-filled at the
 * middle of the sawtooth shape in order for the
 * anti-aliasing to properly work.
 *
 * For use with a sine wave \p src LUT.
 */
static void rep_range_saw(float *restrict dst, const float *restrict src,
		size_t from, size_t num, size_t skip) {
	/*
	 * Twice the scale factor giving the greatest
	 * aliasing reduction per number of values.
	 *
	 * Used with twice the number of values,
	 * gives a higher-quality result with
	 * very similar frequency roll-off.
	 *
	 * Requires inserting an extra zero value
	 * at the cycle boundary (e.g. beginning).
	 */
	const float scale = (float) (SAU_Wave_LEN / num);
	for (size_t i = from, end = from + num - skip; i < end; ++i) {
		float s = tanhf(src[i + skip] * scale);
		dst[i] = -1.f + s*2.f;
	}
}

/*
 * Copy values in reverse direction in \p lut
 * from first value range to second value range.
 */
static void hmirror_range(float *restrict lut,
		size_t from, size_t num, size_t offs) {
	for (size_t i = from, end = from + num; i < end; ++i) {
		lut[offs - i] = lut[i];
	}
}

/**
 * Fill in the look-up tables enumerated by SAU_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void SAU_global_init_Wave(void) {
	static bool done = false;
	if (done)
		return;
	done = true;

	float *const sin_lut = SAU_Wave_luts[SAU_WAVE_SIN];
	float *const sqr_lut = SAU_Wave_luts[SAU_WAVE_SQR];
	float *const tri_lut = SAU_Wave_luts[SAU_WAVE_TRI];
	float *const saw_lut = SAU_Wave_luts[SAU_WAVE_SAW];
	float *const sha_lut = SAU_Wave_luts[SAU_WAVE_SHA];
	float *const szh_lut = SAU_Wave_luts[SAU_WAVE_SZH];
	float *const ssr_lut = SAU_Wave_luts[SAU_WAVE_SSR];
	int i;
	const double val_scale = SAU_Wave_MAXVAL;
	const double len_scale = 1.f / HALFLEN;
	/*
	 * First half:
	 *  - sin
	 *  - sqr (fill only)
	 *  - tri
	 *  - ssr
	 */
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * len_scale;
		const double x_rev = (HALFLEN-i) * len_scale;

		const double sin_x = sin(SAU_PI * x);
		sin_lut[i] = val_scale * sin_x;

		sqr_lut[i] = SAU_Wave_MAXVAL;

		if (i < (HALFLEN>>1))
			tri_lut[i] = val_scale * 2.f * x;
		else
			tri_lut[i] = val_scale * 2.f * x_rev;

		ssr_lut[i] = val_scale * sqrtf(sin_x);
	}
	/*
	 * Replacement tanh-based stEP. (An experimental example
	 * of the LUT-value-fiddling approach to anti-aliasing.)
	 *
	 * Replace ideal step function with tanh-of-sin values.
	 * Tuned for nice anti-aliasing at mid frequencies, and
	 * a "nice", yet not too dull sound at low frequencies.
	 * (The "saw" version zeroes some values at the center,
	 * though one zero value is displaced to the beginning.)
	 *
	 * REP and first half:
	 *  - sqr (rep only)
	 *  - saw
	 */
	const int rsqr_len = HALFLEN/32;
	rep_range_sqr(sqr_lut, sin_lut, 0, rsqr_len);
	hmirror_range(sqr_lut, 0, rsqr_len, HALFLEN);
	const int rsaw_len = HALFLEN/16;
	const double saw_scale = 1.f / (HALFLEN - rsaw_len);
	const int saw_skip = 6; // Pick to start with lowest >= 0.f amplitude
	// The skipped saw_lut values are == 0.f
	rep_range_saw(saw_lut+1, sin_lut, 0, rsaw_len, saw_skip);
	for (i = rsaw_len; i < HALFLEN; ++i) {
		const double x = (i - rsaw_len) * saw_scale;

		saw_lut[i+1 - saw_skip] = SAU_Wave_MAXVAL - x;
	}
	/* Second half:
	 *  - sin
	 *  - sqr
	 *  - tri
	 *  - saw
	 *  - ssr
	 */
	for (; i < SAU_Wave_LEN; ++i) {
		sin_lut[i] = -sin_lut[i - HALFLEN];

		sqr_lut[i] = -sqr_lut[i - HALFLEN];

		tri_lut[i] = -tri_lut[i - HALFLEN];

		saw_lut[i] = -saw_lut[(SAU_Wave_LEN-1) - (i-1)];

		ssr_lut[i] = -ssr_lut[i - HALFLEN];
	}
	/* Full cycle:
	 *  - sha
	 *  - szh
	 */
	for (i = 0; i < SAU_Wave_LEN; ++i) {
		const double x = i * len_scale;

		double sha_x = sin((SAU_PI * x) * 0.5f + SAU_ASIN_1_2);
		sha_x = fabs(sha_x) - 0.5f;
		sha_x += sha_x;
		sha_lut[i] = val_scale * sha_x;

		double szh_x = sin((SAU_PI * x) + SAU_ASIN_1_2);
		if (szh_x > 0.f) {
			szh_x -= 0.5f;
			szh_x += szh_x;
			szh_lut[i] = val_scale * szh_x;
		} else {
			szh_lut[i] = -SAU_Wave_MAXVAL;
		}
	}
}

/**
 * Print an index-value table for a LUT.
 */
void SAU_Wave_print(uint8_t id) {
	if (id >= SAU_WAVE_TYPES)
		return;
	const float *lut = SAU_Wave_luts[id];
	const char *lut_name = SAU_Wave_names[id];
	fprintf(stdout, "LUT: %s\n", lut_name);
	for (int i = 0; i < SAU_Wave_LEN; ++i) {
		float v = lut[i];
		fprintf(stdout, "[\t%d]: \t%.11f\n", i, v);
	}
}
