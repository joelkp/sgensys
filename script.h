/* ssndgen: Script file data and functions.
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
#include "reflist.h"
#include "program.h"

/**
 * Points to bounding members of a linearly ordered list of nodes.
 */
typedef struct SSG_NodeRange {
	void *first, *last;
} SSG_NodeRange;

/**
 * Script data operator flags.
 */
enum {
	SSG_SDOP_NEW_CARRIER = 1<<0,
	SSG_SDOP_LATER_USED = 1<<1,
	SSG_SDOP_MULTIPLE = 1<<2,
	SSG_SDOP_NESTED = 1<<3,
	SSG_SDOP_SILENCE_ADDED = 1<<4,
	SSG_SDOP_HAS_COMPOSITE = 1<<5,
};

/**
 * Node type for operator data.
 */
typedef struct SSG_ScriptOpData {
	struct SSG_ScriptOpData *range_next;
	struct SSG_ScriptEvData *event;
	struct SSG_ScriptOpData *next_bound;
	struct SSG_ScriptOpData *prev_use; /* for same op(s) */
	const char *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_id; // for scriptconv
	uint32_t op_params;
	SSG_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	SSG_Ramp freq, freq2;
	SSG_Ramp amp, amp2;
	float phase;
	/* new node adjacents in operator linkage graph */
	SSG_RefList *mod_lists;
} SSG_ScriptOpData;

/**
 * Script data event flags.
 */
enum {
	SSG_SDEV_NEW_OPGRAPH = 1<<0,
	SSG_SDEV_VOICE_LATER_USED = 1<<1,
	SSG_SDEV_ADD_WAIT_DURATION = 1<<2,
};

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SSG_ScriptEvData {
	struct SSG_ScriptEvData *next;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SSG_NodeRange op_all;
	/* voice parameters */
	uint32_t vo_id;
	uint32_t vo_params;
	struct SSG_ScriptEvData *vo_prev; /* preceding event for voice */
	SSG_Ramp pan;
	SSG_RefList *carriers;
} SSG_ScriptEvData;

/**
 * Script data option flags.
 *
 * Set after parsing the setting of script options in a script.
 */
enum {
	SSG_SOPT_AMPMULT = 1<<0,
	SSG_SOPT_A4_FREQ = 1<<1,
	SSG_SOPT_DEF_TIME = 1<<2,
	SSG_SOPT_DEF_FREQ = 1<<3,
	SSG_SOPT_DEF_RELFREQ = 1<<4,
	SSG_SOPT_DEF_CHANMIX = 1<<5,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SSG_ScriptOptions {
	uint32_t changed; // flags (SSG_SOPT_*) set upon change by script
	float ampmult;    // amplitude multiplier for non-modulator operators
	float A4_freq;    // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_relfreq,
	      def_chanmix;
} SSG_ScriptOptions;

struct SSG_MemPool;

/**
 * Type returned after processing a file.
 */
typedef struct SSG_Script {
	SSG_ScriptEvData *events;
	const char *name; // currently simply set to the filename
	SSG_ScriptOptions sopt;
	struct SSG_MemPool *mem; // internally used, provided until destroy
} SSG_Script;

SSG_Script *SSG_load_Script(const char *restrict script_arg, bool is_path);
void SSG_discard_Script(SSG_Script *restrict o);
