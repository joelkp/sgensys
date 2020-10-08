/* ssndgen: Audio generator pre-run data allocator.
 * Copyright (c) 2020 Joel K. Pettersson
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
#include "../mempool.h"

/*
 * Operator node flags.
 */
enum {
	ON_VISITED = 1<<0,
	ON_TIME_INF = 1<<1, /* used for SSG_TIMEP_LINKED */
};

typedef struct OperatorNode {
	SSG_Osc osc;
	uint32_t time;
	uint32_t silence;
	uint8_t flags;
	const SSG_ProgramOpList *fmods;
	const SSG_ProgramOpList *pmods;
	const SSG_ProgramOpList *amods;
	SSG_Ramp amp, freq;
	SSG_Ramp amp2, freq2;
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
	const SSG_ProgramOpRef *graph;
	uint32_t graph_count;
	SSG_Ramp pan;
	uint32_t pan_pos;
} VoiceNode;

typedef struct EventNode {
	uint32_t wait;
	uint16_t vo_id;
	const SSG_ProgramOpRef *graph;
	const SSG_ProgramOpData *op_data;
	const SSG_ProgramVoData *vo_data;
	uint32_t graph_count;
	uint32_t op_data_count;
} EventNode;

typedef struct SSG_PreAlloc {
	const SSG_Program *prg;
	uint32_t srate;
	size_t ev_count;
	uint32_t op_count;
	uint16_t vo_count;
	uint16_t max_bufs;
	EventNode **events;
	VoiceNode *voices;
	OperatorNode *operators;
	SSG_MemPool *mem;
} SSG_PreAlloc;

bool SSG_init_PreAlloc(SSG_PreAlloc *restrict o,
		const SSG_Program *restrict prg, uint32_t srate,
		SSG_MemPool *restrict mem);
void SSG_fini_PreAlloc(SSG_PreAlloc *restrict o);
