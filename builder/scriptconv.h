/* ssndgen: Script data to audio program converter.
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

/*
 * Voice allocation state flags.
 */
enum {
	SSG_VAS_GRAPH = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct SSG_VoAllocState {
	SSG_ScriptEvData *last_ev;
	SSG_ProgramOpGraph *op_graph;
	uint32_t flags;
	uint32_t duration_ms;
} SSG_VoAllocState;

SSG_DEF_ArrType(SSG_VoAlloc, SSG_VoAllocState, _)

/*
 * Operator allocation state flags.
 */
enum {
	SSG_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct SSG_OpAllocState {
	SSG_ScriptOpData *last_sod;
	SSG_ProgramOpAdjcs *adjcs;
	uint32_t flags;
	//uint32_t duration_ms;
} SSG_OpAllocState;

SSG_DEF_ArrType(SSG_OpAlloc, SSG_OpAllocState, _)

SSG_DEF_ArrType(OpRefArr, SSG_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SSG_VoiceGraph {
	OpRefArr op_list;
	SSG_VoAlloc *va;
	SSG_OpAlloc *oa;
	uint32_t op_nest_depth;
} SSG_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void SSG_init_VoiceGraph(SSG_VoiceGraph *restrict o,
		SSG_VoAlloc *restrict va, SSG_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

void SSG_fini_VoiceGraph(SSG_VoiceGraph *restrict o);

bool SSG_VoiceGraph_set(SSG_VoiceGraph *restrict o,
		const SSG_ProgramEvent *restrict ev);
