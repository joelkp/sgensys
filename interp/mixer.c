/* ssndgen: Audio mixer module.
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
SSG_Mixer *SSG_create_Mixer(void) {
	SSG_Mixer *o = calloc(1, sizeof(SSG_Mixer));
	if (!o)
		return NULL;
	o->mix_l = calloc(SSG_MIX_BUFLEN, sizeof(float));
	if (!o->mix_l) goto ERROR;
	o->mix_r = calloc(SSG_MIX_BUFLEN, sizeof(float));
	if (!o->mix_r) goto ERROR;
	o->pan_buf = calloc(SSG_MIX_BUFLEN, sizeof(float));
	if (!o->pan_buf) goto ERROR;
	SSG_Mixer_set_scale(o, 1.f);
	return o;

ERROR:
	SSG_destroy_Mixer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SSG_destroy_Mixer(SSG_Mixer *restrict o) {
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
void SSG_Mixer_clear(SSG_Mixer *restrict o) {
	memset(o->mix_l, 0, sizeof(float) * SSG_MIX_BUFLEN);
	memset(o->mix_r, 0, sizeof(float) * SSG_MIX_BUFLEN);
}

/**
 * Add \p len samples from \p buf into the mix buffers,
 * using \p pan for panning and scaling each sample.
 *
 * Sample rate needs to be set if \p pan has curve enabled.
 */
void SSG_Mixer_add(SSG_Mixer *restrict o,
		float *restrict buf, size_t len,
		SSG_Ramp *restrict pan, uint32_t *restrict pan_pos) {
	if (pan->flags & SSG_RAMPP_GOAL) {
		SSG_Ramp_run(pan, pan_pos, o->pan_buf, len, o->srate, NULL);
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
void SSG_Mixer_write(SSG_Mixer *restrict o,
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
