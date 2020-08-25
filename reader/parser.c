/* ssndgen: Script file parser.
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

#include "scanner.h"
#include "parser.h"
#include "../math.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * File-reading code
 */

/* Basic character types. */
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_ALPHA(c) (IS_LOWER(c) || IS_UPPER(c))

/* Sensible to print, for ASCII only. */
#define IS_VISIBLE(c) ((c) >= '!' && (c) <= '~')

typedef struct ScanLookup {
	SSG_ScriptOptions sopt;
	const char *const*wave_names;
	const char *const*ramp_names;
} ScanLookup;

/*
 * Default script options, used until changed in a script.
 */
static const SSG_ScriptOptions def_sopt = {
	.changed = 0,
	.ampmult = 1.f,
	.A4_freq = 444.f,
	.def_time_ms = 1000,
	.def_freq = 444.f,
	.def_relfreq = 1.f,
	.def_chanmix = 0.f,
};

static bool init_ScanLookup(ScanLookup *restrict o, SSG_SymTab *restrict st) {
	o->sopt = def_sopt;
	o->wave_names = SSG_SymTab_pool_stra(st,
			SSG_Wave_names, SSG_WAVE_TYPES);
	if (!o->wave_names)
		return false;
	o->ramp_names = SSG_SymTab_pool_stra(st,
			SSG_Ramp_names, SSG_RAMP_TYPES);
	if (!o->ramp_names)
		return false;
	return true;
}

/*
 * Handle unknown character, checking for EOF and treating
 * the character as invalid if not an end marker.
 *
 * \return false if EOF reached
 */
static bool handle_unknown_or_eof(SSG_Scanner *restrict o, uint8_t c) {
	if (c == 0)
		return false;
	if (IS_VISIBLE(c)) {
		SSG_Scanner_warning(o, NULL,
				"invalid character '%c'", c);
	} else {
		SSG_Scanner_warning(o, NULL,
				"invalid character (value 0x%02hhX)", c);
	}
	return true;
}

/*
 * Print warning for EOF without closing \p c scope-closing character.
 */
static void warn_eof_without_closing(SSG_Scanner *restrict o, uint8_t c) {
	SSG_Scanner_warning(o, NULL, "end of file without closing '%c'", c);
}

/*
 * Print warning for scope-opening character in disallowed place.
 */
static void warn_opening_disallowed(SSG_Scanner *restrict o,
		uint8_t open_c) {
	SSG_Scanner_warning(o, NULL, "opening '%c' out of place",
			open_c);
}

/*
 * Print warning for scope-closing character without scope-opening character.
 */
static void warn_closing_without_opening(SSG_Scanner *restrict o,
		uint8_t close_c, uint8_t open_c) {
	SSG_Scanner_warning(o, NULL, "closing '%c' without opening '%c'",
			close_c, open_c);
}

typedef struct NumParser {
	SSG_Scanner *sc;
	SSG_ScanNumConst_f numconst_f;
	SSG_ScanFrame sf_start;
	bool has_infnum;
} NumParser;
enum {
	NUMEXP_SUB = 0,
	NUMEXP_ADT,
	NUMEXP_MLT,
	NUMEXP_POW,
	NUMEXP_NUM,
};
static double scan_num_r(NumParser *restrict o, uint8_t pri, uint32_t level) {
	SSG_Scanner *sc = o->sc;
	double num;
	bool minus = false;
	uint8_t c;
	if (level > 0) SSG_Scanner_skipws(sc);
	c = SSG_Scanner_getc(sc);
	if ((level > 0) && (c == '+' || c == '-')) {
		if (c == '-') minus = true;
		SSG_Scanner_skipws(sc);
		c = SSG_Scanner_getc(sc);
	}
	if (c == '(') {
		num = scan_num_r(o, NUMEXP_SUB, level+1);
	} else {
		size_t read_len;
		SSG_Scanner_ungetc(sc);
		SSG_Scanner_getd(sc, &num, false, &read_len, o->numconst_f);
		if (read_len == 0)
			return NAN;
	}
	if (isnan(num))
		return NAN;
	if (minus) num = -num;
	if (level == 0 || pri == NUMEXP_NUM)
		return num; /* defer all */
	for (;;) {
		if (isinf(num)) o->has_infnum = true;
		if (level > 0) SSG_Scanner_skipws(sc);
		c = SSG_Scanner_getc(sc);
		switch (c) {
		case SSG_SCAN_SPACE:
		case SSG_SCAN_LNBRK:
			break;
		case '(':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_SUB, level+1);
			break;
		case ')':
			if (pri != NUMEXP_SUB) goto DEFER;
			return num;
		case '^':
			if (pri >= NUMEXP_POW) goto DEFER;
			num = exp(log(num) * scan_num_r(o, NUMEXP_POW, level));
			break;
		case '*':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num *= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '/':
			if (pri >= NUMEXP_MLT) goto DEFER;
			num /= scan_num_r(o, NUMEXP_MLT, level);
			break;
		case '+':
			if (pri >= NUMEXP_ADT) goto DEFER;
			num += scan_num_r(o, NUMEXP_ADT, level);
			break;
		case '-':
			if (pri >= NUMEXP_ADT) goto DEFER;
			num -= scan_num_r(o, NUMEXP_ADT, level);
			break;
		default:
			if (pri == NUMEXP_SUB) {
				SSG_Scanner_warning(sc, &o->sf_start,
"numerical expression has '(' without closing ')'");
			}
			goto DEFER;
		}
		if (isnan(num)) goto DEFER;
	}
