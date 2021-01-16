/* saugns: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2021 Joel K. Pettersson
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

#include "ramp.h"
#include "math.h"
#include "time.h"
// the noinline use below works around i386 clang performance issue

const char *const SAU_Ramp_names[SAU_RAMP_TYPES + 1] = {
	"hold",
	"lin",
	"exp",
	"log",
	"esd",
	"lsd",
	NULL
};

const SAU_Ramp_fill_f SAU_Ramp_fill_funcs[SAU_RAMP_TYPES] = {
	SAU_Ramp_fill_hold,
	SAU_Ramp_fill_lin,
	SAU_Ramp_fill_exp,
	SAU_Ramp_fill_log,
	SAU_Ramp_fill_esd,
	SAU_Ramp_fill_lsd,
};

/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
sauNoinline void SAU_Ramp_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(void)vt;
	(void)pos;
	(void)time;
	if (!mulbuf) {
		for (uint32_t i = 0; i < len; ++i)
			buf[i] = v0;
	} else {
		for (uint32_t i = 0; i < len; ++i)
			buf[i] = v0 * mulbuf[i];
	}
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void SAU_Ramp_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float v = v0 + (vt - v0) * (i_pos * inv_time);
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real exponential curve, it has a definite beginning
 * and end. (Uses one of 'esd' or 'lsd', depending on whether
 * the curve rises or falls.)
 */
void SAU_Ramp_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 > vt ?
		SAU_Ramp_fill_esd :
		SAU_Ramp_fill_lsd)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real "log(1 + x)" curve, it has a definite beginning
 * and end. (Uses one of 'esd' or 'lsd', depending on whether
 * the curve rises or falls.)
 */
void SAU_Ramp_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 < vt ?
		SAU_Ramp_fill_esd :
		SAU_Ramp_fill_lsd)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Fill \p buf with \p len values along a trajectory which
 * exponentially saturates and decays (like a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'lsd' type.
 */
void SAU_Ramp_fill_esd(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float mod = 1.f - i_pos * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		float v = vt + (v0 - vt) * mod;
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Fill \p buf with \p len values along a trajectory which
 * logarithmically saturates and decays (opposite of a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'esd' type.
 */
void SAU_Ramp_fill_lsd(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float mod = i_pos * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		float v = v0 + (vt - v0) * mod;
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Set instance to default values.
 *
 * (This does not include values specific to a particular parameter.)
 */
void SAU_Ramp_reset(SAU_Ramp *restrict o) {
	*o = (SAU_Ramp){0};
	o->type = SAU_RAMP_LIN; // default if goal enabled
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SAU_Ramp_copy(SAU_Ramp *restrict o,
		const SAU_Ramp *restrict src) {
	uint8_t mask = 0;
	if ((src->flags & SAU_RAMPP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SAU_RAMPP_STATE
			| SAU_RAMPP_STATE_RATIO;
	}
	if ((src->flags & SAU_RAMPP_GOAL) != 0) {
		o->vt = src->vt;
		o->time_ms = src->time_ms;
		o->type = src->type;
		mask |= SAU_RAMPP_GOAL
			| SAU_RAMPP_GOAL_RATIO
			| SAU_RAMPP_TIME;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/**
 * Fill \p buf with \p buf_len values for the ramp.
 * A value is \a v0 if no goal is set, or a ramping
 * towards \a vt if a goal is set, unless converted
 * from a ratio.
 *
 * If state and/or goal is a ratio, \p mulbuf is
 * used for value multipliers, to get "absolute"
 * values. (If \p mulbuf is NULL, it is ignored,
 * with the same result as if given 1.0 values.)
 * Otherwise \p mulbuf is ignored.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value. This can be forced at any time, as the
 * \p pos can alternatively be NULL to skip all values before.
 *
 * \return true if ramp goal not yet reached
 */
bool SAU_Ramp_run(SAU_Ramp *restrict o, uint32_t *restrict pos,
		float *restrict buf, uint32_t buf_len, uint32_t srate,
		const float *restrict mulbuf) {
	uint32_t len = 0;
	if (!(o->flags & SAU_RAMPP_GOAL)) goto FILL;
	/*
	 * If only one of state and goal is a ratio value,
	 * adjust state value used for state-to-goal fill.
	 */
	if ((o->flags & SAU_RAMPP_GOAL_RATIO) != 0) {
		if (!(o->flags & SAU_RAMPP_STATE_RATIO)) {
			if (mulbuf != NULL) o->v0 /= mulbuf[0];
			o->flags |= SAU_RAMPP_STATE_RATIO;
		}
		/* allow a missing mulbuf */
	} else {
		if ((o->flags & SAU_RAMPP_STATE_RATIO) != 0) {
			if (mulbuf != NULL) o->v0 *= mulbuf[0];
			o->flags &= ~SAU_RAMPP_STATE_RATIO;
		}
		mulbuf = NULL; /* no ratio handling past first value */
	}
	if (!pos) goto REACHED;
	uint32_t time = SAU_MS_IN_SAMPLES(o->time_ms, srate);
	len = time - *pos;
	if (len > buf_len) len = buf_len;
	SAU_Ramp_fill_funcs[o->type](buf, len,
			o->v0, o->vt, *pos, time, mulbuf);
	*pos += len;
	if (*pos == time)
	REACHED: {
		/*
		 * Goal reached; turn into new state value,
		 * filling remaining buffer values with it.
		 */
		o->v0 = o->vt;
		o->flags &= ~(SAU_RAMPP_GOAL | SAU_RAMPP_GOAL_RATIO);
	FILL:
		if (!(o->flags & SAU_RAMPP_STATE_RATIO))
			mulbuf = NULL;
		else if (mulbuf != NULL)
			mulbuf += len;
		SAU_Ramp_fill_hold(buf + len, buf_len - len,
				o->v0, o->v0, 0, 0, mulbuf);
		return false;
	}
	return true;
}

/**
 * Skip ahead \p skip_len values for the ramp, updating state
 * and run position without generating values.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value. This can be forced at any time, as the
 * \p pos can alternatively be NULL to skip all values before.
 *
 * \return true if ramp goal not yet reached
 */
bool SAU_Ramp_skip(SAU_Ramp *restrict o, uint32_t *restrict pos,
		uint32_t skip_len, uint32_t srate) {
	if (!(o->flags & SAU_RAMPP_GOAL))
		return false;
	if (!pos) goto REACHED;
	uint32_t time = SAU_MS_IN_SAMPLES(o->time_ms, srate);
	uint32_t len = time - *pos;
	if (len > skip_len) len = skip_len;
	*pos += len;
	if (*pos == time)
	REACHED: {
		/*
		 * Goal reached; turn into new state value.
		 */
		o->v0 = o->vt;
		if ((o->flags & SAU_RAMPP_GOAL_RATIO) != 0) {
			o->flags |= SAU_RAMPP_STATE_RATIO;
		} else {
			o->flags &= ~SAU_RAMPP_STATE_RATIO;
		}
		o->flags &= ~(SAU_RAMPP_GOAL | SAU_RAMPP_GOAL_RATIO);
		return false;
	}
	return true;
}
