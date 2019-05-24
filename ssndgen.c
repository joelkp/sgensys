/* ssndgen: Main module / Command-line interface.
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

#include "ssndgen.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define NAME SSG_CLINAME_STR

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: "NAME" [-a|-m] [-r <srate>] [-p] [-o <wavfile>] [-e] <script>...\n"
"       "NAME" [-c] [-p] [-e] <script>...\n"
"\n"
"By default, audio device output is enabled.\n"
"\n"
"  -a \tAudible; always enable audio device output.\n"
"  -m \tMuted; always disable audio device output.\n"
"  -r \tSample rate in Hz (default "SSG_STREXP(SSG_DEFAULT_SRATE)");\n"
"     \tif unsupported for audio device, warns and prints rate used instead.\n"
"  -o \tWrite a 16-bit PCM WAV file, always using the sample rate requested;\n"
"     \tdisables audio device output by default.\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(NAME" "SSG_VERSION_STR);
}

/*
 * Read a positive integer from the given string.
 *
 * \return positive value or -1 if invalid
 */
static int32_t get_piarg(const char *restrict str) {
	char *endp;
	int32_t i;
	errno = 0;
	i = strtol(str, &endp, 10);
	if (errno || i <= 0 || endp == str || *endp)
		return -1;
	return i;
}

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SSG_PtrArr *restrict script_args,
		const char **restrict wav_path,
		uint32_t *restrict srate) {
	int i;
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SSG_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'a':
			if ((*flags & (SSG_ARG_AUDIO_DISABLE |
					SSG_ARG_MODE_CHECK)) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_FULL |
				SSG_ARG_AUDIO_ENABLE;
			break;
		case 'c':
			if ((*flags & SSG_ARG_MODE_FULL) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_CHECK;
			break;
		case 'e':
			*flags |= SSG_ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			goto CLEAR;
		case 'm':
			if ((*flags & (SSG_ARG_AUDIO_ENABLE |
					SSG_ARG_MODE_CHECK)) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_FULL |
				SSG_ARG_AUDIO_DISABLE;
			break;
		case 'o':
			if (arg[1] != '\0') goto INVALID;
			if ((*flags & SSG_ARG_MODE_CHECK) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_FULL;
			--argc;
			++argv;
			if (argc < 1) goto INVALID;
			arg = *argv;
			*wav_path = arg;
			continue;
		case 'p':
			*flags |= SSG_ARG_PRINT_INFO;
			break;
		case 'r':
			if (arg[1] != '\0') goto INVALID;
			if ((*flags & SSG_ARG_MODE_CHECK) != 0)
				goto INVALID;
			*flags |= SSG_ARG_MODE_FULL;
			--argc;
			++argv;
			if (argc < 1) goto INVALID;
			arg = *argv;
			i = get_piarg(arg);
			if (i < 0) goto INVALID;
			*srate = i;
			continue;
		case 'v':
			print_version();
			goto CLEAR;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (script_args->count != 0);
INVALID:
	print_usage(false);
CLEAR:
	SSG_PtrArr_clear(script_args);
	return false;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SSG_PtrArr script_args = (SSG_PtrArr){0};
	SSG_PtrArr prg_objs = (SSG_PtrArr){0};
	const char *wav_path = NULL;
	uint32_t options = 0;
	uint32_t srate = SSG_DEFAULT_SRATE;
	if (!parse_args(argc, argv, &options, &script_args, &wav_path,
			&srate))
		return 0;
	bool error = !SSG_build(&script_args, options, &prg_objs);
	SSG_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		error = !SSG_play(&prg_objs, srate, options, wav_path);
		SSG_discard(&prg_objs);
		if (error)
			return 1;
	}
	return 0;
}