DEFER:
	SSG_Scanner_ungetc(sc);
	return num;
}
static SSG__noinline bool scan_num(SSG_Scanner *restrict o,
		SSG_ScanNumConst_f scan_numconst, float *restrict var) {
	NumParser np = {o, scan_numconst, o->sf, false};
	float num = scan_num_r(&np, NUMEXP_NUM, 0);
	if (isnan(num))
		return false;
	if (isinf(num)) np.has_infnum = true;
	if (np.has_infnum) {
		SSG_Scanner_warning(o, &np.sf_start,
				"discarding expression with infinite number");
		return false;
	}
	*var = num;
	return true;
}

static SSG__noinline bool scan_time_val(SSG_Scanner *restrict o,
		uint32_t *restrict val) {
	SSG_ScanFrame sf = o->sf;
	float val_s;
	if (!scan_num(o, NULL, &val_s))
		return false;
	if (val_s < 0.f) {
		SSG_Scanner_warning(o, &sf, "discarding negative time value");
		return false;
	}
	*val = lrint(val_s * 1000.f);
	return true;
}

static size_t scan_chanmix_const(SSG_Scanner *restrict o,
		double *restrict val) {
	char c = SSG_File_GETC(o->f);
	switch (c) {
	case 'C':
		*val = 0.f;
		return 1;
	case 'L':
		*val = -1.f;
		return 1;
	case 'R':
		*val = 1.f;
		return 1;
	default:
		SSG_File_DECP(o->f);
		return 0;
	}
}

#define OCTAVES 11
static size_t scan_note_const(SSG_Scanner *restrict o,
		double *restrict val) {
	static const float octaves[OCTAVES] = {
		(1.f/16.f),
		(1.f/8.f),
		(1.f/4.f),
		(1.f/2.f),
		1.f, /* no. 4 - standard tuning here */
		2.f,
		4.f,
		8.f,
		16.f,
		32.f,
		64.f
	};
	static const float notes[3][8] = {
		{ /* flat */
			48.f/25.f,
			16.f/15.f,
			6.f/5.f,
			32.f/25.f,
			36.f/25.f,
			8.f/5.f,
			9.f/5.f,
			96.f/25.f
		},
		{ /* normal (9/8 replaced with 10/9 for symmetry) */
			1.f,
			10.f/9.f,
			5.f/4.f,
			4.f/3.f,
			3.f/2.f,
			5.f/3.f,
			15.f/8.f,
			2.f
		},
		{ /* sharp */
			25.f/24.f,
			75.f/64.f,
			125.f/96.f,
			25.f/18.f,
			25.f/16.f,
			225.f/128.f,
			125.f/64.f,
			25.f/12.f
		}
	};
	SSG_File *f = o->f;
	ScanLookup *sl = o->data;
	size_t len = 0, num_len;
	uint8_t c;
	float freq;
	int32_t octave;
	int32_t semitone = 1, note;
	int32_t subnote = -1;
	++len;
	c = SSG_File_GETC(f);
	if (c >= 'a' && c <= 'g') {
		subnote = c - 'c';
		if (subnote < 0) /* a, b */
			subnote += 7;
		++len;
		c = SSG_File_GETC(f);
	}
	if (c < 'A' || c > 'G') {
		SSG_File_UNGETN(f, len);
		return 0;
	}
	note = c - 'C';
	if (note < 0) /* A, B */
		note += 7;
	++len;
	c = SSG_File_GETC(f);
	if (c == 's')
		semitone = 2;
	else if (c == 'f')
		semitone = 0;
	else {
		SSG_File_DECP(f);
		--len;
	}
	SSG_Scanner_geti(o, &octave, false, &num_len);
	len += num_len;
	if (num_len == 0)
		octave = 4;
	else if (octave >= OCTAVES) {
		SSG_Scanner_warning(o, NULL,
"invalid octave specified for note, using 4 (valid range 0-10)");
		octave = 4;
	}
	freq = sl->sopt.A4_freq * (3.f/5.f); /* get C4 */
	freq *= octaves[octave] * notes[semitone][note];
	if (subnote >= 0)
		freq *= 1.f + (notes[semitone][note+1] /
				notes[semitone][note] - 1.f) *
			(notes[1][subnote] - 1.f);
	*val = (double) freq;
	return len;
}

static SSG_SymStr *scan_label(SSG_Scanner *restrict o,
		char op) {
	SSG_SymStr *s = NULL;
	SSG_Scanner_get_symstr(o, &s);
	if (!s) {
		SSG_Scanner_warning(o, NULL,
				"ignoring %c without label name", op);
	}
	return s;
}

