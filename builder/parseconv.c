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
	size_t i;
	uint32_t wait = 0, waitcount = 0;
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ParseOpData **ops;
		ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SSG_ParseOpData *op = ops[i];
			if (wait < op->time.v_ms)
				wait = op->time.v_ms;
		}
		e = e->next;
		if (e != NULL) {
			waitcount += e->wait_ms;
		}
	}
	for (e = to->groupfrom; e != e_after; ) {
		SSG_ParseOpData **ops;
		ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&e->operators);
		for (i = 0; i < e->operators.count; ++i) {
			SSG_ParseOpData *op = ops[i];
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
	size_t i;
	SSG_ParseOpData **ops;
	ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&op->fmods);
	for (i = op->fmods.old_count; i < op->fmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&op->pmods);
	for (i = op->pmods.old_count; i < op->pmods.count; ++i) {
		time_operator(ops[i]);
	}
	ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&op->amods);
	for (i = op->amods.old_count; i < op->amods.count; ++i) {
		time_operator(ops[i]);
	}
}

static void time_event(SSG_ParseEvData *restrict e) {
	/*
	 * Adjust default ramp durations, handle silence as well as the case of
	 * adding present event duration to wait time of next event.
	 */
	// e->pan.flags |= SSG_RAMPP_TIME; // TODO: revisit semantics
	size_t i;
	SSG_ParseOpData **ops;
	ops = (SSG_ParseOpData**) SSG_PtrList_ITEMS(&e->operators);
	for (i = e->operators.old_count; i < e->operators.count; ++i) {
		time_operator(ops[i]);
	}
	/*
	 * Timing for composites - done before event list flattened.
	 */
	if (e->composite != NULL) {
		SSG_ParseEvData *ce = e->composite;
		SSG_ParseOpData *ce_op, *ce_op_prev, *e_op;
		ce_op = (SSG_ParseOpData*) SSG_PtrList_GET(&ce->operators, 0),
		ce_op_prev = ce_op->op_prev,
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
			ce_op = (SSG_ParseOpData*)
				SSG_PtrList_GET(&ce->operators, 0);
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
		if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
			SSG_ParseEvData *ce_next = ce->next;
			se->wait_ms -= ce->wait_ms + added_wait_ms;
			added_wait_ms = 0;
			wait_ms = 0;
			se_prev->next = ce;
			se_prev = ce;
			se_prev->next = se;
			ce = ce_next;
		} else {
			SSG_ParseEvData *se_next, *ce_next;
			se_next = se->next;
			ce_next = ce->next;
			ce->wait_ms -= wait_ms;
			added_wait_ms += ce->wait_ms;
			wait_ms = 0;
			se->next = ce;
			ce->next = se_next;
			se_prev = ce;
			se = se_next;
			ce = ce_next;
		}
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
	pod->op_conv = od;
	od->event = o->ev;
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
	if (pod->op_prev != NULL) od->op_prev = pod->op_prev->op_conv;
	/* op_next */
	/* fmods */
	/* pmods */
	/* amods */
	if (!SSG_PtrList_add(&o->ev->op_all, od)) goto ERROR;
	return true;

ERROR:
	free(od);
	return false;
}

/*
 * Recursively create needed operator data nodes,
 * visiting new operator nodes as they branch out.
 */
static bool ParseConv_add_ops(ParseConv *restrict o,
		const SSG_PtrList *restrict pod_list) {
	SSG_ParseOpData **pods;
	pods = (SSG_ParseOpData**) SSG_PtrList_ITEMS(pod_list);
	for (size_t i = pod_list->old_count; i < pod_list->count; ++i) {
		SSG_ParseOpData *pod = pods[i];
		// TODO: handle multiple operator nodes
		//if (pod->op_flags & SSG_SDOP_MULTIPLE) continue;
		if (!ParseConv_add_opdata(o, pod)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->fmods)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->pmods)) goto ERROR;
		if (!ParseConv_add_ops(o, &pod->amods)) goto ERROR;
	}
	return true;

