/* saugns: Text file buffer module.
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

#include "file.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Default callback. Moves through the circular buffer in
 * one of two ways, depending on whether or not file status
 * has SAU_FILE_END set.
 *
 * If clear, increases call position to the beginning
 * of the next buffer area, wrapping it to within the
 * buffer boundary.
 *
 * If set, instead calls SAU_File_end(), writing out the
 * end marker to the current character in the buffer and
 * increasing the wrapped position by one.
 *
 * \return call position difference in clear case, or 0
 */
size_t SAU_File_action_wrap(SAU_File *restrict o) {
	if (o->status & SAU_FILE_END) {
		SAU_File_end(o, 0, false); // repeat end marker
		return 0;
	}
	size_t skip_len = o->call_pos - (o->call_pos & ~(SAU_FILE_ALEN - 1));
	size_t len = SAU_FILE_ALEN - skip_len;
	o->call_pos = (o->call_pos + len) & (SAU_FILE_BUFSIZ - 1);
	return len;
}

/**
 * Reset all state other than buffer contents.
 * Used for opening and closing.
 */
void SAU_File_init(SAU_File *restrict o,
		SAU_FileAction_f call_f, void *restrict ref,
		const char *path, SAU_FileClose_f close_f) {
	if (o->close_f != NULL) o->close_f(o);

	o->pos = 0;
	o->call_pos = 0;
	o->call_f = call_f;
	o->status = SAU_FILE_OK;
	o->end_pos = (size_t) -1;
	o->ref = ref;
	o->path = path;
	o->close_f = close_f;
}

/**
 * Create instance. Sets the default callback.
 */
SAU_File *SAU_create_File(void) {
	SAU_File *o = calloc(1, sizeof(SAU_File));
	if (!o)
		return NULL;
	SAU_File_init(o, SAU_File_action_wrap, NULL, NULL, NULL);
	return o;
}

/**
 * Create instance with parent. Sets the default callback.
 */
SAU_File *SAU_create_sub_File(SAU_File *restrict parent) {
	if (!parent)
		return NULL;
	SAU_File *o = SAU_create_File();
	if (!o)
		return NULL;
	o->parent = parent;
	return o;
}

/**
 * Destroy instance. Closes file if open.
 *
 * \return parent instance or NULL
 */
SAU_File *SAU_destroy_File(SAU_File *restrict o) {
	if (!o)
		return NULL;

	if (o->close_f != NULL) o->close_f(o);
	SAU_File *parent = o->parent;
	free(o);
	return parent;
}

static size_t mode_fread(SAU_File *restrict o);
static size_t mode_strread(SAU_File *restrict o);

static void ref_fclose(SAU_File *restrict o);

/**
 * Open stdio file for reading.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed when EOF or a read error occurs,
 * but \a path is only cleared with a new open call or a call
 * to SAU_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool SAU_File_fopenrb(SAU_File *restrict o, const char *restrict path) {
	if (!path)
		return false;
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	SAU_File_init(o, mode_fread, f, path, ref_fclose);
	return true;
}

/**
 * Open string as file for reading. The string must be NULL-terminated.
 * The path is optional and only used to name the file.
 * (If a file was already opened, it is closed on success.)
 *
 * The file is automatically closed upon a NULL byte,
 * but \a path is only cleared with a new open call or a call
 * to SAU_File_reset(), so as to remain available for printing.
 *
 * \return true on success
 */
bool SAU_File_stropenrb(SAU_File *restrict o,
		const char *restrict path, const char *restrict str) {
	if (!str)
		return false;
	SAU_File_init(o, mode_strread, (void*) str, path, NULL);
	return true;
}

/**
 * Close and clear internal reference if open. Sets SAU_FILE_END status
 * and restores the callback to SAU_File_action_wrap(). If there is a
 * parent file instance, will set SAU_FILE_CHANGE status.
 *
 * Leaves buffer contents and remaining parts of state untouched.
 *
 * Automatically used by SAU_File_end().
 * Re-opening also automatically closes.
 */
void SAU_File_close(SAU_File *restrict o) {
	if (o->status & SAU_FILE_END)
		return;
	o->status |= SAU_FILE_END;
	if (o->parent != NULL)
		o->status |= SAU_FILE_CHANGE;
	if (o->close_f != NULL) {
		o->close_f(o);
		o->close_f = NULL;
	}
	o->ref = NULL;
	o->call_pos = (o->pos + 1) & (SAU_FILE_BUFSIZ - 1);
	o->call_f = SAU_File_action_wrap;
}

