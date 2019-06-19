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

#pragma once
#include "../ramp.h"

#define SAU_MIX_BUFLEN 1024

typedef struct SAU_Mixer {
	float *mix_l, *mix_r;
	float *pan_buf;
	uint32_t srate;
	float scale;
} SAU_Mixer;

SAU_Mixer *SAU_create_Mixer(void);
void SAU_destroy_Mixer(SAU_Mixer *restrict o);

/**
 * Set sample rate used for panning.
 */
static inline void SAU_Mixer_set_srate(SAU_Mixer *restrict o, uint32_t srate) {
	o->srate = srate;
}

/**
 * Set amplitude scaling.
 */
static inline void SAU_Mixer_set_scale(SAU_Mixer *restrict o, float scale) {
	o->scale = scale * 0.5f; // half for panning sum
}

void SAU_Mixer_clear(SAU_Mixer *restrict o);
void SAU_Mixer_add(SAU_Mixer *restrict o,
		float *restrict buf, size_t len,
		SAU_Ramp *restrict pan, uint32_t *restrict pan_pos);
void SAU_Mixer_write(SAU_Mixer *restrict o,
		int16_t **restrict spp, size_t len);
