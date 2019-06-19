/* saugns: Audio program player module.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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

#include "../saugns.h"
#include "../interp/interp.h"
#include "audiodev.h"
#include "wavfile.h"
#include "../time.h"
#include <stdlib.h>

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1
#define NUM_CHANNELS 2

typedef struct SAU_Output {
	SAU_AudioDev *ad;
	SAU_WAVFile *wf;
	int16_t *buf;
	uint32_t ad_srate;
	uint32_t options;
	size_t buf_len;
	size_t ch_len;
} SAU_Output;

/*
 * \return true unless error occurred
 */
static bool SAU_fini_Output(SAU_Output *restrict o) {
	free(o->buf);
	if (o->ad != NULL) SAU_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (SAU_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SAU_init_Output(SAU_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & SAU_ARG_AUDIO_ENABLE) != 0) :
		((options & SAU_ARG_AUDIO_DISABLE) == 0);
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (SAU_Output){0};
	o->options = options;
	if ((options & SAU_ARG_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SAU_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!o->ad) goto ERROR;
		o->ad_srate = ad_srate;
	}
	if (ad_srate != srate) {
		if (!o->wf || ad_srate > srate)
			max_srate = ad_srate;
	}
	o->ch_len = SAU_MS_IN_SAMPLES(BUF_TIME_MS, max_srate);
	if (o->ch_len < CH_MIN_LEN)
		o->ch_len = CH_MIN_LEN;
	o->buf_len = o->ch_len * NUM_CHANNELS;
	o->buf = calloc(o->buf_len, sizeof(int16_t));
	if (!o->buf) goto ERROR;
	if (wav_path != NULL) {
		o->wf = SAU_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!o->wf) goto ERROR;
	}
	return true;
ERROR:
	return SAU_fini_Output(o);
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool SAU_Output_run(SAU_Output *restrict o,
		const SAU_Program *restrict prg,
		bool split_gen, uint32_t other_srate) {
	uint32_t srate = (o->ad != NULL) ? o->ad_srate : other_srate;
	SAU_Interp *gen = SAU_create_Interp(prg, srate);
	if (!gen)
		return false;
	size_t len;
	bool error = false;
	bool run = !(o->options & SAU_ARG_MODE_CHECK);
	if ((o->options & SAU_ARG_PRINT_INFO) != 0)
		SAU_Interp_print(gen);
	if (run && split_gen && (o->ad != NULL)) {
		for (;;) {
			len = SAU_Interp_run(gen, o->buf, o->ch_len);
			if (!len) break;
			if (!SAU_AudioDev_write(o->ad, o->buf, len)) {
				error = true;
				SAU_error(NULL, "audio device write failed");
			}
		}
		SAU_destroy_Interp(gen);
		gen = SAU_create_Interp(prg, other_srate);
		if (!gen)
			return false;
	}
	bool use_audiodev = !split_gen && (o->ad != NULL);
	bool use_wavfile = (o->wf != NULL);
	if (run) for (;;) {
		len = SAU_Interp_run(gen, o->buf, o->ch_len);
		if (!len) break;
		if (use_audiodev && !SAU_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			SAU_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !SAU_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			SAU_error(NULL, "WAV file write failed");
		}
	}
	SAU_destroy_Interp(gen);
	return !error;
}

/**
 * Run the listed programs through the audio generator until completion,
 * ignoring NULL entries.
 *
 * The output is sent to either none, one, or both of the audio device
 * or a WAV file.
 *
 * \return true unless error occurred
 */
bool SAU_play(const SAU_PtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	SAU_Output out;
	if (!SAU_init_Output(&out, srate, options, wav_path))
		return false;
	bool status = true;
	bool split_gen = false;
	if (out.ad != NULL && out.wf != NULL && (out.ad_srate != srate)) {
		split_gen = true;
		SAU_warning(NULL,
"generating audio twice, using different sample rates");
	}
	const SAU_Program **prgs =
		(const SAU_Program**) SAU_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const SAU_Program *prg = prgs[i];
		if (!prg) continue;
		if (!SAU_Output_run(&out, prg, split_gen, srate))
			status = false;
	}
	if (!SAU_fini_Output(&out))
		status = false;
	return status;
}
