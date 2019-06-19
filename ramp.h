/* saugns: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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

/**
 * Ramp types.
 */
enum {
	SAU_RAMP_HOLD = 0,
	SAU_RAMP_LIN,
	SAU_RAMP_EXP,
	SAU_RAMP_LOG,
	SAU_RAMP_ESD,
	SAU_RAMP_LSD,
	SAU_RAMP_TYPES
};

/** Names of ramp types, with an extra NULL pointer at the end. */
extern const char *const SAU_Ramp_names[SAU_RAMP_TYPES + 1];

typedef void (*SAU_Ramp_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Curve fill functions for ramp types. */
extern const SAU_Ramp_fill_f SAU_Ramp_fill_funcs[SAU_RAMP_TYPES];

void SAU_Ramp_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Ramp_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Ramp_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Ramp_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Ramp_fill_esd(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Ramp_fill_lsd(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);

/**
 * Ramp parameter flags.
 */
enum {
	SAU_RAMPP_STATE       = 1<<0, // v0 set
	SAU_RAMPP_STATE_RATIO = 1<<1,
	SAU_RAMPP_GOAL        = 1<<2, // vt and time_ms set
	SAU_RAMPP_GOAL_RATIO  = 1<<3,
	SAU_RAMPP_TIME        = 1<<4, // manually used for tracking changes
};

/**
 * Ramp parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SAU_Ramp {
	float v0, vt;
	uint32_t time_ms;
	uint8_t type;
	uint8_t flags;
} SAU_Ramp;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SAU_Ramp_ENABLED(o) \
	((o)->flags & (SAU_RAMPP_STATE | SAU_RAMPP_GOAL))

void SAU_Ramp_reset(SAU_Ramp *restrict o);
void SAU_Ramp_copy(SAU_Ramp *restrict o,
		const SAU_Ramp *restrict src);

bool SAU_Ramp_run(SAU_Ramp *restrict o, uint32_t *restrict pos,
		float *restrict buf, uint32_t buf_len, uint32_t srate,
		const float *restrict mulbuf);
bool SAU_Ramp_skip(SAU_Ramp *restrict o, uint32_t *restrict pos,
		uint32_t skip_len, uint32_t srate);
