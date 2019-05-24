/* ssndgen: Parser output data and functions.
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

/**
 * Linked list of node ranges each for a parse data sublist.
 */
typedef struct SSG_ParseSublist {
	SSG_NodeRange range;
	struct SSG_ParseSublist *next;
	uint8_t use_type;
} SSG_ParseSublist;

/**
 * Node type for operator data.
 */
typedef struct SSG_ParseOpData {
	struct SSG_ParseOpData *range_next;
	struct SSG_ParseEvData *event;
	SSG_ParseSublist *nest_scopes;
	SSG_ParseSublist *last_nest_scope;
	struct SSG_ParseOpData *next_bound;
	SSG_SymStr *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	SSG_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	uint8_t use_type;
	SSG_Ramp freq, freq2;
	SSG_Ramp amp, amp2;
	float phase;
	struct SSG_ParseOpData *op_prev; /* preceding for same op(s) */
	void *op_conv; /* for parseconv */
} SSG_ParseOpData;

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SSG_ParseEvData {
	struct SSG_ParseEvData *next;
	struct SSG_ParseEvData *groupfrom;
	struct SSG_ParseEvData *composite;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SSG_NodeRange operators; /* operator nodes directly linked from event */
	void *ev_conv; /* for parseconv */
	/* voice parameters */
	uint32_t vo_params;
	struct SSG_ParseEvData *vo_prev; /* preceding event for voice */
	SSG_Ramp pan;
} SSG_ParseEvData;

/**
 * Type returned after processing a file.
 */
typedef struct SSG_Parse {
	SSG_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SSG_ScriptOptions sopt;
} SSG_Parse;

SSG_Parse *SSG_create_Parse(const char *restrict script_arg, bool is_path);
void SSG_destroy_Parse(SSG_Parse *restrict o);
