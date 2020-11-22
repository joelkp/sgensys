/* ssndgen: Audio program interpreter module.
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

struct SSG_Interp;
typedef struct SSG_Interp SSG_Interp;

SSG_Interp *SSG_create_Interp(const SSG_Program *restrict prg,
		uint32_t srate) SSG__malloclike;
void SSG_destroy_Interp(SSG_Interp *restrict o);

size_t SSG_Interp_run(SSG_Interp *restrict o,
		int16_t *restrict buf, size_t buf_len);

void SSG_Interp_print(const SSG_Interp *restrict o);
