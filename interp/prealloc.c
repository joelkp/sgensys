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

#include "prealloc.h"

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
		//e->vo_id = SSG_PVO_NO_ID;
		e->vo_id = vo_id;
		e->op_data = prg_e->op_data;
		e->op_data_count = prg_e->op_data_count;
		if (prg_e->vo_data) {
			const SSG_ProgramVoData *pvd = prg_e->vo_data;
			uint32_t params = pvd->params;
			// TODO: Move OpRef stuff to pre-alloc
			if (params & SSG_PVOP_OPLIST) {
				e->op_list = pvd->op_list;
				e->op_count = pvd->op_count;
			}
			o->voices[vo_id].pos = -vo_wait_time;
			vo_wait_time = 0;
			e->vo_data = prg_e->vo_data;
		}
		o->events[i] = e;
	}
	return true;
}

bool SSG_init_PreAlloc(SSG_PreAlloc *restrict o,
		const SSG_Program *restrict prg, uint32_t srate,
		SSG_MemPool *restrict mem) {
	*o = (SSG_PreAlloc){};
	o->prg = prg;
	o->srate = srate;
	o->mem = mem;
	size_t i;

	i = prg->ev_count;
	if (i > 0) {
		o->events = SSG_MemPool_alloc(o->mem,
				i * sizeof(EventNode*));
		if (!o->events) goto ERROR;
		o->ev_count = i;
	}
	i = prg->op_count;
	if (i > 0) {
		o->operators = SSG_MemPool_alloc(o->mem,
				i * sizeof(OperatorNode));
		if (!o->operators) goto ERROR;
		o->op_count = i;
	}
	i = prg->vo_count;
	if (i > 0) {
		o->voices = SSG_MemPool_alloc(o->mem,
				i * sizeof(VoiceNode));
		if (!o->voices) goto ERROR;
		o->vo_count = i;
	}
	o->max_bufs = COUNT_BUFS(prg->op_nest_depth);

	init_operators(o);
	if (!init_events(o)) goto ERROR;
	return true;
ERROR:
	return false;
}

void SSG_fini_PreAlloc(SSG_PreAlloc *restrict o SSG__maybe_unused) {
}
