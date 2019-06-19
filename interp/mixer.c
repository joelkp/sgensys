/* saugns: Audio mixer module.
 * Copyright (c) 2019-2020 Joel K. Pettersson
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

#include "mixer.h"
#include "../math.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create instance.
 */
SAU_Mixer *SAU_create_Mixer(void) {
	SAU_Mixer *o = calloc(1, sizeof(SAU_Mixer));
	if (!o)
		return NULL;
	o->mix_l = calloc(SAU_MIX_BUFLEN, sizeof(float));
	if (!o->mix_l) goto ERROR;
	o->mix_r = calloc(SAU_MIX_BUFLEN, sizeof(float));
	if (!o->mix_r) goto ERROR;
	o->pan_buf = calloc(SAU_MIX_BUFLEN, sizeof(float));
	if (!o->pan_buf) goto ERROR;
	SAU_Mixer_set_scale(o, 1.f);
	return o;

ERROR:
	SAU_destroy_Mixer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Mixer(SAU_Mixer *restrict o) {
	if (!o)
		return;
	free(o->mix_l);
	free(o->mix_r);
	free(o->pan_buf);
	free(o);
}

/**
 * Clear the mix buffers.
 */
void SAU_Mixer_clear(SAU_Mixer *restrict o) {
	memset(o->mix_l, 0, sizeof(float) * SAU_MIX_BUFLEN);
	memset(o->mix_r, 0, sizeof(float) * SAU_MIX_BUFLEN);
}

/**
 * Add \p len samples from \p buf into the mix buffers,
 * using \p pan for panning and scaling each sample.
 *
 * Sample rate needs to be set if \p pan has curve enabled.
 */
void SAU_Mixer_add(SAU_Mixer *restrict o,
		float *restrict buf, size_t len,
		SAU_Ramp *restrict pan, uint32_t *restrict pan_pos) {
	if (pan->flags & SAU_RAMPP_GOAL) {
		SAU_Ramp_run(pan, pan_pos, o->pan_buf, len, o->srate, NULL);
		for (size_t i = 0; i < len; ++i) {
			float s = buf[i] * o->scale;
			float s_r = s * o->pan_buf[i];
			o->mix_l[i] += s - s_r;
			o->mix_r[i] += s + s_r;
		}
	} else {
		for (size_t i = 0; i < len; ++i) {
			float s = buf[i] * o->scale;
			float s_r = s * pan->v0;
			o->mix_l[i] += s - s_r;
			o->mix_r[i] += s + s_r;
		}
	}
}

/**
 * Write \p len samples from the mix buffers
 * into a 16-bit stereo (interleaved) buffer
 * pointed to by \p spp. Advances \p spp.
 */
void SAU_Mixer_write(SAU_Mixer *restrict o,
		int16_t **restrict spp, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		float s_l = o->mix_l[i];
		float s_r = o->mix_r[i];
		if (s_l > 1.f) s_l = 1.f;
		else if (s_l < -1.f) s_l = -1.f;
		if (s_r > 1.f) s_r = 1.f;
		else if (s_r < -1.f) s_r = -1.f;
		*(*spp)++ += lrintf(s_l * (float) INT16_MAX);
		*(*spp)++ += lrintf(s_r * (float) INT16_MAX);
	}
}