ERROR:
	return false;
}

/*
 * Recursively fill in lists for operator node graph,
 * visiting all operator nodes as they branch out.
 */
static bool ParseConv_link_ops(ParseConv *restrict o,
		SSG_PtrList *restrict od_list,
		const SSG_PtrList *restrict pod_list) {
	SSG_ParseOpData **pods;
	pods = (SSG_ParseOpData**) SSG_PtrList_ITEMS(pod_list);
	for (size_t i = 0; i < pod_list->count; ++i) {
		SSG_ParseOpData *pod = pods[i];
		// TODO: handle multiple operator nodes
		//if (pod->op_flags & SSG_SDOP_MULTIPLE) continue;
		SSG_ScriptOpData *od = pod->op_conv;
		if (!od) goto ERROR;
		SSG_ScriptEvData *e = od->event;
		if (e->ev_flags & SSG_SDEV_NEW_OPGRAPH) {
			// Handle linking for carriers separately
			if (od->op_flags & SSG_SDOP_NEW_CARRIER)
				SSG_PtrList_add(&e->op_graph, od);
		}
		if (od_list != NULL) SSG_PtrList_add(od_list, od);
		if (od->op_params & SSG_POPP_ADJCS) {
			if (!ParseConv_link_ops(o,
					&od->fmods, &pod->fmods)) goto ERROR;
			if (!ParseConv_link_ops(o,
					&od->pmods, &pod->pmods)) goto ERROR;
			if (!ParseConv_link_ops(o,
					&od->amods, &pod->amods)) goto ERROR;
		}
	}
	return true;

ERROR:
	SSG_error("parseconv", "converted node missing at some level");
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
	if (o->ev != NULL) o->ev->next = e;
	o->ev = e;
	if (!o->first_ev) o->first_ev = e;
	/* groupfrom */
	/* composite */
	e->wait_ms = pe->wait_ms;
	e->ev_flags = pe->ev_flags;
	e->vo_params = pe->vo_params;
	if (pe->vo_prev != NULL) e->vo_prev = pe->vo_prev->ev_conv;
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
 * post-parsing passes - perform timing adjustments, flatten event list.
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
	/*
	 * Flatten in separate pass following timing adjustments for events;
	 * otherwise, cannot always arrange events in the correct order.
	 */
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (pe->composite != NULL) flatten_events(pe);
	}
	/*
	 * Convert adjusted parser output to script data.
	 */
	SSG_Script *s = calloc(1, sizeof(SSG_Script));
	if (!s)
		return NULL;
	s->name = p->name;
	s->sopt = p->sopt;
	for (pe = p->events; pe != NULL; pe = pe->next) {
		if (!ParseConv_add_event(o, pe)) goto ERROR;
	}
	s->events = o->first_ev;
	return s;

ERROR:
	SSG_discard_Script(s);
	return NULL;
}

/**
 * Create script data for the given script. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SSG_Script *SSG_load_Script(struct SSG_File *restrict f) {
	ParseConv pc = (ParseConv){0};
	SSG_Parse *p = SSG_create_Parse(f);
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
	SSG_PtrList_clear(&op->op_next);
	SSG_PtrList_clear(&op->fmods);
	SSG_PtrList_clear(&op->pmods);
	SSG_PtrList_clear(&op->amods);
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SSG_ScriptEvData *restrict e) {
	size_t i;
	SSG_ScriptOpData **ops;
	ops = (SSG_ScriptOpData**) SSG_PtrList_ITEMS(&e->op_all);
	for (i = e->op_all.old_count; i < e->op_all.count; ++i) {
		destroy_operator(ops[i]);
	}
	SSG_PtrList_clear(&e->op_all);
	SSG_PtrList_clear(&e->op_graph);
	free(e);
}

/**
 * Destroy script data.
 */
void SSG_discard_Script(SSG_Script *restrict o) {
	if (!o)
		return;
	SSG_ScriptEvData *e;
	for (e = o->events; e != NULL; ) {
		SSG_ScriptEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
