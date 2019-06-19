/* saugns: Audio program interpreter pre-run data allocator.
 * Copyright (c) 2018-2020 Joel K. Pettersson
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
#include "osc.h"
#include "../program.h"
#include "../arrtype.h"
#include "../mempool.h"

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
	ON_TIME_INF = 1<<1, /* used for SAU_TIMEP_LINKED */
};

typedef struct OperatorNode {
	SAU_Osc osc;
	uint32_t time;
	uint32_t silence;
	uint8_t flags;
	const SAU_ProgramOpList *fmods;
	const SAU_ProgramOpList *pmods;
	const SAU_ProgramOpList *amods;
	SAU_Ramp amp, freq;
	SAU_Ramp amp2, freq2;
	uint32_t amp_pos, freq_pos;
	uint32_t amp2_pos, freq2_pos;
} OperatorNode;

/*
 * Voice node flags.
 */
enum {
	VN_INIT = 1<<0,
};

typedef struct VoiceNode {
	int32_t pos; /* negative for wait time */
	uint32_t duration;
	uint8_t flags;
	const SAU_ProgramOpRef *graph;
	uint32_t graph_count;
	SAU_Ramp pan;
	uint32_t pan_pos;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	uint32_t graph_count;
	const SAU_ProgramOpRef *graph;
	const SAU_ProgramEvent *prg_e;
} EventNode;

SAU_DEF_ArrType(SAU_OpRefArr, SAU_ProgramOpRef, )

/*
 * Voice data per event during pre-allocation pass.
 */
typedef struct SAU_VoiceGraph {
	SAU_OpRefArr vo_graph;
	uint32_t nest_level;
	uint32_t nest_max; // for all traversals
} SAU_VoiceGraph;

/*
 * Pre-allocation data. For copying from after filled.
 */
typedef struct SAU_PreAlloc {
	const SAU_Program *prg;
	uint32_t srate;
	size_t ev_count;
	uint32_t op_count;
	uint16_t vo_count;
	uint16_t max_bufs;
	EventNode **events;
	VoiceNode *voices;
	OperatorNode *operators;
	SAU_MemPool *mem;
	SAU_VoiceGraph vg;
} SAU_PreAlloc;

bool SAU_fill_PreAlloc(SAU_PreAlloc *restrict o,
		const SAU_Program *restrict prg, uint32_t srate,
		SAU_MemPool *restrict mem);
