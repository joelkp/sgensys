/* saugns: Audio program interpreter module.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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
#include "../program.h"

struct SAU_Interp;
typedef struct SAU_Interp SAU_Interp;

SAU_Interp *SAU_create_Interp(const SAU_Program *restrict prg,
		uint32_t srate) SAU__malloclike;
void SAU_destroy_Interp(SAU_Interp *restrict o);

size_t SAU_Interp_run(SAU_Interp *restrict o,
		int16_t *restrict buf, size_t buf_len);

void SAU_Interp_print(const SAU_Interp *restrict o);
