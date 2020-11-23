/* ssndgen: Main functions and project definitions.
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

#pragma once
#include "program.h"
#include "ptrarr.h"

#define SSG_CLINAME_STR "ssndgen"
#define SSG_VERSION_STR "v0.2-dev"

#define SSG_DEFAULT_SRATE 96000

/**
 * Command line options flags.
 */
enum {
	SSG_ARG_MODE_FULL     = 1<<0,
	SSG_ARG_AUDIO_ENABLE  = 1<<1,
	SSG_ARG_AUDIO_DISABLE = 1<<2,
	SSG_ARG_MODE_CHECK    = 1<<3,
	SSG_ARG_PRINT_INFO    = 1<<4,
	SSG_ARG_EVAL_STRING   = 1<<5,
};

size_t SSG_build(const SSG_PtrArr *restrict script_args, uint32_t options,
		SSG_PtrArr *restrict prg_objs);
void SSG_discard(SSG_PtrArr *restrict prg_objs);

bool SSG_play(const SSG_PtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path);
