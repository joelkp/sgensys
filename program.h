/* ssndgen: Audio program data and functions.
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
	SSG_PVOP_GRAPH = 1<<0,
	SSG_PVOP_PAN = 1<<1,
	SSG_PVO_PARAMS = (1<<2) - 1
};

/**
 * Operator parameter flags.
 */
enum {
	SSG_POPP_WAVE = 1<<0,
	SSG_POPP_TIME = 1<<1,
	SSG_POPP_SILENCE = 1<<2,
	SSG_POPP_FREQ = 1<<3,
	SSG_POPP_FREQ2 = 1<<4,
	SSG_POPP_PHASE = 1<<5,
	SSG_POPP_AMP = 1<<6,
	SSG_POPP_AMP2 = 1<<7,
	SSG_POP_PARAMS = (1<<8) - 1
};

/*
 * Voice ID constants.
 */
#define SSG_PVO_NO_ID  UINT16_MAX       /* voice ID missing */
#define SSG_PVO_MAX_ID (UINT16_MAX - 1) /* error if exceeded */

/*
 * Operator ID constants.
 */
#define SSG_POP_NO_ID  UINT32_MAX       /* operator ID missing */
#define SSG_POP_MAX_ID (UINT32_MAX - 1) /* error if exceeded */

/**
 * Operator use types.
 */
enum {
	SSG_POP_CARR = 0,
	SSG_POP_FMOD,
	SSG_POP_PMOD,
	SSG_POP_AMOD,
	SSG_POP_USES,
};

typedef struct SSG_ProgramOpRef {
	uint32_t id;
	uint8_t use;
	uint8_t level; /* > 0 if used as a modulator */
} SSG_ProgramOpRef;

typedef struct SSG_ProgramOpList {
	uint32_t count;
	uint32_t ids[];
} SSG_ProgramOpList;

typedef struct SSG_ProgramVoData {
	const SSG_ProgramOpList *carriers;
	uint32_t params;
	SSG_Ramp pan;
	const struct SSG_ProgramVoData *prev;
} SSG_ProgramVoData;

typedef struct SSG_ProgramOpData {
	const SSG_ProgramOpList *fmods;
	const SSG_ProgramOpList *pmods;
	const SSG_ProgramOpList *amods;
	uint32_t id;
	uint32_t params;
	SSG_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	SSG_Ramp freq, freq2;
	SSG_Ramp amp, amp2;
	float phase;
	const struct SSG_ProgramOpData *prev;
} SSG_ProgramOpData;

typedef struct SSG_ProgramEvent {
	uint32_t wait_ms;
	uint16_t vo_id;
	uint32_t op_data_count;
	const SSG_ProgramVoData *vo_data;
	const SSG_ProgramOpData *op_data;
} SSG_ProgramEvent;

/**
 * Program flags affecting interpretation.
 */
enum {
	SSG_PMODE_AMP_DIV_VOICES = 1<<0,
};

struct SSG_MemPool;

/**
 * Main program type. Contains everything needed for interpretation.
 */
typedef struct SSG_Program {
	const SSG_ProgramEvent **events;
	size_t ev_count;
	uint16_t mode;
	uint16_t vo_count;
	uint32_t op_count;
	uint32_t duration_ms;
	const char *name;
	struct SSG_MemPool *mem; // internally used, provided until destroy
} SSG_Program;

struct SSG_Script;
SSG_Program* SSG_build_Program(struct SSG_Script *restrict sd);
void SSG_discard_Program(SSG_Program *restrict o);

void SSG_Program_print_info(const SSG_Program *restrict o,
		const char *restrict name_prefix,
		const char *restrict name_suffix);
void SSG_ProgramEvent_print_voice(const SSG_ProgramEvent *restrict ev);
void SSG_ProgramEvent_print_operators(const SSG_ProgramEvent *restrict ev);