static bool scan_symafind(SSG_Scanner *restrict o,
		const char *const*restrict stra,
		size_t *restrict found_i, const char *restrict print_type) {
	SSG_ScanFrame sf_begin = o->sf;
	SSG_SymStr *s = NULL;
	SSG_Scanner_get_symstr(o, &s);
	if (!s) {
		SSG_Scanner_warning(o, NULL,
				"%s type value missing", print_type);
		return false;
	}
	for (size_t i = 0; stra[i] != NULL; ++i) {
		if (stra[i] == s->key) {
			*found_i = i;
			return true;
		}
	}
	SSG_Scanner_warning(o, &sf_begin,
			"invalid %s type value; available are:", print_type);
	fprintf(stderr, "\t%s", stra[0]);
	for (size_t i = 1; stra[i] != NULL; ++i) {
		fprintf(stderr, ", %s", stra[i]);
	}
	putc('\n', stderr);
	return false;
}

static bool scan_wavetype(SSG_Scanner *restrict o, size_t *restrict found_id) {
	ScanLookup *sl = o->data;
	return scan_symafind(o, sl->wave_names,
			found_id, "wave type");
}

static bool scan_ramp_state(SSG_Scanner *restrict o,
		SSG_ScanNumConst_f scan_numconst,
		SSG_Ramp *restrict ramp, bool mult) {
	if (!scan_num(o, scan_numconst, &ramp->v0))
		return false;
	if (mult) {
		ramp->flags |= SSG_RAMPP_STATE_RATIO;
	} else {
		ramp->flags &= ~SSG_RAMPP_STATE_RATIO;
	}
	ramp->flags |= SSG_RAMPP_STATE;
	return true;
}

static bool scan_ramp(SSG_Scanner *restrict o,
		SSG_ScanNumConst_f scan_numconst,
		SSG_Ramp *restrict ramp, bool mult) {
	if (!SSG_Scanner_tryc(o, '{')) {
		return scan_ramp_state(o, scan_numconst, ramp, mult);
	}
	ScanLookup *sl = o->data;
	bool goal = false;
	bool time_set = (ramp->flags & SSG_RAMPP_TIME) != 0;
	float vt;
	uint32_t time_ms = sl->sopt.def_time_ms;
	uint8_t type = ramp->type; // has default
	if ((ramp->flags & SSG_RAMPP_GOAL) != 0) {
		// allow partial change
		if (((ramp->flags & SSG_RAMPP_GOAL_RATIO) != 0) == mult) {
			goal = true;
			vt = ramp->vt;
		}
		time_ms = ramp->time_ms;
	}
	for (;;) {
		uint8_t c = SSG_Scanner_getc_nospace(o);
		switch (c) {
		case SSG_SCAN_LNBRK:
			break;
		case 'c': {
			size_t id;
			if (scan_symafind(o, sl->ramp_names,
					&id, "ramp curve")) {
				type = id;
			}
			break; }
		case 't':
			if (scan_time_val(o, &time_ms))
				time_set = true;
			break;
		case 'v':
			if (scan_num(o, scan_numconst, &vt))
				goal = true;
			break;
		case '}':
			goto RETURN;
		default:
			if (!handle_unknown_or_eof(o, c)) {
				warn_eof_without_closing(o, '}');
				goto RETURN;
			}
			break;
		}
	}
RETURN:
	if (!goal) {
		SSG_Scanner_warning(o, NULL,
				"ignoring value ramp with no target value");
		return false;
	}
	ramp->vt = vt;
	ramp->time_ms = time_ms;
	ramp->type = type;
	ramp->flags |= SSG_RAMPP_GOAL;
	if (mult)
		ramp->flags |= SSG_RAMPP_GOAL_RATIO;
	else
		ramp->flags &= ~SSG_RAMPP_GOAL_RATIO;
	if (time_set)
		ramp->flags |= SSG_RAMPP_TIME;
	else
		ramp->flags &= ~SSG_RAMPP_TIME;
	return true;
}

/*
 * Parser
 */

typedef struct SSG_Parser {
	ScanLookup sl;
	SSG_Scanner *sc;
	SSG_SymTab *st;
	SSG_MemPool *mp;
	uint32_t call_level;
	/* node state */
	SSG_ParseEvData *ev, *first_ev;
} SSG_Parser;

/*
 * Finalize parser instance.
 */
static void fini_Parser(SSG_Parser *restrict o) {
	SSG_destroy_Scanner(o->sc);
	SSG_destroy_SymTab(o->st);
	SSG_destroy_MemPool(o->mp);
}

/*
 * Initialize parser instance.
 *
 * The same symbol table and script-set data will be used
 * until the instance is finalized.
 *
 * \return true, or false on allocation failure
 */
static bool init_Parser(SSG_Parser *restrict o) {
	SSG_MemPool *mp = SSG_create_MemPool(0);
	SSG_SymTab *st = SSG_create_SymTab(mp);
	SSG_Scanner *sc = SSG_create_Scanner(st);
	*o = (SSG_Parser){0};
	o->sc = sc;
	o->st = st;
	o->mp = mp;
	if (!sc || !st || !mp) goto ERROR;
	if (!init_ScanLookup(&o->sl, st)) goto ERROR;
	sc->data = &o->sl;
	return true;

ERROR:
	fini_Parser(o);
	return false;
}

/*
 * Scope values.
 */
enum {
	SCOPE_TOP = 0,
	SCOPE_BLOCK,
	SCOPE_BIND,
	SCOPE_NEST,
};

typedef struct ParseLevel ParseLevel;
typedef void (*ParseLevelSub_f)(ParseLevel *restrict pl);

