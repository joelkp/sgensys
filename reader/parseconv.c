/* saugns: Parser output to script data converter.
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

#include "parser.h"

/*
 * Script data construction from parse data.
 *
 * Adjust and replace data structures. The per-event
 * operator list becomes flat, with separate lists kept for
 * recursive traversal in scriptconv.
 */

/*
 * Adjust timing for a duration group; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void time_durgroup(SAU_ParseEvData *restrict e_last) {
	SAU_ParseDurGroup *dur = e_last->dur;
	SAU_ParseEvData *e, *e_after = e_last->next;
	uint32_t wait = 0, waitcount = 0;
	for (e = dur->range.first; e != e_after; ) {
		for (SAU_ParseOpData *op = e->operators.first;
				op != NULL; op = op->range_next) {
			if (wait < op->time.v_ms)
				wait = op->time.v_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = dur->range.first; e != e_after; ) {
		for (SAU_ParseOpData *op = e->operators.first;
				op != NULL; op = op->range_next) {
			if (!(op->time.flags & SAU_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = wait + waitcount;
				op->time.flags |= SAU_TIMEP_SET;
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static inline void time_ramp(SAU_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SAU_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static void time_operator(SAU_ParseOpData *restrict op) {
	SAU_ParseEvData *e = op->event;
	if ((op->op_flags & SAU_PDOP_NESTED) != 0 &&
			!(op->time.flags & SAU_TIMEP_SET)) {
		if (!(op->op_flags & SAU_PDOP_HAS_COMPOSITE))
			op->time.flags |= SAU_TIMEP_LINKED;
		op->time.flags |= SAU_TIMEP_SET;
	}
	if (!(op->time.flags & SAU_TIMEP_LINKED)) {
		time_ramp(&op->freq, op->time.v_ms);
		time_ramp(&op->freq2, op->time.v_ms);
		time_ramp(&op->amp, op->time.v_ms);
		time_ramp(&op->amp2, op->time.v_ms);
		if (!(op->op_flags & SAU_PDOP_SILENCE_ADDED)) {
			op->time.v_ms += op->silence_ms;
			op->op_flags |= SAU_PDOP_SILENCE_ADDED;
		}
	}
	if ((e->ev_flags & SAU_PDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			e->next->wait_ms += op->time.v_ms;
		e->ev_flags &= ~SAU_PDEV_ADD_WAIT_DURATION;
	}
	for (SAU_ParseSublist *scope = op->nest_scopes;
			scope != NULL; scope = scope->next) {
		SAU_ParseOpData *sub_op = scope->range.first;
		for (; sub_op != NULL; sub_op = sub_op->range_next) {
			time_operator(sub_op);
		}
	}
}

static void time_event(SAU_ParseEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	// e->pan.flags |= SAU_RAMPP_TIME; // TODO: revisit semantics
	SAU_ParseOpData *op;
	op = e->operators.first;
	for (; op != NULL; op = op->range_next) {
		time_operator(op);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SAU_ParseEvData *ce = e->composite;
		SAU_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = ce->operators.first;
		ce_op_prev = ce_op->prev;
		e_op = ce_op_prev;
		e_op->time.flags |= SAU_TIMEP_SET; /* always used from now on */
		for (;;) {
			ce->wait_ms += ce_op_prev->time.v_ms;
			if (!(ce_op->time.flags & SAU_TIMEP_SET)) {
				ce_op->time.flags |= SAU_TIMEP_SET;
				if ((ce_op->op_flags &
(SAU_PDOP_NESTED | SAU_PDOP_HAS_COMPOSITE)) == SAU_PDOP_NESTED)
					ce_op->time.flags |= SAU_TIMEP_LINKED;
				else
					ce_op->time.v_ms = ce_op_prev->time.v_ms
						- ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time.flags & SAU_TIMEP_LINKED)
				e_op->time.flags |= SAU_TIMEP_LINKED;
			else if (!(e_op->time.flags & SAU_TIMEP_LINKED))
				e_op->time.v_ms += ce_op->time.v_ms +
					(ce->wait_ms - ce_op_prev->time.v_ms);
			ce_op->op_params &= ~SAU_POPP_TIME;
			ce_op_prev = ce_op;
			ce = ce->next;
			if (!ce) break;
			ce_op = ce->operators.first;
		}
	}
}

/*
 * Deals with events that are "composite" (attached to a main event as
 * successive "sub-events" rather than part of the big, linear event sequence).
 *
 * Such events, if attached to the passed event, will be given their place in
 * the ordinary event list.
 */
