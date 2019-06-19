/* saugns: Parser output data and functions.
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
#include "symtab.h"
#include "../help.h"

/**
 * Linked list of node ranges each for a parse data sublist.
 */
typedef struct SAU_ParseSublist {
	SAU_NodeRange range;
	struct SAU_ParseSublist *next;
	uint8_t use_type;
} SAU_ParseSublist;

/**
 * Linked list of duration groupings of nodes.
 *
 * Each event links to the duration node range
 * used to calculate duration and default time
 * for that piece of the script.
 */
typedef struct SAU_ParseDurGroup {
	SAU_NodeRange range;
	struct SAU_ParseDurGroup *next;
} SAU_ParseDurGroup;

/**
 * Parse data operator flags.
 */
enum {
	SAU_PDOP_MULTIPLE = 1<<0,
	SAU_PDOP_NESTED = 1<<1,
	SAU_PDOP_SILENCE_ADDED = 1<<2,
	SAU_PDOP_HAS_COMPOSITE = 1<<3,
	SAU_PDOP_IGNORED = 1<<4, // node skipped by parseconv
};

/**
 * Node type for operator data.
 */
typedef struct SAU_ParseOpData {
	struct SAU_ParseOpData *range_next;
	struct SAU_ParseEvData *event;
	struct SAU_ParseOpData *prev; /* preceding for same op(s) */
	SAU_ParseSublist *nest_scopes;
	SAU_ParseSublist *last_nest_scope;
	struct SAU_ParseOpData *next_bound;
	SAU_SymStr *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	SAU_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	uint8_t use_type;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	/* for parseconv */
	void *op_conv;
	void *op_context;
} SAU_ParseOpData;

/**
 * Parse data event flags.
 */
enum {
	SAU_PDEV_ADD_WAIT_DURATION = 1<<0,
};

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SAU_ParseEvData {
	struct SAU_ParseEvData *next;
	struct SAU_ParseEvData *composite;
	SAU_ParseDurGroup *dur;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SAU_NodeRange operators; /* operator nodes directly linked from event */
	/* voice parameters */
	uint32_t vo_params;
	struct SAU_ParseEvData *vo_prev; /* preceding event for same voice */
	SAU_Ramp pan;
	/* for parseconv */
	void *ev_conv;
	void *vo_context;
} SAU_ParseEvData;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Parse {
	SAU_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
	SAU_SymTab *symtab;
	SAU_MemPool *mem; // internally used, provided until destroy
} SAU_Parse;

SAU_Parse *SAU_create_Parse(const char *restrict script_arg, bool is_path);
void SAU_destroy_Parse(SAU_Parse *restrict o);
