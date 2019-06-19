/* saugns: Main functions and project definitions.
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

#define SAU_CLINAME_STR "saugns"
#define SAU_VERSION_STR "v0.3-dev"

#define SAU_DEFAULT_SRATE 96000

/**
 * Command line options flags.
 */
enum {
	SAU_ARG_MODE_FULL     = 1<<0,
	SAU_ARG_AUDIO_ENABLE  = 1<<1,
	SAU_ARG_AUDIO_DISABLE = 1<<2,
	SAU_ARG_MODE_CHECK    = 1<<3,
	SAU_ARG_PRINT_INFO    = 1<<4,
	SAU_ARG_EVAL_STRING   = 1<<5,
};

size_t SAU_build(const SAU_PtrArr *restrict script_args, uint32_t options,
		SAU_PtrArr *restrict prg_objs);
void SAU_discard(SAU_PtrArr *restrict prg_objs);

bool SAU_play(const SAU_PtrArr *restrict prg_objs, uint32_t srate,
		uint32_t options, const char *restrict wav_path);