static void parse_in_event(ParseLevel *restrict pl);
static void parse_in_settings(ParseLevel *restrict pl);

/*
 * Parse level flags.
 */
enum {
	PL_DEFERRED_SUB  = 1<<0, // \a sub_f exited to attempt handling above
	PL_BIND_MULTIPLE = 1<<1, // previous node interpreted as set of nodes
	PL_NESTED_SCOPE  = 1<<2,
	PL_ACTIVE_EV     = 1<<3,
	PL_ACTIVE_OP     = 1<<4,
};

/*
 * Things that need to be separate for each nested parse_level() go here.
 *
 *
 */
struct ParseLevel {
	SSG_Parser *o;
	struct ParseLevel *parent;
	ParseLevelSub_f sub_f; // identifies "location" and implicit context
	uint32_t pl_flags;
	uint8_t scope;
	SSG_ParseEvData *event, *last_event;
	SSG_ParseOpData *operator, *first_operator, *last_operator;
	SSG_ParseOpData *parent_op, *op_prev;
	SSG_ParseSublist *op_scope;
	SSG_SymStr *set_label; /* label assigned to next node */
	/* timing/delay */
	SSG_ParseEvData *group_from; /* where to begin for group_events() */
	SSG_ParseEvData *composite; /* grouping of events for a voice and/or operator */
	uint32_t next_wait_ms; /* added for next event */
};

static bool parse_waittime(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	/* FIXME: ADD_WAIT_DURATION */
	if (SSG_Scanner_tryc(sc, 't')) {
		if (!pl->last_operator) {
			SSG_Scanner_warning(sc, NULL,
"add wait for last duration before any parts given");
			return false;
		}
		pl->last_event->ev_flags |= SSG_SDEV_ADD_WAIT_DURATION;
	} else {
		uint32_t wait_ms;
		if (!scan_time_val(sc, &wait_ms))
			return false;
		pl->next_wait_ms += wait_ms;
	}
	return true;
}

/*
 * Node- and scope-handling functions
 */

/*
 * Destroy the given operator data node.
 */
static void destroy_operator(SSG_ParseOpData *restrict op) {
	SSG_ParseOpData *sub_op, *sub_op_next;
	SSG_ParseSublist *scope = op->nest_scopes, *scope_next;
	for (; scope != NULL; scope = scope_next) {
		scope_next = scope->next;
		sub_op = scope->range.first;
		for (; sub_op != NULL; sub_op = sub_op_next) {
			sub_op_next = sub_op->range_next;
			destroy_operator(sub_op);
		}
		free(scope);
	}
	free(op);
}

/*
 * Destroy the given event data node and all associated operator data nodes.
 */
static void destroy_event_node(SSG_ParseEvData *restrict e) {
	SSG_ParseOpData *op, *op_next;
	op = e->operators.first;
	for (; op != NULL; op = op_next) {
		op_next = op->range_next;
		destroy_operator(op);
	}
	free(e);
}

static SSG_ParseSublist *create_op_scope(uint8_t use_type) {
	SSG_ParseSublist *o = calloc(1, sizeof(SSG_ParseSublist));
	if (!o)
		return NULL;
	o->use_type = use_type;
	return o;
}

static SSG__noinline void end_operator(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & PL_ACTIVE_OP))
		return;
	pl->pl_flags &= ~PL_ACTIVE_OP;
	SSG_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SSG_ParseOpData *op = pl->operator;
	if (SSG_Ramp_ENABLED(&op->amp)) {
		if (!(op->op_flags & SSG_SDOP_NESTED)) {
			op->amp.v0 *= sl->sopt.ampmult;
			op->amp.vt *= sl->sopt.ampmult;
		}
	}
	if (SSG_Ramp_ENABLED(&op->amp2)) {
		if (!(op->op_flags & SSG_SDOP_NESTED)) {
			op->amp2.v0 *= sl->sopt.ampmult;
			op->amp2.vt *= sl->sopt.ampmult;
		}
	}
	SSG_ParseOpData *pop = op->op_prev;
	if (!pop) {
		/*
		 * Reset all operator state for initial event.
		 */
		op->op_params |= SSG_POP_PARAMS;
	}
	pl->operator = NULL;
	pl->last_operator = op;
}

static SSG__noinline void end_event(ParseLevel *restrict pl) {
	if (!(pl->pl_flags & PL_ACTIVE_EV))
		return;
	pl->pl_flags &= ~PL_ACTIVE_EV;
	SSG_ParseEvData *e = pl->event;
	end_operator(pl);
	SSG_ParseEvData *pve = e->vo_prev;
	if (!pve) {
		/*
		 * Reset all voice state for initial event.
		 */
		e->vo_params |= SSG_PVO_PARAMS & ~SSG_PVOP_GRAPH;
	}
	pl->last_event = e;
	pl->event = NULL;
}

