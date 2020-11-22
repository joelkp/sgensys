/* ssndgen: Audio program interpreter pre-run data allocator.
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

#include "prealloc.h"
#include <stdio.h>

/*
 * Voice graph traverser and data allocator.
 */

static bool traverse_op_node(SSG_PreAlloc *restrict o,
		SSG_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool traverse_op_list(SSG_PreAlloc *restrict o,
		const SSG_ProgramOpList *restrict op_list, uint8_t mod_use) {
	if (!op_list) {
		SSG_error("voicegraph", "fail on blank operator list");
		return false;
	}
	SSG_ProgramOpRef op_ref = {0, mod_use, o->vg.nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!traverse_op_node(o, &op_ref))
			return false;
	}
	return true;
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool traverse_op_node(SSG_PreAlloc *restrict o,
		SSG_ProgramOpRef *restrict op_ref) {
	OperatorNode *on = &o->operators[op_ref->id];
	if (on->flags & ON_VISITED) {
		SSG_warning("voicegraph",
"skipping operator %d; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->vg.nest_level > o->vg.nest_max) {
		o->vg.nest_max = o->vg.nest_level;
	}
	++o->vg.nest_level;
	on->flags |= ON_VISITED;
	if (!traverse_op_list(o, on->fmods, SSG_POP_FMOD))
		return false;
	if (!traverse_op_list(o, on->pmods, SSG_POP_PMOD))
		return false;
	if (!traverse_op_list(o, on->amods, SSG_POP_AMOD))
		return false;
	on->flags &= ~ON_VISITED;
	--o->vg.nest_level;
	if (!SSG_OpRefArr_add(&o->vg.vo_graph, op_ref))
		return false;
	return true;
}

/*
 * Create operator graph for voice using data built
 * during allocation, assigning an operator reference
 * list to the voice and block IDs to the operators.
 *
 * \return true, or false on allocation failure
 */
static bool set_voice_graph(SSG_PreAlloc *restrict o,
		const SSG_ProgramVoData *restrict pvd,
		EventNode *restrict ev) {
	if (!pvd->carriers->count) goto DONE;
	if (!traverse_op_list(o, pvd->carriers, SSG_POP_CARR))
		return false;
	if (!SSG_OpRefArr_mpmemdup(&o->vg.vo_graph,
				(SSG_ProgramOpRef**) &ev->graph, o->mem))
		return false;
	ev->graph_count = o->vg.vo_graph.count;
DONE:
	o->vg.vo_graph.count = 0; // re-use allocation
	return true;
}

/*
 * Main interpreter pre-allocation code.
 */

// maximum number of buffers needed for op nesting depth
#define COUNT_BUFS(op_nest_depth) ((1 + (op_nest_depth)) * 7)

static void init_operators(SSG_PreAlloc *restrict o) {
	for (size_t i = 0; i < o->prg->op_count; ++i) {
		OperatorNode *on = &o->operators[i];
		SSG_init_Osc(&on->osc, o->srate);
	}
}

static bool init_events(SSG_PreAlloc *restrict o) {
	const SSG_Program *prg = o->prg;
	uint32_t vo_wait_time = 0;
	for (size_t i = 0; i < prg->ev_count; ++i) {
		const SSG_ProgramEvent *prg_e = prg->events[i];
		EventNode *e = SSG_MemPool_alloc(o->mem, sizeof(EventNode));
		if (!e)
			return false;
		uint16_t vo_id = prg_e->vo_id;
		e->wait = SSG_MS_IN_SAMPLES(prg_e->wait_ms, o->srate);
		vo_wait_time += e->wait;
		e->prg_e = prg_e;
		for (size_t i = 0; i < prg_e->op_data_count; ++i) {
			const SSG_ProgramOpData *od = &prg_e->op_data[i];
			OperatorNode *on = &o->operators[od->id];
			/*
			 * Apply linkage updates for use in init traversal.
			 */
			if (od->fmods != NULL) on->fmods = od->fmods;
			if (od->pmods != NULL) on->pmods = od->pmods;
			if (od->amods != NULL) on->amods = od->amods;
		}
		if (prg_e->vo_data) {
			const SSG_ProgramVoData *pvd = prg_e->vo_data;
			uint32_t params = pvd->params;
			if (params & SSG_PVOP_GRAPH) {
				if (!set_voice_graph(o, pvd, e))
					return false;
			}
			o->voices[vo_id].pos = -vo_wait_time;
			vo_wait_time = 0;
		}
		o->events[i] = e;
	}
	return true;
}

/*
 * Check whether result is to be regarded as usable.
 *
 * \return true, unless invalid data detected
 */
static bool check_validity(SSG_PreAlloc *restrict o) {
	bool error = false;
	if (o->vg.nest_max > UINT8_MAX) {
		fprintf(stderr,
"%s: error: operators nested %d levels, maximum is %d levels\n",
			o->prg->name, o->vg.nest_max, UINT8_MAX);
		error = true;
	}
	return !error;
}

bool SSG_fill_PreAlloc(SSG_PreAlloc *restrict o,
		const SSG_Program *restrict prg, uint32_t srate,
		SSG_MemPool *restrict mem) {
	size_t i;
	bool error = false;
	*o = (SSG_PreAlloc){};
	o->prg = prg;
	o->srate = srate;
	o->mem = mem;
	i = prg->ev_count;
	if (i > 0) {
		o->events = SSG_MemPool_alloc(o->mem,
				i * sizeof(EventNode*));
		if (!o->events) goto MEM_ERR;
		o->ev_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = SSG_MemPool_alloc(o->mem,
				i * sizeof(OperatorNode));
		if (!o->operators) goto MEM_ERR;
		o->op_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = SSG_MemPool_alloc(o->mem,
				i * sizeof(VoiceNode));
		if (!o->voices) goto MEM_ERR;
		o->vo_count = i;
	}

	init_operators(o);
	if (!init_events(o)) goto MEM_ERR;
	if (!check_validity(o)) {
		error = true;
	}
	o->max_bufs = COUNT_BUFS(o->vg.nest_max);
	if (false)
	MEM_ERR: {
		SSG_error("prealloc", "memory allocation failure");
		error = true;
	}
	SSG_OpRefArr_clear(&o->vg.vo_graph);
	return !error;
}
