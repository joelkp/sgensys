/* ssndgen: Audio program player module.
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

#include "../ssndgen.h"
#include "../interp/generator.h"
#include "audiodev.h"
#include "wavfile.h"
#include "../time.h"
#include <stdlib.h>

#define BUF_TIME_MS  256
#define CH_MIN_LEN   1
#define NUM_CHANNELS 2

typedef struct SSG_Output {
	SSG_AudioDev *ad;
	SSG_WAVFile *wf;
	int16_t *buf;
	uint32_t ad_srate;
	uint32_t options;
	size_t buf_len;
	size_t ch_len;
} SSG_Output;

/*
 * Set up use of audio device and/or WAV file, and buffer of suitable size.
 *
 * \return true unless error occurred
 */
static bool SSG_init_Output(SSG_Output *restrict o, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	bool use_audiodev = (wav_path != NULL) ?
		((options & SSG_ARG_AUDIO_ENABLE) != 0) :
		((options & SSG_ARG_AUDIO_DISABLE) == 0);
	uint32_t ad_srate = srate;
	uint32_t max_srate = srate;
	*o = (SSG_Output){0};
	o->options = options;
	if ((options & SSG_ARG_MODE_CHECK) != 0)
		return true;
	if (use_audiodev) {
		o->ad = SSG_open_AudioDev(NUM_CHANNELS, &ad_srate);
		if (!o->ad)
			return false;
		o->ad_srate = ad_srate;
	}
	if (wav_path != NULL) {
		o->wf = SSG_create_WAVFile(wav_path, NUM_CHANNELS, srate);
		if (!o->wf)
			return false;
	}
	if (ad_srate != srate) {
		if (!o->wf || ad_srate > srate)
			max_srate = ad_srate;
	}

	o->ch_len = SSG_MS_IN_SAMPLES(BUF_TIME_MS, max_srate);
	if (o->ch_len < CH_MIN_LEN) o->ch_len = CH_MIN_LEN;
	o->buf_len = o->ch_len * NUM_CHANNELS;
	o->buf = calloc(o->buf_len, sizeof(int16_t));
	if (!o->buf)
		return false;
	return true;
}

/*
 * \return true unless error occurred
 */
static bool SSG_fini_Output(SSG_Output *restrict o) {
	free(o->buf);
	if (o->ad != NULL) SSG_close_AudioDev(o->ad);
	if (o->wf != NULL)
		return (SSG_close_WAVFile(o->wf) == 0);
	return true;
}

/*
 * Produce audio for program \p prg, optionally sending it
 * to the audio device and/or WAV file.
 *
 * \return true unless error occurred
 */
static bool SSG_Output_run(SSG_Output *restrict o,
		const SSG_Program *restrict prg, uint32_t srate,
		bool use_audiodev, bool use_wavfile) {
	SSG_Generator *gen = SSG_create_Generator(prg, srate);
	if (!gen)
		return false;
	size_t len;
	bool error = false;
	bool run = !(o->options & SSG_ARG_MODE_CHECK);
	use_audiodev = use_audiodev && (o->ad != NULL);
	use_wavfile = use_wavfile && (o->wf != NULL);
	while (run) {
		run = SSG_Generator_run(gen, o->buf, o->ch_len, &len);
		if (use_audiodev && !SSG_AudioDev_write(o->ad, o->buf, len)) {
			error = true;
			SSG_error(NULL, "audio device write failed");
		}
		if (use_wavfile && !SSG_WAVFile_write(o->wf, o->buf, len)) {
			error = true;
			SSG_error(NULL, "WAV file write failed");
		}
	}
	SSG_destroy_Generator(gen);
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
bool SSG_play(const SSG_PtrList *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path) {
	if (!prg_objs->count)
		return true;

	SSG_Output out;
	bool status = true;
	if (!SSG_init_Output(&out, srate, options, wav_path)) {
		status = false;
		goto CLEANUP;
	}
	bool split_gen;
	if (out.ad != NULL && out.wf != NULL && (out.ad_srate != srate)) {
		split_gen = true;
		SSG_warning(NULL,
"generating audio twice, using different sample rates");
	} else {
		split_gen = false;
		if (out.ad != NULL) srate = out.ad_srate;
	}
	const SSG_Program **prgs =
		(const SSG_Program**) SSG_PtrList_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const SSG_Program *prg = prgs[i];
		if (!prg) continue;
		if ((options & SSG_ARG_PRINT_INFO) != 0)
			SSG_Program_print_info(prg);
		if (split_gen) {
			if (!SSG_Output_run(&out, prg, out.ad_srate,
						true, false))
				status = false;
			if (!SSG_Output_run(&out, prg, srate,
						false, true))
				status = false;
		} else {
			if (!SSG_Output_run(&out, prg, srate,
						true, true))
				status = false;
		}
	}

CLEANUP:
	if (!SSG_fini_Output(&out))
		status = false;
	return status;
}
