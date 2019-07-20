/* saugns: Audio program data and functions.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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
#include "time.h"
#include "ramp.h"
#include "wave.h"

/*
 * Program types and definitions.
 */

/**
 * Voice parameter flags.
 */
enum {
	SAU_PVOP_GRAPH = 1<<0,
	SAU_PVOP_PAN = 1<<1,
	SAU_PVO_PARAMS = (1<<2) - 1
};

/**
 * Operator parameter flags.
 */
enum {
	SAU_POPP_WAVE = 1<<0,
	SAU_POPP_TIME = 1<<1,
	SAU_POPP_SILENCE = 1<<2,
	SAU_POPP_FREQ = 1<<3,
	SAU_POPP_FREQ2 = 1<<4,
	SAU_POPP_PHASE = 1<<5,
	SAU_POPP_AMP = 1<<6,
	SAU_POPP_AMP2 = 1<<7,
	SAU_POP_PARAMS = (1<<8) - 1
};

/*
 * Voice ID constants.
 */
#define SAU_PVO_NO_ID  UINT16_MAX       /* voice ID missing */
#define SAU_PVO_MAX_ID (UINT16_MAX - 1) /* error if exceeded */

/*
 * Operator ID constants.
 */
#define SAU_POP_NO_ID  UINT32_MAX       /* operator ID missing */
#define SAU_POP_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

/**
 * Operator use types.
 */
enum {
	SAU_POP_CARR = 0,
	SAU_POP_FMOD,
	SAU_POP_PMOD,
	SAU_POP_AMOD,
	SAU_POP_USES,
};

typedef struct SAU_ProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} SAU_ProgramOpRef;

typedef struct SAU_ProgramOpList {
	uint32_t count;
	uint32_t ids[];
} SAU_ProgramOpList;

typedef struct SAU_ProgramVoData {
	const SAU_ProgramOpList *carriers;
	uint32_t params;
	SAU_Ramp pan;
	const struct SAU_ProgramVoData *prev;
} SAU_ProgramVoData;

typedef struct SAU_ProgramOpData {
	const SAU_ProgramOpList *fmods;
	const SAU_ProgramOpList *pmods;
	const SAU_ProgramOpList *amods;
	uint32_t id;
	uint32_t params;
	SAU_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	const struct SAU_ProgramOpData *prev;
} SAU_ProgramOpData;

typedef struct SAU_ProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const SAU_ProgramVoData *vo_data;
	const SAU_ProgramOpData *op_data;
} SAU_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SAU_PMODE_AMP_DIV_VOICES = 1<<0,
};

struct SAU_MemPool;

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SAU_Program {
	const SAU_ProgramEvent **events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint32_t duration_ms;
	const char *name;
	struct SAU_MemPool *mem; // internally used, provided until destroy
} SAU_Program;

struct SAU_Script;
SAU_Program* SAU_build_Program(struct SAU_Script *restrict sd) sauMalloclike;
void SAU_discard_Program(SAU_Program *restrict o);

void SAU_Program_print_info(const SAU_Program *restrict o,
		const char *restrict name_prefix,
		const char *restrict name_suffix);
void SAU_ProgramEvent_print_voice(const SAU_ProgramEvent *restrict ev);
void SAU_ProgramEvent_print_operators(const SAU_ProgramEvent *restrict ev);