static void flatten_events(SAU_ParseEvData *restrict e) {
	SAU_ParseEvData *ce = e->composite;
	SAU_ParseEvData *se = e->next, *se_prev = e;
	uint32_t wait_ms = 0;
	uint32_t added_wait_ms = 0;
	while (ce != NULL) {
		if (!se) {
			/*
			 * No more events in the ordinary sequence,
			 * so append all composites.
			 */
			se_prev->next = ce;
			break;
		}
		/*
		 * If several events should pass in the ordinary sequence
		 * before the next composite is inserted, skip ahead.
		 */
		wait_ms += se->wait_ms;
		if (se->next && (wait_ms + se->next->wait_ms)
				<= (ce->wait_ms + added_wait_ms)) {
			se_prev = se;
			se = se->next;
			continue;
		}
		/*
		 * Insert next composite before or after
		 * the next event of the ordinary sequence.
		 */
		SAU_ParseEvData *ce_next = ce->next;
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
		} else {
			SAU_ParseEvData *se_next = se->next;
			ce->wait_ms -= wait_ms;
			added_wait_ms += ce->wait_ms;
			wait_ms = 0;
			se->next = ce;
			ce->next = se_next;
			se_prev = ce;
			se = se_next;
		}
		ce = ce_next;
	}
	e->composite = NULL;
}

typedef struct ParseConv {
	SAU_ScriptEvData *ev, *first_ev;
	SAU_MemPool *mem, *tmp;
} ParseConv;

/*
 * Per-operator data pointed to by all its nodes during conversion.
 */
typedef struct OpContext {
	SAU_ParseOpData *last_use;
} OpContext;

/*
 * Get operator context for node, updating associated data.
 *
 * If the node is ignored, the SAU_PDOP_IGNORED flag is set
 * before returning NULL. A NULL context means either error
 * or an ignored node.
 *
 * \return instance, or NULL on allocation failure or ignored node
 */
static OpContext *ParseConv_update_opcontext(ParseConv *restrict o,
		SAU_ScriptOpData *restrict od,
		SAU_ParseOpData *restrict pod) {
	OpContext *oc = NULL;
	if (!pod->prev) {
		oc = SAU_MemPool_alloc(o->tmp, sizeof(OpContext));
		if (!oc)
			return NULL;
		if (pod->use_type == SAU_POP_CARR) {
			SAU_ScriptEvData *e = o->ev;
			e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
			od->op_flags |= SAU_SDOP_ADD_CARRIER;
		}
	} else {
		oc = pod->prev->op_context;
		if (!oc) {
			/*
			 * This can happen if earlier nodes were excluded,
			 * in which case all follow-ons nodes will also be
			 * ignored.
			 */
			pod->op_flags |= SAU_PDOP_IGNORED;
			return NULL;
		}
		if (pod->use_type == SAU_POP_CARR) {
			od->op_flags |= SAU_SDOP_ADD_CARRIER;
		}
		SAU_ScriptOpData *prev_use = oc->last_use->op_conv;
		od->prev_use = prev_use;
		prev_use->next_use = od;
	}
	oc->last_use = pod;
	pod->op_context = oc;
	return oc;
}

/*
 * Per-voice data pointed to by all its nodes during conversion.
 */
typedef struct VoContext {
	SAU_ParseEvData *last_vo_use;
} VoContext;

/*
 * Convert data for an operator node to script operator data,
 * adding it to the list to be used for the current script event.
 */
static bool ParseConv_add_opdata(ParseConv *restrict o,
		SAU_ParseOpData *restrict pod) {
	SAU_ScriptOpData *od = SAU_MemPool_alloc(o->mem,
			sizeof(SAU_ScriptOpData));
	if (!od) goto ERROR;
	SAU_ScriptEvData *e = o->ev;
	pod->op_conv = od;
	od->event = e;
	/* next_bound */
	/* op_flags */
	od->op_params = pod->op_params;
	od->time = pod->time;
	od->silence_ms = pod->silence_ms;
	od->wave = pod->wave;
	od->freq = pod->freq;
	od->freq2 = pod->freq2;
	od->amp = pod->amp;
	od->amp2 = pod->amp2;
	od->phase = pod->phase;
	if (!ParseConv_update_opcontext(o, od, pod)) goto ERROR;
	if (!e->op_all.first)
		e->op_all.first = od;
	else
		((SAU_ScriptOpData*) e->op_all.last)->range_next = od;
	e->op_all.last = od;
	return true;
ERROR:
	return false;
}

/*
 * Recursively create needed operator data nodes,
 * visiting new operator nodes as they branch out.
 */
static bool ParseConv_add_ops(ParseConv *restrict o,
		const SAU_NodeRange *restrict pod_list) {
	if (!pod_list)
		return true;
	SAU_ParseOpData *pod = pod_list->first;
	for (; pod != NULL; pod = pod->range_next) {
		// TODO: handle multiple operator nodes
		if (pod->op_flags & SAU_PDOP_MULTIPLE) {
			// TODO: handle multiple operator nodes
			pod->op_flags |= SAU_PDOP_IGNORED;
			continue;
		}
		if (!ParseConv_add_opdata(o, pod)) {
			if (pod->op_flags & SAU_PDOP_IGNORED) continue;
			goto ERROR;
		}
		for (SAU_ParseSublist *scope = pod->nest_scopes;
				scope != NULL; scope = scope->next) {
			if (!ParseConv_add_ops(o, &scope->range)) goto ERROR;
		}
	}
	return true;
ERROR:
	return false;
}

/*
 * Recursively fill in lists for operator node graph,
 * visiting all linked operator nodes as they branch out.
 */