static void begin_event(ParseLevel *restrict pl,
		bool is_composite) {
	SSG_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SSG_ParseEvData *e;
	end_event(pl);
	pl->event = calloc(1, sizeof(SSG_ParseEvData));
	e = pl->event;
	e->wait_ms = pl->next_wait_ms;
	pl->next_wait_ms = 0;
	SSG_Ramp_reset(&e->pan);
	if (pl->op_prev != NULL) {
		SSG_ParseEvData *pve = pl->op_prev->event;
		pve->ev_flags |= SSG_SDEV_VOICE_LATER_USED;
		if (is_composite) {
			if (!pl->composite) {
				pve->composite = e;
				pl->composite = pve;
			} else {
				pve->next = e;
			}
		} else if (pve->composite != NULL) {
			SSG_ParseEvData *last_ce;
			for (last_ce = pve->composite; last_ce->next;
					last_ce = last_ce->next) ;
			last_ce->ev_flags |= SSG_SDEV_VOICE_LATER_USED;
		}
		e->vo_prev = pve;
	} else {
		/*
		 * New voice with initial parameter values.
		 */
		e->pan.v0 = sl->sopt.def_chanmix;
		e->pan.flags |= SSG_RAMPP_STATE;
	}
	if (!pl->group_from)
		pl->group_from = e;
	if (!is_composite) {
		if (!o->first_ev)
			o->first_ev = e;
		else
			o->ev->next = e;
		o->ev = e;
		pl->composite = NULL;
	}
	pl->pl_flags |= PL_ACTIVE_EV;
}

static void begin_operator(ParseLevel *restrict pl,
		bool is_composite) {
	SSG_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SSG_ParseEvData *e = pl->event;
	SSG_ParseOpData *op, *pop = pl->op_prev;
	/*
	 * It is assumed that a valid voice event exists.
	 */
	end_operator(pl);
	pl->operator = calloc(1, sizeof(SSG_ParseOpData));
	op = pl->operator;
	if (!pl->first_operator)
		pl->first_operator = op;
	if (!is_composite && pl->last_operator != NULL)
		pl->last_operator->next_bound = op;
	/*
	 * Initialize node.
	 */
	op->time.v_ms = sl->sopt.def_time_ms; /* time is not copied */
	SSG_Ramp_reset(&op->freq);
	SSG_Ramp_reset(&op->freq2);
	SSG_Ramp_reset(&op->amp);
	SSG_Ramp_reset(&op->amp2);
	if (pop != NULL) {
		op->use_type = pop->use_type;
		pop->op_flags |= SSG_SDOP_LATER_USED;
		op->op_prev = pop;
		op->op_flags = pop->op_flags &
			(SSG_SDOP_NESTED | SSG_SDOP_MULTIPLE);
		if (is_composite) {
			pop->op_flags |= SSG_SDOP_HAS_COMPOSITE;
		} else {
			op->time.flags |= SSG_TIMEP_SET;
		}
		if ((pl->pl_flags & PL_BIND_MULTIPLE) != 0) {
			SSG_ParseOpData *mpop = pop;
			uint32_t max_time = 0;
			do {
				if (max_time < mpop->time.v_ms)
					max_time = mpop->time.v_ms;
			} while ((mpop = mpop->next_bound) != NULL);
			op->op_flags |= SSG_SDOP_MULTIPLE;
			op->time.v_ms = max_time;
			pl->pl_flags &= ~PL_BIND_MULTIPLE;
		}
	} else {
		/*
		 * New operator with initial parameter values.
		 */
		op->use_type = (pl->op_scope != NULL) ?
				pl->op_scope->use_type :
				SSG_POP_CARR;
		if (op->use_type == SSG_POP_CARR) {
			op->freq.v0 = sl->sopt.def_freq;
		} else {
			op->op_flags |= SSG_SDOP_NESTED;
			op->freq.v0 = sl->sopt.def_relfreq;
			op->freq.flags |= SSG_RAMPP_STATE_RATIO;
		}
		op->freq.flags |= SSG_RAMPP_STATE;
		op->amp.v0 = 1.0f;
		op->amp.flags |= SSG_RAMPP_STATE;
	}
	op->event = e;
	/*
	 * Add new operator to parent(s), ie. either the current event node,
	 * or an operator node (either ordinary or representing multiple
	 * carriers) in the case of operator linking/nesting.
	 */
	SSG_NodeRange *list = &e->operators;
	if (!pop && pl->op_scope != NULL)
		list = &pl->op_scope->range;
	if (!list->first)
		list->first = op;
	else
		((SSG_ParseOpData*) list->last)->range_next = op;
	list->last = op;
	/*
	 * Assign label. If no new label but previous node
	 * (for a non-composite) has one, update label to
	 * point to new node, but keep pointer in previous node.
	 */
	if (pl->set_label != NULL) {
		op->label = pl->set_label;
		op->label->data = op;
		pl->set_label = NULL;
	} else if (!is_composite && pop != NULL && pop->label != NULL) {
		op->label = pop->label;
		op->label->data = op;
	}
	pl->pl_flags |= PL_ACTIVE_OP;
}

/*
 * Begin a new operator - depending on the context, either for the present
 * event or for a new event begun.
 *
 * Used instead of directly calling begin_operator() and/or begin_event().
 */
static void begin_node(ParseLevel *restrict pl,
		SSG_ParseOpData *restrict previous,
		bool is_composite) {
	pl->op_prev = previous;
	if (!pl->event || /* not in event parse means event now ended */
			pl->sub_f != parse_in_event ||
			pl->next_wait_ms ||
			is_composite)
		begin_event(pl, is_composite);
	begin_operator(pl, is_composite);
}