/**
 * Reset state. Closes if open, clears file status (to SAU_FILE_OK),
 * and zeroes the buffer.
 */
void SAU_File_reset(SAU_File *restrict o) {
	SAU_File_init(o, SAU_File_action_wrap, NULL, NULL, NULL);
	memset(o->buf, 0, SAU_FILE_BUFSIZ);
}

/**
 * Mark currently opened file as ended. Used automatically on and
 * after EOF, but can also be called manually to act as if EOF
 * follows the current buffer contents.
 *
 * The first time this is called for an open file, will call
 * SAU_File_close(), which will close the internal reference
 * and reset the callback. If \p error is true, will
 * additionally set SAU_FILE_ERROR status beforehand.
 *
 * On each call, an end marker will be written \p keep_len bytes
 * after the current position in the buffer. The callback call
 * position is set to the position after the marker.
 */
void SAU_File_end(SAU_File *restrict o, size_t keep_len, bool error) {
	SAU_File_close(o);
	if (error)
		o->status |= SAU_FILE_ERROR;
	o->end_pos = (o->pos + keep_len) & (SAU_FILE_BUFSIZ - 1);
	o->buf[o->end_pos] = o->status;
	o->call_pos = (o->end_pos + 1) & (SAU_FILE_BUFSIZ - 1);
}

/*
 * Read up to a buffer area of data from a stdio file.
 * Closes file upon EOF or read error.
 *
 * Upon short read, inserts SAU_File_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_fread(SAU_File *restrict o) {
	FILE *f = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (SAU_FILE_BUFSIZ - 1) & ~(SAU_FILE_ALEN - 1);
	len = fread(&o->buf[o->pos], 1, SAU_FILE_ALEN, f);
	o->call_pos = (o->pos + len) & (SAU_FILE_BUFSIZ - 1);
	if (len < SAU_FILE_ALEN) SAU_File_end(o, len, ferror(f) != 0);
	return len;
}

/*
 * Read up to a buffer area of data from a string, advancing
 * the pointer, unless the string is NULL. Closes file
 * (setting the string to NULL) upon NULL byte.
 *
 * Upon short read, inserts SAU_File_STATUS() value
 * not counted in return length as an end marker.
 * If the file is closed, further calls will reset the
 * reading position and write the end marker again.
 *
 * \return number of characters successfully read
 */
static size_t mode_strread(SAU_File *restrict o) {
	const char *str = o->ref;
	size_t len;
	// Move to and fill at the first character of the buffer area.
	o->pos &= (SAU_FILE_BUFSIZ - 1) & ~(SAU_FILE_ALEN - 1);
	len = strlen(str);
	if (len >= SAU_FILE_ALEN) {
		len = SAU_FILE_ALEN;
		o->ref = &((char*)o->ref)[len];
		o->call_pos = (o->pos + len) & (SAU_FILE_BUFSIZ - 1);
	} else {
		SAU_File_end(o, len, false);
	}
	memcpy(&o->buf[o->pos], str, len);
	return len;
}

/*
 * Close stdio file without clearing state.
 */
static void ref_fclose(SAU_File *restrict o) {
	if (o->ref != NULL) {
		fclose(o->ref);
		o->ref = NULL;
	}
}

#define IS_SPACE(c) ((c) == ' ' || (c) == '\t')
#define IS_LNBRK(c) ((c) == '\n' || (c) == '\r')
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

/**
 * Read characters into \p buf. At most \p buf_len - 1 characters
 * are read, and the string is always NULL-terminated.
 *
 * If \p lenp is not NULL, it will be set to the string length.
 * If \p filter_f is not NULL, it will be used to filter characters
 * and end the string when 0 is returned; otherwise, characters
 * will be read until the buffer is filled or the file ends.
 *
 * \return true if the string fit into the buffer, false if truncated
 */
bool SAU_File_getstr(SAU_File *restrict o,
		void *restrict buf, size_t buf_len,
		size_t *restrict lenp, SAU_FileFilter_f filter_f) {
	uint8_t *dst = buf;
	size_t i = 0;
	size_t max_len = buf_len - 1;
	bool truncate = false;
	if (filter_f != NULL) for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = filter_f(o, SAU_File_GETC(o));
		if (c == '\0') {
			SAU_File_DECP(o);
			break;
		}
		dst[i++] = c;
	} else for (;;) {
		if (i == max_len) {
			truncate = true;
			break;
		}
		uint8_t c = SAU_File_GETC(o);
		if (c <= SAU_FILE_MARKER && SAU_File_AFTER_EOF(o)) {
			SAU_File_DECP(o);
			break;
		}
		dst[i++] = c;
	}
	dst[i] = '\0';
	if (lenp) *lenp = i;
	return !truncate;
}