static bool ParseConv_link_ops(ParseConv *restrict o,
		SAU_RefList *restrict *od_list,
		const SAU_NodeRange *restrict pod_list,
		uint8_t list_type) {
	if (!pod_list)
		return true;
	SAU_ScriptEvData *e = o->ev;
	if (list_type != SAU_POP_CARR ||
			(e->ev_flags & SAU_SDEV_NEW_OPGRAPH) != 0) {
		*od_list = SAU_create_RefList(list_type, o->mem);
		if (!*od_list) goto ERROR;
	}
	SAU_ParseOpData *pod = pod_list->first;
	for (; pod != NULL; pod = pod->range_next) {
		if (pod->op_flags & SAU_PDOP_IGNORED) continue;
		SAU_ScriptOpData *od = pod->op_conv;
		if (!od) goto ERROR;
		if ((list_type != SAU_POP_CARR ||
				 ((e->ev_flags & SAU_SDEV_NEW_OPGRAPH) &&
				  (od->op_flags & SAU_SDOP_ADD_CARRIER))) &&
				!SAU_RefList_add(*od_list, od, 0, o->mem))
			goto ERROR;
		SAU_RefList *last_mod_list = NULL;
		for (SAU_ParseSublist *scope = pod->nest_scopes;
				scope != NULL; scope = scope->next) {
			SAU_RefList *next_mod_list = NULL;
			if (!ParseConv_link_ops(o, &next_mod_list,
						&scope->range,
						scope->use_type)) goto ERROR;
			if (!od->mod_lists)
				od->mod_lists = next_mod_list;
			else
				last_mod_list->next = next_mod_list;
			last_mod_list = next_mod_list;
		}
	}
	return true;
ERROR:
	return false;
}

/*
 * Convert the given event data node and all associated operator data nodes.
 */
static bool ParseConv_add_event(ParseConv *restrict o,
		SAU_ParseEvData *restrict pe) {
	SAU_ScriptEvData *e = SAU_MemPool_alloc(o->mem,
			sizeof(SAU_ScriptEvData));
	if (!e) goto ERROR;
	pe->ev_conv = e;
	if (!o->first_ev)
		o->first_ev = e;
	else
		o->ev->next = e;
	o->ev = e;
	e->wait_ms = pe->wait_ms;
	/* ev_flags */
	VoContext *vc;
	if (!pe->vo_prev) {
		vc = SAU_MemPool_alloc(o->tmp, sizeof(VoContext));
		if (!vc) goto ERROR;
		e->ev_flags |= SAU_SDEV_NEW_OPGRAPH;
	} else {
		vc = pe->vo_prev->vo_context;
		SAU_ScriptEvData *prev_vo_use = vc->last_vo_use->ev_conv;
		e->prev_vo_use = prev_vo_use;
		prev_vo_use->next_vo_use = e;
	}
	vc->last_vo_use = pe;
	pe->vo_context = vc;
	e->vo_params = pe->vo_params;
	e->pan = pe->pan;
	if (!ParseConv_add_ops(o, &pe->operators)) goto ERROR;
	if (!ParseConv_link_ops(o, &e->carriers,
				&pe->operators, SAU_POP_CARR)) goto ERROR;
	return true;
ERROR:
	return false;
}

/*
 * Convert parser output to script data, performing
 * post-parsing passes. Perform timing adjustments,
 * flatten event list.
 *
 * Ideally, adjustments of parse data would be
 * more cleanly separated into the later stages.
 */
static SAU_Script *ParseConv_convert(ParseConv *restrict o,
		SAU_Parse *restrict p) {
	SAU_ParseEvData *pe;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		time_event(pe);
		if (pe == pe->dur->range.last) time_durgroup(pe);
	}
	o->mem = SAU_create_MemPool(0);
	o->tmp = p->mem;
	if (!o->mem || !o->tmp) goto ERROR;
	SAU_Script *s = SAU_MemPool_alloc(o->mem, sizeof(SAU_Script));
	if (!s) goto ERROR;
	s->name = p->name;
	s->sopt = p->sopt;
	s->mem = o->mem;
	/*
	 * Convert events, flattening the remaining list while proceeding.
	 * Flattening must be done following the timing adjustment pass;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (!ParseConv_add_event(o, pe)) goto ERROR;
		if (pe->composite != NULL) flatten_events(pe);
	}
	s->events = o->first_ev;
	if (false)
	ERROR: {
		SAU_destroy_MemPool(o->mem);
		SAU_error("parseconv", "memory allocation failure");
		s = NULL;
	}
	return s;
}

/**
 * Create script data for the given script. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SAU_Script *SAU_load_Script(const char *restrict script_arg, bool is_path) {
	ParseConv pc = (ParseConv){0};
	SAU_Parse *p = SAU_create_Parse(script_arg, is_path);
	if (!p)
		return NULL;
	SAU_Script *o = ParseConv_convert(&pc, p);
	SAU_destroy_Parse(p);
	return o;
}

/**
 * Destroy script data.
 */
void SAU_discard_Script(SAU_Script *restrict o) {
	if (!o)
		return;
	SAU_destroy_MemPool(o->mem);
}