static void begin_scope(SSG_Parser *restrict o, ParseLevel *restrict pl,
		ParseLevel *restrict parent_pl,
		uint8_t use_type, uint8_t newscope) {
	*pl = (ParseLevel){0};
	pl->o = o;
	pl->scope = newscope;
	if (!parent_pl) {
		// handle newscope == SCOPE_TOP here
		if (use_type != SSG_POP_CARR)
			pl->op_scope = create_op_scope(use_type);
		return;
	}
	pl->parent = parent_pl;
	pl->pl_flags = parent_pl->pl_flags &
		(PL_NESTED_SCOPE | PL_BIND_MULTIPLE);
	pl->sub_f = parent_pl->sub_f;
	pl->event = parent_pl->event;
	pl->operator = parent_pl->operator;
	pl->parent_op = parent_pl->parent_op;
	switch (newscope) {
	case SCOPE_BLOCK:
		pl->group_from = parent_pl->group_from;
		pl->op_scope = parent_pl->op_scope;
		break;
	case SCOPE_BIND:
		pl->group_from = parent_pl->group_from;
		pl->op_scope = create_op_scope(use_type);
		break;
	case SCOPE_NEST:
		pl->pl_flags |= PL_NESTED_SCOPE;
		pl->parent_op = parent_pl->operator;
		pl->op_scope = create_op_scope(use_type);
		break;
	default:
		break;
	}
}

static void end_scope(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	end_operator(pl);
	if (pl->set_label != NULL) {
		SSG_Scanner_warning(o->sc, NULL,
				"ignoring label assignment without operator");
	}
	switch (pl->scope) {
	case SCOPE_TOP: {
		/*
		 * At end of top scope, i.e. at end of script,
		 * end last event and adjust timing.
		 */
		SSG_ParseEvData *group_to;
		end_event(pl);
		group_to = (pl->composite) ? pl->composite : pl->last_event;
		if (group_to)
			group_to->groupfrom = pl->group_from;
		break; }
	case SCOPE_BLOCK:
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		if (pl->pl_flags & PL_ACTIVE_EV) {
			end_event(pl->parent);
			pl->parent->pl_flags |= PL_ACTIVE_EV;
			pl->parent->event = pl->event;
		}
		if (pl->last_event != NULL)
			pl->parent->last_event = pl->last_event;
		break;
	case SCOPE_BIND:
		if (!pl->parent->group_from)
			pl->parent->group_from = pl->group_from;
		/*
		 * Begin multiple-operator node in parent scope
		 * for the operator nodes in this scope,
		 * provided any are present.
		 */
		if (pl->first_operator != NULL) {
			pl->parent->pl_flags |= PL_BIND_MULTIPLE;
			begin_node(pl->parent, pl->first_operator, false);
		}
		break;
	case SCOPE_NEST: {
		if (!pl->parent_op)
			break;
		SSG_ParseOpData *parent_op = pl->parent_op;
		if (!parent_op->nest_scopes)
			parent_op->nest_scopes = pl->op_scope;
		else
			parent_op->last_nest_scope->next = pl->op_scope;
		parent_op->last_nest_scope = pl->op_scope;
		break; }
	default:
		break;
	}
}

/*
 * Main parser functions
 */

