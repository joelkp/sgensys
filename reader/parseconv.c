/* ssndgen: Parser output to script data converter.
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
#include <stdlib.h>

/*
 * Script data construction from parse data.
 *
 * Adjust and replace data structures. The per-event
 * operator list becomes flat, with separate lists kept for
 * recursive traversal in scriptconv.
 */

/*
 * Adjust timing for event groupings; the script syntax for time grouping is
 * only allowed on the "top" operator level, so the algorithm only deals with
 * this for the events involved.
 */
static void group_events(SSG_ParseEvData *restrict to) {
	SSG_ParseEvData *e, *e_after = to->next;
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ParseOpData *op;
		op = e->operators.first;
		for (; op != NULL; op = op->range_next) {
			if (wait < op->time.v_ms)
				wait = op->time.v_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ParseOpData *op;
		op = e->operators.first;
		for (; op != NULL; op = op->range_next) {
			if (!(op->time.flags & SSG_TIMEP_SET)) {
				/* fill in sensible default time */
				op->time.v_ms = wait + waitcount;
				op->time.flags |= SSG_TIMEP_SET;
			}
		}
		e = e->next;
		if (e != NULL) {
			waitcount -= e->wait_ms;
		}
	}
	to->groupfrom = NULL;
	if (e_after != NULL)
		e_after->wait_ms += wait;
}

static inline void time_ramp(SSG_Ramp *restrict ramp,
		uint32_t default_time_ms) {
	if (!(ramp->flags & SSG_RAMPP_TIME))
		ramp->time_ms = default_time_ms;
}

static void time_operator(SSG_ParseOpData *restrict op) {
	SSG_ParseEvData *e = op->event;
	if ((op->op_flags & SSG_SDOP_NESTED) != 0 &&
			!(op->time.flags & SSG_TIMEP_SET)) {
		if (!(op->op_flags & SSG_SDOP_HAS_COMPOSITE))
			op->time.flags |= SSG_TIMEP_LINKED;
		op->time.flags |= SSG_TIMEP_SET;
	}
	if (!(op->time.flags & SSG_TIMEP_LINKED)) {
		time_ramp(&op->freq, op->time.v_ms);
		time_ramp(&op->freq2, op->time.v_ms);
		time_ramp(&op->amp, op->time.v_ms);
		time_ramp(&op->amp2, op->time.v_ms);
		if (!(op->op_flags & SSG_SDOP_SILENCE_ADDED)) {
			op->time.v_ms += op->silence_ms;
			op->op_flags |= SSG_SDOP_SILENCE_ADDED;
		}
	}
	if ((e->ev_flags & SSG_SDEV_ADD_WAIT_DURATION) != 0) {
		if (e->next != NULL)
			e->next->wait_ms += op->time.v_ms;
		e->ev_flags &= ~SSG_SDEV_ADD_WAIT_DURATION;
	}
	for (SSG_ParseSublist *scope = op->nest_scopes;
			scope != NULL; scope = scope->next) {
		SSG_ParseOpData *sub_op = scope->range.first;
		for (; sub_op != NULL; sub_op = sub_op->range_next) {
			time_operator(sub_op);
		}
	}
}

