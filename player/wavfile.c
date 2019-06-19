/* saugns: WAV file writer module.
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

#include "wavfile.h"
#include <stdio.h>
#include <stdlib.h>

static void fputw(uint16_t i16, FILE *restrict stream) {
	uint8_t b;
	b = i16 & 0xff;
	putc(b, stream);
	b = (i16 >>= 8) & 0xff;
	putc(b, stream);
}

static void fputl(uint32_t i32, FILE *restrict stream) {
	uint8_t b;
	b = i32 & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
	b = (i32 >>= 8) & 0xff;
	putc(b, stream);
}

#define SOUND_BITS 16
#define SOUND_BYTES (SOUND_BITS / 8)

struct SAU_WAVFile {
	FILE *f;
	uint16_t channels;
	uint32_t samples;
};

/**
 * Create 16-bit WAV file for audio output. Sound data may thereafter be
 * written any number of times using SAU_WAVFile_write().
 *
 * \return instance or NULL if fopen fails
 */
SAU_WAVFile *SAU_create_WAVFile(const char *restrict fpath,
		uint16_t channels, uint32_t srate) {
	FILE *f = fopen(fpath, "wb");
	if (!f) {
		SAU_error(NULL, "couldn't open WAV file \"%s\" for writing",
			fpath);
		return NULL;
	}
	SAU_WAVFile *o = malloc(sizeof(SAU_WAVFile));
	o->f = f;
	o->channels = channels;
	o->samples = 0;

	fputs("RIFF", f);
	fputl(36 /* update adding audio data size later */, f);
	fputs("WAVE", f);

	fputs("fmt ", f);
	fputl(16, f); /* fmt-chunk size */
	fputw(1, f); /* format */
	fputw(channels, f);
	fputl(srate, f); /* sample rate */
	fputl(channels * srate * SOUND_BYTES, f); /* byte rate */
	fputw(channels * SOUND_BYTES, f); /* block align */
	fputw(SOUND_BITS, f); /* bits per sample */

	fputs("data", f);
	fputl(0 /* updated with data size later */, f); /* fmt-chunk size */

	return o;
}

/**
 * Write \p samples from \p buf to WAV file. Channels are assumed
 * to be interleaved in the buffer, and the buffer of length
 * (channels * samples).
 *
 * \return true if write successful
 */
bool SAU_WAVFile_write(SAU_WAVFile *restrict o,
		const int16_t *restrict buf, uint32_t samples) {
	uint32_t written;
	written = fwrite(buf, o->channels * SOUND_BYTES, samples, o->f);
	o->samples += written;
	return (written == samples);
}

/**
 * Close file and destroy instance.
 *
 * Updates the WAV file header with the total length/size of
 * audio data written.
 *
 * \return value of ferror, checked before closing file
 */
int SAU_close_WAVFile(SAU_WAVFile *restrict o) {
	int err;
	FILE *f = o->f;
	uint32_t bytes = o->channels * o->samples * SOUND_BYTES;

	fseek(f, 4 /* after "RIFF" */, SEEK_SET);
	fputl(36 + bytes, f);

	fseek(f, 32 /* after "data" */, SEEK_CUR);
	fputl(bytes, f); /* fmt-chunk size */

	err = ferror(f);
	fclose(f);
	free(o);
	return err;
}