static void parse_in_settings(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	ScanLookup *sl = &o->sl;
	SSG_Scanner *sc = o->sc;
	pl->sub_f = parse_in_settings;
	uint8_t c;
	for (;;) {
		c = SSG_Scanner_getc_nospace(sc);
		switch (c) {
		case 'a':
			if (scan_num(sc, NULL, &sl->sopt.ampmult))
				sl->sopt.changed |= SSG_SOPT_AMPMULT;
			break;
		case 'c':
			if (scan_num(sc, scan_chanmix_const,
						&sl->sopt.def_chanmix))
				sl->sopt.changed |= SSG_SOPT_DEF_CHANMIX;
			break;
		case 'f':
			if (scan_num(sc, scan_note_const, &sl->sopt.def_freq))
				sl->sopt.changed |= SSG_SOPT_DEF_FREQ;
			break;
		case 'n': {
			float freq;
			if (scan_num(sc, NULL, &freq)) {
				if (freq < 1.f) {
					SSG_Scanner_warning(sc, NULL,
"ignoring tuning frequency (Hz) below 1.0");
					break;
				}
				sl->sopt.A4_freq = freq;
				sl->sopt.changed |= SSG_SOPT_A4_FREQ;
			}
			break; }
		case 'r':
			if (scan_num(sc, NULL, &sl->sopt.def_relfreq))
				sl->sopt.changed |= SSG_SOPT_DEF_RELFREQ;
			break;
		case 't':
			if (scan_time_val(sc, &sl->sopt.def_time_ms))
				sl->sopt.changed |= SSG_SOPT_DEF_TIME;
			break;
		default:
			goto DEFER;
		}
	}
DEFER:
	SSG_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static void parse_level(SSG_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t use_type, uint8_t newscope);

static bool parse_ev_amp(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	SSG_ParseOpData *op = pl->operator;
	if (scan_ramp(sc, NULL, &op->amp, false))
		op->op_params |= SSG_POPP_AMP;
	if (SSG_Scanner_tryc(sc, ',')) {
		if (scan_ramp(sc, NULL, &op->amp2, false))
			op->op_params |= SSG_POPP_AMP2;
	}
	if (SSG_Scanner_tryc(sc, '~') && SSG_Scanner_tryc(sc, '[')) {
		op->op_params |= SSG_POPP_ADJCS;
		parse_level(o, pl, SSG_POP_AMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_chanmix(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	SSG_ParseEvData *e = pl->event;
	SSG_ParseOpData *op = pl->operator;
	if (op->op_flags & SSG_SDOP_NESTED)
		return true; // reject
	if (scan_ramp(sc, scan_chanmix_const, &e->pan, false))
		e->vo_params |= SSG_PVOP_PAN;
	return false;
}

static bool parse_ev_freq(ParseLevel *restrict pl, bool rel_freq) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	SSG_ParseOpData *op = pl->operator;
	if (rel_freq && !(op->op_flags & SSG_SDOP_NESTED))
		return true; // reject
	SSG_ScanNumConst_f numconst_f = rel_freq ? NULL : scan_note_const;
	if (scan_ramp(sc, numconst_f, &op->freq, rel_freq))
		op->op_params |= SSG_POPP_FREQ;
	if (SSG_Scanner_tryc(sc, ',')) {
		if (scan_ramp(sc, numconst_f, &op->freq2, rel_freq))
			op->op_params |= SSG_POPP_FREQ2;
	}
	if (SSG_Scanner_tryc(sc, '~') && SSG_Scanner_tryc(sc, '[')) {
		op->op_params |= SSG_POPP_ADJCS;
		parse_level(o, pl, SSG_POP_FMOD, SCOPE_NEST);
	}
	return false;
}

static bool parse_ev_phase(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	SSG_ParseOpData *op = pl->operator;
	if (scan_num(sc, NULL, &op->phase)) {
		op->phase = fmod(op->phase, 1.f);
		if (op->phase < 0.f)
			op->phase += 1.f;
		op->op_params |= SSG_POPP_PHASE;
	}
	if (SSG_Scanner_tryc(sc, '+') && SSG_Scanner_tryc(sc, '[')) {
		op->op_params |= SSG_POPP_ADJCS;
		parse_level(o, pl, SSG_POP_PMOD, SCOPE_NEST);
	}
	return false;
}

static void parse_in_event(ParseLevel *restrict pl) {
	SSG_Parser *o = pl->o;
	SSG_Scanner *sc = o->sc;
	SSG_ParseOpData *op = pl->operator;
	pl->sub_f = parse_in_event;
	uint8_t c;
	for (;;) {
		c = SSG_Scanner_getc_nospace(sc);
		switch (c) {
		case '\\':
			if (parse_waittime(pl) && pl->event != NULL) {
				// FIXME: Buggy update node handling
				// for carriers etc. if enabled.
				//begin_node(pl, pl->operator, false);
			}
			break;
		case 'a':
			if (parse_ev_amp(pl)) goto DEFER;
			break;
		case 'c':
			if (parse_ev_chanmix(pl)) goto DEFER;
			break;
		case 'f':
			if (parse_ev_freq(pl, false)) goto DEFER;
			break;
		case 'p':
			if (parse_ev_phase(pl)) goto DEFER;
			break;
		case 'r':
			if (parse_ev_freq(pl, true)) goto DEFER;
			break;
		case 's':
			scan_time_val(sc, &op->silence_ms);
			op->op_params |= SSG_POPP_SILENCE;
			break;
		case 't':
			if (SSG_Scanner_tryc(sc, '*')) {
				/* later fitted or kept to default */
				op->time.v_ms = o->sl.sopt.def_time_ms;
				op->time.flags = 0;
			} else if (SSG_Scanner_tryc(sc, 'i')) {
				if (!(op->op_flags & SSG_SDOP_NESTED)) {
					SSG_Scanner_warning(sc, NULL,
"ignoring 'ti' (infinite time) for non-nested operator");
					break;
				}
				op->time.flags |= SSG_TIMEP_SET
					| SSG_TIMEP_LINKED;
			} else {
				if (!scan_time_val(sc, &op->time.v_ms))
					break;
				op->time.flags = SSG_TIMEP_SET;
			}
			op->op_params |= SSG_POPP_TIME;
			break;
		case 'w': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			op->wave = wave;
			op->op_params |= SSG_POPP_WAVE;
			break; }
		default:
			goto DEFER;
		}
	}
DEFER:
	SSG_Scanner_ungetc(sc);
	pl->pl_flags |= PL_DEFERRED_SUB; /* let parse_level() look at it */
}

static void parse_level(SSG_Parser *restrict o,
		ParseLevel *restrict parent_pl,
		uint8_t use_type, uint8_t newscope) {
	ParseLevel pl;
	SSG_SymStr *label;
	begin_scope(o, &pl, parent_pl, use_type, newscope);
	++o->call_level;
	SSG_Scanner *sc = o->sc;
	uint8_t c;
	for (;;) {
		c = SSG_Scanner_getc_nospace(sc);
		switch (c) {
		case SSG_SCAN_LNBRK:
			if (pl.scope == SCOPE_TOP) {
				/*
				 * On top level of script,
				 * each line has a new "subscope".
				 */
				if (o->call_level > 1)
					goto RETURN;
				pl.sub_f = NULL;
				pl.first_operator = NULL;
			}
			break;
		case '\'':
			/*
			 * Label assignment (set to what follows).
			 */
			if (pl.set_label != NULL) {
				SSG_Scanner_warning(sc, NULL,
"ignoring label assignment to label assignment");
				break;
			}
			pl.set_label = label = scan_label(sc, c);
			break;
		case ';':
			if (pl.sub_f == parse_in_settings || !pl.event)
				goto INVALID;
			begin_node(&pl, pl.operator, true);
			parse_in_event(&pl);
			break;
		case '<':
			parse_level(o, &pl, use_type, SCOPE_BLOCK);
			break;
		case '>':
			if (pl.scope == SCOPE_BLOCK) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, '>', '<');
			break;
		case '@':
			if (SSG_Scanner_tryc(sc, '[')) {
				end_operator(&pl);
				parse_level(o, &pl, use_type, SCOPE_BIND);
				/*
				 * Multiple-operator node now open.
				 */
				parse_in_event(&pl);
				break;
			}
			/*
			 * Label reference (get and use value).
			 */
			if (pl.set_label != NULL) {
				SSG_Scanner_warning(sc, NULL,
"ignoring label assignment to label reference");
				pl.set_label = NULL;
			}
			pl.sub_f = NULL;
			label = scan_label(sc, c);
			if (label != NULL) {
				SSG_ParseOpData *ref = label->data;
				if (!ref)
					SSG_Scanner_warning(sc, NULL,
"ignoring reference to undefined label");
				else {
					begin_node(&pl, ref, false);
					parse_in_event(&pl);
				}
			}
			break;
		case 'O': {
			size_t wave;
			if (!scan_wavetype(sc, &wave))
				break;
			begin_node(&pl, NULL, false);
			pl.operator->wave = wave;
			parse_in_event(&pl);
			break; }
		case 'Q':
			goto FINISH;
		case 'S':
			parse_in_settings(&pl);
			break;
		case '[':
			warn_opening_disallowed(sc, '[');
			break;
		case '\\':
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			parse_waittime(&pl);
			break;
		case ']':
			if (pl.scope == SCOPE_NEST) {
				end_operator(&pl);
			}
			if (pl.scope > SCOPE_BLOCK) {
				goto RETURN;
			}
			warn_closing_without_opening(sc, ']', '[');
			break;
		case '{':
			warn_opening_disallowed(sc, '{');
			break;
		case '|':
			if (pl.sub_f == parse_in_settings ||
					((pl.pl_flags & PL_NESTED_SCOPE) != 0
					&& pl.event != NULL))
				goto INVALID;
			if (!pl.event) {
				SSG_Scanner_warning(sc, NULL,
"no sounds precede time separator");
				break;
			}
			if (pl.group_from != NULL) {
				SSG_ParseEvData *group_to = (pl.composite) ?
					pl.composite :
					pl.event;
				group_to->groupfrom = pl.group_from;
				pl.group_from = NULL;
			}
			end_event(&pl);
			pl.sub_f = NULL;
			break;
		case '}':
			warn_closing_without_opening(sc, '}', '{');
			break;
		default:
		INVALID:
			if (!handle_unknown_or_eof(sc, c)) goto FINISH;
			break;
		}
		/*
		 * Return to any sub-parsing routine.
		 */
		if (pl.sub_f != NULL && !(pl.pl_flags & PL_DEFERRED_SUB))
			pl.sub_f(&pl);
		pl.pl_flags &= ~PL_DEFERRED_SUB;
	}
FINISH:
	if (newscope > SCOPE_BLOCK)
		warn_eof_without_closing(sc, ']');
	else if (newscope == SCOPE_BLOCK)
		warn_eof_without_closing(sc, '>');
RETURN:
	end_scope(&pl);
	--o->call_level;
}

/*
 * Process file.
 *
 * \return name of script, or NULL on error preventing parse
 */
static const char *parse_file(SSG_Parser *restrict o,
		const char *restrict script, bool is_path) {
	SSG_Scanner *sc = o->sc;
	const char *name;
	if (!SSG_Scanner_open(sc, script, is_path))
		return NULL;
	parse_level(o, NULL, SSG_POP_CARR, SCOPE_TOP);
	name = sc->f->path;
	SSG_Scanner_close(sc);
	return name;
}

/**
 * Parse a file and return script data.
 *
 * \return instance or NULL on error preventing parse
 */
SSG_Parse* SSG_create_Parse(const char *restrict script_arg, bool is_path) {
	if (!script_arg)
		return NULL;
	SSG_Parser pr;
	SSG_Parse *o = NULL;
	init_Parser(&pr);
	const char *name = parse_file(&pr, script_arg, is_path);
	if (!name) goto DONE;

	o = calloc(1, sizeof(SSG_Parse));
	o->events = pr.first_ev;
	o->name = name;
	o->sopt = pr.sl.sopt;
DONE:
	fini_Parser(&pr);
	return o;
}

/**
 * Destroy instance.
 */
void SSG_destroy_Parse(SSG_Parse *restrict o) {
	if (!o)
		return;
	SSG_ParseEvData *e;
	for (e = o->events; e != NULL; ) {
		SSG_ParseEvData *e_next = e->next;
		destroy_event_node(e);
		e = e_next;
	}
	free(o);
}