static void time_event(SSG_ParseEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	// e->pan.flags |= SSG_RAMPP_TIME; // TODO: revisit semantics
	SSG_ParseOpData *op;
	op = e->operators.first;
	for (; op != NULL; op = op->range_next) {
		time_operator(op);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SSG_ParseEvData *ce = e->composite;
		SSG_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = ce->operators.first;
		ce_op_prev = ce_op->op_prev;
		e_op = ce_op_prev;
		e_op->time.flags |= SSG_TIMEP_SET; /* always used from now on */
		for (;;) {
			ce->wait_ms += ce_op_prev->time.v_ms;
			if (!(ce_op->time.flags & SSG_TIMEP_SET)) {
				ce_op->time.flags |= SSG_TIMEP_SET;
				if ((ce_op->op_flags &
(SSG_SDOP_NESTED | SSG_SDOP_HAS_COMPOSITE)) == SSG_SDOP_NESTED)
					ce_op->time.flags |= SSG_TIMEP_LINKED;
				else
					ce_op->time.v_ms = ce_op_prev->time.v_ms
						- ce_op_prev->silence_ms;
			}
			time_event(ce);
			if (ce_op->time.flags & SSG_TIMEP_LINKED)
				e_op->time.flags |= SSG_TIMEP_LINKED;
			else if (!(e_op->time.flags & SSG_TIMEP_LINKED))
				e_op->time.v_ms += ce_op->time.v_ms +
					(ce->wait_ms - ce_op_prev->time.v_ms);
			ce_op->op_params &= ~SSG_POPP_TIME;
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
static void flatten_events(SSG_ParseEvData *restrict e) {
	SSG_ParseEvData *ce = e->composite;
	SSG_ParseEvData *se = e->next, *se_prev = e;
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
		SSG_ParseEvData *ce_next = ce->next;
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
		} else {
			SSG_ParseEvData *se_next = se->next;
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
	SSG_ScriptEvData *ev, *first_ev;
} ParseConv;

/*
 * Convert data for an operator node to script operator data,
 * adding it to the list to be used for the current script event.
 */
static bool ParseConv_add_opdata(ParseConv *restrict o,
		SSG_ParseOpData *restrict pod) {
	SSG_ScriptOpData *od = calloc(1, sizeof(SSG_ScriptOpData));
	if (!od)
		return false;
	SSG_ScriptEvData *e = o->ev;
	pod->op_conv = od;
	od->event = e;
	/* next_bound */
	/* label */
	od->op_flags = pod->op_flags;
	od->op_params = pod->op_params;
	od->time = pod->time;
	od->silence_ms = pod->silence_ms;
	od->wave = pod->wave;
	od->freq = pod->freq;
	od->freq2 = pod->freq2;
	od->amp = pod->amp;
	od->amp2 = pod->amp2;
	od->phase = pod->phase;
	if (pod->op_prev != NULL) {
		od->op_prev = pod->op_prev->op_conv;
		SSG_PtrArr_soft_copy(&od->fmods, &od->op_prev->fmods);
		SSG_PtrArr_soft_copy(&od->pmods, &od->op_prev->pmods);
		SSG_PtrArr_soft_copy(&od->amods, &od->op_prev->amods);
	} else {
		if (pod->use_type == SSG_POP_CARR) {
			e->ev_flags |= SSG_SDEV_NEW_OPGRAPH;
			od->op_flags |= SSG_SDOP_NEW_CARRIER;
		}
	}
	/* op_next */
	if (!e->op_all.first)
		e->op_all.first = od;
	else
		((SSG_ScriptOpData*) e->op_all.last)->range_next = od;
	e->op_all.last = od;
	return true;
//ERROR:
//	free(od);
//	return false;
}

/*
 * Recursively create needed operator data nodes,
 * visiting new operator nodes as they branch out.
 */
static bool ParseConv_add_ops(ParseConv *restrict o,
		const SSG_NodeRange *restrict pod_list) {
	if (!pod_list)
		return true;
	SSG_ParseOpData *pod = pod_list->first;
	for (; pod != NULL; pod = pod->range_next) {
		// TODO: handle multiple operator nodes
		if (pod->op_flags & SSG_SDOP_MULTIPLE) continue;
		if (!ParseConv_add_opdata(o, pod)) goto ERROR;
		for (SSG_ParseSublist *scope = pod->nest_scopes;
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
		SSG_PtrArr *restrict od_list,
		const SSG_NodeRange *restrict pod_list) {
	if (!pod_list)
		return true;
	SSG_ParseOpData *pod = pod_list->first;
	if (od_list != NULL)
		SSG_PtrArr_clear(od_list); // rebuild, replace any soft copy
	for (; pod != NULL; pod = pod->range_next) {
		// TODO: handle multiple operator nodes
		if (pod->op_flags & SSG_SDOP_MULTIPLE) continue;
		SSG_ScriptOpData *od = pod->op_conv;
		if (!od) goto ERROR;
		SSG_ScriptEvData *e = od->event;
		if (e->ev_flags & SSG_SDEV_NEW_OPGRAPH) {
			// handle linking for carriers separately
			if ((od->op_flags & SSG_SDOP_NEW_CARRIER) != 0
				&& !SSG_PtrArr_add(&e->op_carriers, od))
				goto ERROR;
		}
		if (od_list != NULL && !SSG_PtrArr_add(od_list, od))
			goto ERROR;
		SSG_NodeRange *sub_lists[SSG_POP_USES] = {0};
		for (SSG_ParseSublist *scope = pod->nest_scopes;
				scope != NULL; scope = scope->next) {
			sub_lists[scope->use_type] = &scope->range;
		}
		if (!ParseConv_link_ops(o, &od->fmods,
					sub_lists[SSG_POP_FMOD]))
			goto ERROR;
		if (!ParseConv_link_ops(o, &od->pmods,
					sub_lists[SSG_POP_PMOD]))
			goto ERROR;
		if (!ParseConv_link_ops(o, &od->amods,
					sub_lists[SSG_POP_AMOD]))
			goto ERROR;
	}
	return true;
ERROR:
	return false;
}

/*
 * Convert the given event data node and all associated operator data nodes.
 */
static bool ParseConv_add_event(ParseConv *restrict o,
		SSG_ParseEvData *restrict pe) {
	SSG_ScriptEvData *e = calloc(1, sizeof(SSG_ScriptEvData));
	if (!e)
		return false;
	pe->ev_conv = e;
	if (!o->first_ev)
		o->first_ev = e;
	else
		o->ev->next = e;
	o->ev = e;
	/* groupfrom */
	/* composite */
	e->wait_ms = pe->wait_ms;
	e->ev_flags = pe->ev_flags;
	e->vo_params = pe->vo_params;
	if (pe->vo_prev != NULL) {
		e->vo_prev = pe->vo_prev->ev_conv;
	} else {
		e->ev_flags |= SSG_SDEV_NEW_OPGRAPH;
	}
	e->pan = pe->pan;
	if (!ParseConv_add_ops(o, &pe->operators)) goto ERROR;
	if (!ParseConv_link_ops(o, NULL, &pe->operators)) goto ERROR;
	return true;
ERROR:
	free(e);
	return false;
}

/*
 * Convert parser output to script data, performing
 * post-parsing passes - perform timing adjustments,
 * flatten event list.
 *
 * Ideally, adjustments of parse data would be
 * more cleanly separated into the later stages.
 */
static SSG_Script *ParseConv_convert(ParseConv *restrict o,
		SSG_Parse *restrict p) {
	SSG_ParseEvData *pe;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		time_event(pe);
		if (pe->groupfrom != NULL) group_events(pe);
	}
	SSG_Script *s = calloc(1, sizeof(SSG_Script));
	if (!s)
		return NULL;
	s->name = p->name;
	s->sopt = p->sopt;
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
	return s;
ERROR:
	SSG_discard_Script(s);
	SSG_error("parseconv", "memory allocation failure");
	return NULL;
}

/**
 * Create script data for the given script. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SSG_Script *SSG_load_Script(const char *restrict script_arg, bool is_path) {
	ParseConv pc = (ParseConv){0};
	SSG_Parse *p = SSG_create_Parse(script_arg, is_path);
	if (!p)
		return NULL;
	SSG_Script *o = ParseConv_convert(&pc, p);
	SSG_destroy_Parse(p);
	return o;
}

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SSG_ScriptOpData *restrict op) {
	SSG_PtrArr_clear(&op->op_next);
	SSG_PtrArr_clear(&op->fmods);
	SSG_PtrArr_clear(&op->pmods);
	SSG_PtrArr_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SSG_ScriptEvData *restrict e) {
	SSG_ScriptOpData *op, *op_next;
	for (op = e->op_all.first; op != NULL; op = op_next) {
		op_next = op->range_next;
		destroy_operator(op);
	}
	SSG_PtrArr_clear(&e->op_carriers);
	free(e);
}

/**
 * Destroy script data.
 */
void SSG_discard_Script(SSG_Script *restrict o) {
	if (!o)
		return;
	SSG_ScriptEvData *e, *e_next;
	for (e = o->events; e != NULL; e = e_next) {
		e_next = e->next;
		destroy_event_node(e);
	}
	free(o);
}