/**
 * Read integer into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits.
 *
 * If \p lenp is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SAU_File_geti(SAU_File *restrict o,
		int32_t *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	int32_t num = 0;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = SAU_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SAU_File_GETC(o);
		++len;
	}
	if (!IS_DIGIT(c)) {
		SAU_File_UNGETN(o, len);
		if (lenp) *lenp = 0;
		return true;
	}
	if (minus) {
		do {
			int32_t new_num = num * 10 - (c - '0');
			if (new_num > num) truncate = true;
			else num = new_num;
			c = SAU_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MIN;
	} else {
		do {
			int32_t new_num = num * 10 + (c - '0');
			if (new_num < num) truncate = true;
			else num = new_num;
			c = SAU_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (truncate) num = INT32_MAX;
	}
	*var = num;
	SAU_File_DECP(o);
	--len;
	if (lenp) *lenp = len;
	return !truncate;
}

/**
 * Read double-precision floating point number into \p var.
 *
 * Expects the number to begin at the current position.
 * The number sub-string must have the form:
 * optional sign, then digits and/or point followed by digits.
 *
 * If \p lenp is not NULL, it will be set to the number of characters
 * read. 0 implies that no number was read and that \p var is unchanged.
 *
 * \return true unless number too large and result truncated
 */
bool SAU_File_getd(SAU_File *restrict o,
		double *restrict var, bool allow_sign,
		size_t *restrict lenp) {
	uint8_t c;
	long double num = 0.f, pos_mul = 1.f;
	double res;
	bool minus = false;
	bool truncate = false;
	size_t len = 0;
	c = SAU_File_GETC(o);
	++len;
	if (allow_sign && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		c = SAU_File_GETC(o);
		++len;
	}
	if (c != '.') {
		if (!IS_DIGIT(c)) {
			SAU_File_UNGETN(o, len);
			if (lenp) *lenp = 0;
			return true;
		}
		do {
			num = num * 10.f + (c - '0');
			c = SAU_File_GETC(o);
			++len;
		} while (IS_DIGIT(c));
		if (c != '.') goto DONE;
		c = SAU_File_GETC(o);
		++len;
	} else {
		c = SAU_File_GETC(o);
		++len;
		if (!IS_DIGIT(c)) {
			SAU_File_UNGETN(o, len);
			if (lenp) *lenp = 0;
			return true;
		}
	}
	while (IS_DIGIT(c)) {
		pos_mul *= 0.1f;
		num += (c - '0') * pos_mul;
		c = SAU_File_GETC(o);
		++len;
	}
DONE:
	res = (double) num;
	if (isinf(res)) truncate = true;
	if (minus) res = -res;
	*var = res;
	SAU_File_DECP(o);
	--len;
	if (lenp) *lenp = len;
	return !truncate;
}

/**
 * Advance past characters until \p filter_f returns zero.
 *
 * \return number of characters skipped
 */
size_t SAU_File_skipstr(SAU_File *restrict o, SAU_FileFilter_f filter_f) {
	size_t i = 0;
	for (;;) {
		uint8_t c = filter_f(o, SAU_File_GETC(o));
		if (c == '\0') break;
		++i;
	}
	SAU_File_DECP(o);
	return i;
}

/**
 * Advance past characters until the next is neither a space nor a tab.
 *
 * \return number of characters skipped
 */
size_t SAU_File_skipspace(SAU_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = SAU_File_GETC(o);
		if (!IS_SPACE(c)) break;
		++i;
	}
	SAU_File_DECP(o);
	return i;
}

/**
 * Advance past characters until the next marks the end of the line (or file).
 *
 * \return number of characters skipped
 */
size_t SAU_File_skipline(SAU_File *restrict o) {
	size_t i = 0;
	for (;;) {
		uint8_t c = SAU_File_GETC(o);
		if (IS_LNBRK(c) ||
			(c <= SAU_FILE_MARKER && SAU_File_AFTER_EOF(o))) break;
		++i;
	}
	SAU_File_DECP(o);
	return i;
}
