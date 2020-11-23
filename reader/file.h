/* ssndgen: Text file buffer module.
 * Copyright (c) 2014, 2017-2020 Joel K. Pettersson
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
#include "../common.h"

#define SSG_FILE_ALEN   4096
#define SSG_FILE_ANUM   2
#define SSG_FILE_BUFSIZ (SSG_FILE_ALEN * SSG_FILE_ANUM)

struct SSG_File;
typedef struct SSG_File SSG_File;

/**
 * Action callback type. Must update call position, may change position,
 * and may e.g. handle file reading or writing to/from the buffer in
 * addition. Should return the number of bytes successfully handled.
 */
typedef size_t (*SSG_FileAction_f)(SSG_File *restrict o);

size_t SSG_File_action_wrap(SSG_File *restrict o); // default & EOF'd callback

/**
 * Flip to the beginning of the next buffer area.
 *
 * \return new position
 */
#define SSG_File_ANEXT(o) \
	((o)->pos = ((o)->pos + SSG_FILE_ALEN) \
		& ((SSG_FILE_BUFSIZ - 1) & ~(SSG_FILE_ALEN - 1)))

/**
 * Flip to the next buffer area,
 * maintaining relative position within the area.
 *
 * \return new position
 */
#define SSG_File_AINC(o) \
	((o)->pos = ((o)->pos + SSG_FILE_ALEN) \
		& (SSG_FILE_BUFSIZ - 1))

/**
 * Get position relative to buffer area.
 *
 * \return position
 */
#define SSG_File_APOS(o) \
	((o)->pos & (SSG_FILE_ALEN - 1))

/**
 * Get remaining length (characters after position)
 * within current buffer area.
 *
 * \return length
 */
#define SSG_File_AREM(o) \
	((SSG_FILE_ALEN - 1) - ((o)->pos & (SSG_FILE_ALEN - 1)))

/**
 * Get remaining length (characters after position)
 * within buffer, i.e. before position must wrap.
 *
 * \return length
 */
#define SSG_File_BREM(o) \
	((SSG_FILE_BUFSIZ - 1) - ((o)->pos & (SSG_FILE_BUFSIZ - 1)))

/**
 * True if at call position, prior to calling callback.
 * (The callback is expected to change the call position.)
 */
#define SSG_File_NEED_CALL(o) \
	((o)->pos == (o)->call_pos)

/**
 * Get remaining length (characters after position) before
 * callback position.
 *
 * If position has been decreased, as in e.g. character ungetting,
 * a length longer than that between calls may be returned.
 *
 * \return length
 */
#define SSG_File_CREM(o) \
	(((o)->call_pos - (o)->pos) & (SSG_FILE_BUFSIZ - 1))

/**
 * Increment position without limiting it to the buffer boundary.
 *
 * Mainly useful for advancing position after using a read or write macro
 * which did not advance position.
 *
 * \return new position
 */
#define SSG_File_INCP(o) (++(o)->pos)

/**
 * Decrement position without limiting it to the buffer boundary.
 *
 * Mainly useful for un-advancing position after using a read or write
 * macro which advanced the position.
 *
 * \return new position
 */
#define SSG_File_DECP(o) (--(o)->pos)

/**
 * Wrap position to within the buffer boundary.
 *
 * \return new position
 */
#define SSG_File_FIXP(o) ((o)->pos &= SSG_FILE_BUFSIZ - 1)

/**
 * File reading status constants.
 *
 * When EOF is reached or a file reading error occurs,
 * the relevant flags are set to the status field.
 * The first character after the last one successfully read
 * is then assigned the status as a marker value on each read.
 * The value (the sum of flags) is at most SSG_FILE_MARKER,
 * which is less than a valid character in normal text.
 */
enum {
	SSG_FILE_OK = 0,
	SSG_FILE_END = 1<<0,
	SSG_FILE_ERROR = 1<<1,
	SSG_FILE_CHANGE = 1<<2,
	SSG_FILE_MARKER = 0x07,
};

/**
 * Callback type for closing internal reference.
 * Should close file and set \a ref to NULL, otherwise
 * leaving state unchanged.
 */
typedef void (*SSG_FileClose_f)(SSG_File *restrict o);

/**
 * File type using circular buffer, meant for scanning and parsing.
 * Supports creating sub-instances, e.g. used for nested file includes.
 *
 * Maintains state for moving through the buffer and calling a function
 * to perform action at chosen positions in the buffer.
 *
 * The default callback simply increases and wraps the call position.
 * Opening a file for reading sets a callback to fill the buffer
 * one area at a time.
 */
struct SSG_File {
	size_t pos;
	size_t call_pos;
	SSG_FileAction_f call_f;
	uint8_t status;
	size_t end_pos;
	void *ref;
	const char *path;
	SSG_File *parent;
	SSG_FileClose_f close_f;
	uint8_t buf[SSG_FILE_BUFSIZ];
};

SSG_File *SSG_create_File(void) SSG__malloclike;
SSG_File *SSG_create_sub_File(SSG_File *restrict parent) SSG__malloclike;
SSG_File *SSG_destroy_File(SSG_File *restrict o);

void SSG_File_init(SSG_File *restrict o,
		SSG_FileAction_f call_f, void *restrict ref,
		const char *path, SSG_FileClose_f close_f);

bool SSG_File_fopenrb(SSG_File *restrict o, const char *restrict path);
bool SSG_File_stropenrb(SSG_File *restrict o,
		const char *restrict path, const char *restrict str);

