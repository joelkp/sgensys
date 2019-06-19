/* saugns: Script data to audio program converter.
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
#include "../script.h"
#include "../program.h"
#include "../arrtype.h"
#include "../mempool.h"

/**
 * Voice allocation state flags.
 */
enum {
	SAU_VAS_GRAPH = 1<<0,
};

/**
 * Per-voice state used during program data allocation.
 */
typedef struct SAU_VoAllocState {
	SAU_ScriptEvData *last_sev;
	const SAU_ProgramOpList *carriers;
	SAU_ProgramVoData *vo_prev;
	uint32_t flags;
	uint32_t duration_ms;
} SAU_VoAllocState;

SAU_DEF_ArrType(SAU_VoAlloc, SAU_VoAllocState, _)

/**
 * Operator allocation state flags.
 */
//enum {
//	SAU_OAS_VISITED = 1<<0,
//};

/**
 * Per-operator state used during program data allocation.
 */
typedef struct SAU_OpAllocState {
	SAU_ScriptOpData *last_sod;
	const SAU_ProgramOpList *mod_lists[SAU_POP_USES - 1];
	SAU_ProgramOpData *op_prev;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

SAU_DEF_ArrType(SAU_OpAlloc, SAU_OpAllocState, _)