void SSG_File_close(SSG_File *restrict o);
void SSG_File_reset(SSG_File *restrict o);

void SSG_File_end(SSG_File *restrict o, size_t keep_len, bool error);

/**
 * Check position and call callback if at the call position.
 *
 * Wraps position to within the buffer boundary.
 */
#define SSG_File_UPDATE(o) ((void) \
	((SSG_File_FIXP(o), SSG_File_NEED_CALL(o)) \
		&& (o)->call_f((o))))

/**
 * Get current character, without advancing position.
 *
 * \return current character
 */
#define SSG_File_RETC(o) \
	(SSG_File_UPDATE((o)), \
	 (o)->buf[(o)->pos])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, without advancing position.
 *
 * \return current character
 */
#define SSG_File_RETC_NC(o) \
	((o)->buf[(o)->pos])

/**
 * Get current character, advancing position after retrieval.
 *
 * Equivalent to SSG_File_RETC() followed by SSG_File_INCP().
 *
 * \return current character
 */
#define SSG_File_GETC(o) \
	(SSG_File_UPDATE((o)), \
	 (o)->buf[(o)->pos++])

/**
 * Get current character without checking buffer area boundaries
 * nor handling callback, advancing position after retrieval.
 *
 * \return current character
 */
#define SSG_File_GETC_NC(o) \
	((o)->buf[(o)->pos++])

/**
 * Undo the getting of a character.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done up to (SSG_FILE_ALEN - 1) times, plus
 * the number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define SSG_File_UNGETC(o) \
	((o)->pos = ((o)->pos - 1) & (SSG_FILE_BUFSIZ - 1))

/**
 * Compare current character to value \p c, without advancing position.
 *
 * \return true if equal
 */
#define SSG_File_TESTC(o, c) \
	(SSG_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos] == (c)))

/**
 * Compare current character to value \p c, advancing position if equal.
 *
 * Equivalent to SSG_File_TESTC() followed by SSG_File_INCP()
 * when true.
 *
 * \return true if character got
 */
#define SSG_File_TRYC(o, c) \
	(SSG_File_TESTC((o), c) && (SSG_File_INCP((o)), true))

/**
 * Undo the getting of \p n number of characters.
 * Wraps position to within the buffer boundary.
 *
 * Assuming the read callback is called at multiples of the buffer area
 * length, this can safely be done for n <= (SSG_FILE_ALEN - 1) plus the
 * number of characters gotten within the current buffer area.
 *
 * \return new position
 */
#define SSG_File_UNGETN(o, n) \
	((o)->pos = ((o)->pos - (n)) & (SSG_FILE_BUFSIZ - 1))

/**
 * Set current character, without advancing position.
 */
#define SSG_File_SETC(o, c) ((void) \
	(SSG_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, without advancing position.
 */
#define SSG_File_SETC_NC(o, c) ((void) \
	((o)->buf[(o)->pos] = (c)))

/**
 * Set current character, advancing position after write.
 */
#define SSG_File_PUTC(o, c) ((void) \
	(SSG_File_UPDATE((o)), \
	 ((o)->buf[(o)->pos++] = (c))))

/**
 * Set current character without checking buffer area boundaries nor
 * handling callback, advancing position after write.
 */
#define SSG_File_PUTC_NC(o, c) ((void) \
	((o)->buf[(o)->pos++] = (c)))

/**
 * Non-zero if EOF reached or a read error has occurred.
 * The flags set will indicate which.
 *
 * SSG_File_AT_EOF()/SSG_File_AFTER_EOF() can be used
 * to find out if the position at which the exception
 * occurred has been reached.
 */
#define SSG_File_STATUS(o) \
	((o)->status)

/**
 * True if the current position is the one at which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SSG_File_AFTER_EOF() if reading position was incremented)
 * before handling the situation.
 */
#define SSG_File_AT_EOF(o) \
	((o)->end_pos == (o)->pos)

/**
 * True if the current position is the one after which
 * an end marker was inserted into the buffer.
 *
 * A character can be equal to a marker value without the end
 * of the file having been reached, so check using this
 * (or SSG_File_AT_EOF() if reading position wasn't incremented)
 * before handling the situation.
 */
#define SSG_File_AFTER_EOF(o) \
	((o)->end_pos == (((o)->pos - 1) & (SSG_FILE_BUFSIZ - 1)))

/**
 * Get newline in portable way, advancing position if newline read.
 *
 * \return true if newline got
 */
static inline bool SSG_File_trynewline(SSG_File *restrict o) {
	uint8_t c = SSG_File_RETC(o);
	if (c == '\n') {
		SSG_File_INCP(o);
		SSG_File_TRYC(o, '\r');
		return true;
	}
	if (c == '\r') {
		SSG_File_INCP(o);
		return true;
	}
	return false;
}

/**
 * Callback type for filtering characters.
 * Should return the character to use, or 0 to indicate no match.
 */
typedef uint8_t (*SSG_FileFilter_f)(SSG_File *restrict o, uint8_t c);

bool SSG_File_getstr(SSG_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, SSG_FileFilter_f filter_f);
bool SSG_File_geti(SSG_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp);
bool SSG_File_getd(SSG_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp);
size_t SSG_File_skipstr(SSG_File *restrict o, SSG_FileFilter_f filter_f);
size_t SSG_File_skipspace(SSG_File *restrict o);
size_t SSG_File_skipline(SSG_File *restrict o);
