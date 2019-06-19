/* saugns: Audio program interpreter module.
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

#include "interp.h"
#include "prealloc.h"
#include "mixer.h"
#include <stdio.h>

#define BUF_LEN SAU_MIX_BUFLEN
typedef float Buf[BUF_LEN];

struct SAU_Interp {
	const SAU_Program *prg;
	uint32_t srate;
	uint32_t buf_count;
	Buf *bufs;
	SAU_Mixer *mixer;
	size_t event, ev_count;
	EventNode **events;
	uint32_t event_pos;
	uint16_t voice, vo_count;
	VoiceNode *voices;
	OperatorNode *operators;
	SAU_MemPool *mem;
};

static bool init_for_program(SAU_Interp *restrict o,
		const SAU_Program *restrict prg, uint32_t srate) {
	SAU_PreAlloc pa;
	if (!SAU_fill_PreAlloc(&pa, prg, srate, o->mem))
		return false;
	o->prg = prg;
	o->srate = srate;
	o->events = pa.events;
	o->ev_count = pa.ev_count;
	o->operators = pa.operators;
	o->voices = pa.voices;
	o->vo_count = pa.vo_count;
	if (pa.max_bufs > 0) {
		o->bufs = SAU_MemPool_alloc(o->mem,
				pa.max_bufs * sizeof(Buf));
		if (!o->bufs) goto ERROR;
		o->buf_count = pa.max_bufs;
	}
	o->mixer = SAU_create_Mixer();
	if (!o->mixer) goto ERROR;

	float scale = 1.f;
	if ((prg->mode & SAU_PMODE_AMP_DIV_VOICES) != 0)
		scale /= o->vo_count;
	SAU_Mixer_set_srate(o->mixer, srate);
	SAU_Mixer_set_scale(o->mixer, scale);
	return true;
ERROR:
	return false;
}

/**
 * Create instance for program \p prg and sample rate \p srate.
 */
SAU_Interp *SAU_create_Interp(const SAU_Program *restrict prg,
		uint32_t srate) {
	SAU_MemPool *mem = SAU_create_MemPool(0);
	if (!mem)
		return NULL;
	SAU_Interp *o = SAU_MemPool_alloc(mem, sizeof(SAU_Interp));
	if (!o) {
		SAU_destroy_MemPool(mem);
		return NULL;
	}
	o->mem = mem;
	if (!init_for_program(o, prg, srate)) {
		SAU_destroy_Interp(o);
		return NULL;
	}
	SAU_global_init_Wave();
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_Interp(SAU_Interp *restrict o) {
	if (!o)
		return;
	SAU_destroy_Mixer(o->mixer);
	SAU_destroy_MemPool(o->mem);
}

/*
 * Set voice duration according to the current list of operators.
 */
static void set_voice_duration(SAU_Interp *restrict o,
		VoiceNode *restrict vn) {
	uint32_t time = 0;
	for (uint32_t i = 0; i < vn->graph_count; ++i) {
		const SAU_ProgramOpRef *or = &vn->graph[i];
		if (or->use != SAU_POP_CARR) continue;
		OperatorNode *on = &o->operators[or->id];
		if (on->time > time)
			time = on->time;
	}
	vn->duration = time;
}

/*
 * Process an event update for a ramp parameter.
 */
static void handle_ramp_update(SAU_Ramp *restrict ramp,
		uint32_t *restrict ramp_pos,
		const SAU_Ramp *restrict ramp_src) {
	if ((ramp_src->flags & SAU_RAMPP_GOAL) != 0) {
		*ramp_pos = 0;
	}
	SAU_Ramp_copy(ramp, ramp_src);
}

/*
 * Process one event; to be called for the event when its time comes.
 */
static void handle_event(SAU_Interp *restrict o, EventNode *restrict e) {
	if (1) /* more types to be added in the future */ {
		/*
		 * Set state of operator and/or voice.
		 *
		 * Voice updates must be done last, to take into account
		 * updates for their operators.
		 */
		const SAU_ProgramEvent *prg_e = e->prg_e;
		for (size_t i = 0; i < prg_e->op_data_count; ++i) {
			const SAU_ProgramOpData *od = &prg_e->op_data[i];
			OperatorNode *on = &o->operators[od->id];
			uint32_t params = od->params;
			on->fmods = od->fmods;
			on->pmods = od->pmods;
			on->amods = od->amods;
			if (params & SAU_POPP_WAVE)
				on->osc.lut = SAU_Osc_LUT(od->wave);
			if (params & SAU_POPP_TIME) {
				const SAU_Time *src = &od->time;
				if (src->flags & SAU_TIMEP_LINKED) {
					on->time = 0;
					on->flags |= ON_TIME_INF;
				} else {
					on->time = SAU_MS_IN_SAMPLES(src->v_ms,
							o->srate);
					on->flags &= ~ON_TIME_INF;
				}
			}
			if (params & SAU_POPP_SILENCE)
				on->silence = SAU_MS_IN_SAMPLES(od->silence_ms,
						o->srate);
			if (params & SAU_POPP_FREQ)
				handle_ramp_update(&on->freq,
						&on->freq_pos, &od->freq);
			if (params & SAU_POPP_FREQ2)
				handle_ramp_update(&on->freq2,
						&on->freq2_pos, &od->freq2);
			if (params & SAU_POPP_PHASE)
				on->osc.phase = SAU_Osc_PHASE(od->phase);
			if (params & SAU_POPP_AMP)
				handle_ramp_update(&on->amp,
						&on->amp_pos, &od->amp);
			if (params & SAU_POPP_AMP2)
				handle_ramp_update(&on->amp2,
						&on->amp2_pos, &od->amp2);
		}
		if (prg_e->vo_id != SAU_PVO_NO_ID) {
			const SAU_ProgramVoData *vd = prg_e->vo_data;
			VoiceNode *vn = &o->voices[prg_e->vo_id];
			uint32_t params = (vd != NULL) ? vd->params : 0;
			if (e->graph != NULL) {
				vn->graph = e->graph;
				vn->graph_count = e->graph_count;
			}
			if (params & SAU_PVOP_PAN)
				handle_ramp_update(&vn->pan,
						&vn->pan_pos, &vd->pan);
			vn->flags |= VN_INIT;
			vn->pos = 0;
			if (o->voice > prg_e->vo_id) {
				/* go back to re-activated node */
				o->voice = prg_e->vo_id;
			}
			set_voice_duration(o, vn);
		}
	}
}

/*
 * Generate up to buf_len samples for an operator node,
 * the remainder (if any) zero-filled if acc_ind is zero.
 *
 * Recursively visits the subnodes of the operator node,
 * if any.
 *
 * Returns number of samples generated for the node.
 */
static uint32_t run_block(SAU_Interp *restrict o,
		Buf *restrict bufs, uint32_t buf_len,
		OperatorNode *restrict n,
		float *restrict parent_freq,
		bool wave_env, uint32_t acc_ind) {
	uint32_t i, len = buf_len;
	float *s_buf = *(bufs++), *pm_buf;
	float *freq, *amp;
	/*
	 * If silence, zero-fill and delay processing for duration.
	 */
	uint32_t zero_len = 0;
	if (n->silence) {
		zero_len = n->silence;
		if (zero_len > len)
			zero_len = len;
		if (!acc_ind) for (i = 0; i < zero_len; ++i)
			s_buf[i] = 0;
		len -= zero_len;
		if (!(n->flags & ON_TIME_INF)) n->time -= zero_len;
		n->silence -= zero_len;
		if (!len)
			return zero_len;
		s_buf += zero_len;
	}
	/*
	 * Guard against circular references.
	 */
	if ((n->flags & ON_VISITED) != 0) {
		for (i = 0; i < len; ++i)
			s_buf[i] = 0;
		return zero_len + len;
	}
	n->flags |= ON_VISITED;
	/*
	 * Limit length to time duration of operator.
	 */
	uint32_t skip_len = 0;
	if (n->time < len && !(n->flags & ON_TIME_INF)) {
		skip_len = len - n->time;
		len = n->time;
	}
	/*
	 * Handle frequency, including frequency modulation
	 * if modulators linked.
	 */
	freq = *(bufs++);
	SAU_Ramp_run(&n->freq, &n->freq_pos, freq, len, o->srate, parent_freq);
	if (n->fmods->count > 0) {
		float *freq2 = *(bufs++);
		SAU_Ramp_run(&n->freq2, &n->freq2_pos,
				freq2, len, o->srate, parent_freq);
		const uint32_t *fmods = n->fmods->ids;
		for (i = 0; i < n->fmods->count; ++i)
			run_block(o, bufs, len, &o->operators[fmods[i]],
					freq, true, i);
		float *fm_buf = *bufs;
		for (i = 0; i < len; ++i)
			freq[i] += (freq2[i] - freq[i]) * fm_buf[i];
	} else {
		SAU_Ramp_skip(&n->freq2, &n->freq2_pos, len, o->srate);
	}
	/*
	 * If phase modulators linked, get phase offsets for modulation.
	 */
	pm_buf = NULL;
	if (n->pmods->count > 0) {
		const uint32_t *pmods = n->pmods->ids;
		for (i = 0; i < n->pmods->count; ++i)
			run_block(o, bufs, len, &o->operators[pmods[i]],
					freq, false, i);
		pm_buf = *(bufs++);
	}
	/*
	 * Handle amplitude parameter, including amplitude modulation if
	 * modulators linked.
	 */
	amp = *(bufs++);
	SAU_Ramp_run(&n->amp, &n->amp_pos, amp, len, o->srate, NULL);
	if (n->amods->count > 0) {
		float *amp2 = *(bufs++);
		SAU_Ramp_run(&n->amp2, &n->amp2_pos, amp2, len, o->srate, NULL);
		const uint32_t *amods = n->amods->ids;
		for (i = 0; i < n->amods->count; ++i)
			run_block(o, bufs, len, &o->operators[amods[i]],
					freq, true, i);
		float *am_buf = *bufs;
		for (i = 0; i < len; ++i)
			amp[i] += (amp2[i] - amp[i]) * am_buf[i];
	} else {
		SAU_Ramp_skip(&n->amp2, &n->amp2_pos, len, o->srate);
	}
	if (!wave_env) {
		SAU_Osc_run(&n->osc, s_buf, len, acc_ind, freq, amp, pm_buf);
	} else {
		SAU_Osc_run_env(&n->osc, s_buf, len, acc_ind, freq, amp, pm_buf);
	}
	/*
	 * Update time duration left, zero rest of buffer if unfilled.
	 */
	if (!(n->flags & ON_TIME_INF)) {
		if (!acc_ind && skip_len > 0) {
			s_buf += len;
			for (i = 0; i < skip_len; ++i)
				s_buf[i] = 0;
		}
		n->time -= len;
	}
	n->flags &= ~ON_VISITED;
	return zero_len + len;
}

/*
 * Generate up to BUF_LEN samples for a voice, mixed into the
 * mix buffers.
 *
 * \return number of samples generated
 */
static uint32_t run_voice(SAU_Interp *restrict o,
		VoiceNode *restrict vn, uint32_t len) {
	uint32_t out_len = 0;
	const SAU_ProgramOpRef *ops = vn->graph;
	uint32_t opc = vn->graph_count;
	if (!ops)
		return 0;
	uint32_t acc_ind = 0;
	uint32_t time;
	uint32_t i;
	time = vn->duration;
	if (len > BUF_LEN) len = BUF_LEN;
	if (time > len) time = len;
	for (i = 0; i < opc; ++i) {
		uint32_t last_len;
		// TODO: finish redesign
		if (ops[i].use != SAU_POP_CARR) continue;
		OperatorNode *n = &o->operators[ops[i].id];
		if (n->time == 0) continue;
		last_len = run_block(o, o->bufs, time, n,
				NULL, false, acc_ind++);
		if (last_len > out_len) out_len = last_len;
	}
	if (out_len > 0) {
		SAU_Mixer_add(o->mixer, o->bufs[0], out_len,
				&vn->pan, &vn->pan_pos);
	}
	vn->duration -= time;
	vn->pos += time;
	return out_len;
}

/*
 * Run voices for \p time, repeatedly generating up to BUF_LEN samples
 * and writing them into the 16-bit stereo (interleaved) buffer \p buf.
 *
 * \return number of samples generated
 */
static uint32_t run_for_time(SAU_Interp *restrict o,
		uint32_t time, int16_t *restrict buf) {
	int16_t *sp = buf;
	uint32_t gen_len = 0;
	while (time > 0) {
		uint32_t len = time;
		if (len > BUF_LEN) len = BUF_LEN;
		SAU_Mixer_clear(o->mixer);
		uint32_t last_len = 0;
		for (uint32_t i = o->voice; i < o->vo_count; ++i) {
			VoiceNode *vn = &o->voices[i];
			if (vn->pos < 0) {
				/*
				 * Wait times accumulate across nodes.
				 *
				 * Reduce length by wait time and
				 * end if wait time(s) have swallowed it up.
				 */
				uint32_t wait_time = (uint32_t) -vn->pos;
				if (wait_time >= len) {
					vn->pos += len;
					break;
				}
				sp += wait_time+wait_time; /* stereo double */
				len -= wait_time;
				gen_len += wait_time;
				vn->pos = 0;
			}
			if (vn->duration != 0) {
				uint32_t voice_len = run_voice(o, vn, len);
				if (voice_len > last_len) last_len = voice_len;
			}
		}
		time -= len;
		if (last_len > 0) {
			gen_len += last_len;
			SAU_Mixer_write(o->mixer, &sp, last_len);
		}
	}
	return gen_len;
}

/*
 * Any error checking following audio generation goes here.
 */
static void check_final_state(SAU_Interp *restrict o) {
	for (uint16_t i = 0; i < o->vo_count; ++i) {
		VoiceNode *vn = &o->voices[i];
		if (!(vn->flags & VN_INIT)) {
			SAU_warning("interp",
"voice %hd left uninitialized (never used)", i);
		}
	}
}

/**
 * Main audio generation/processing function. Call repeatedly to write
 * buf_len new samples into the interleaved stereo buffer buf. Any values
 * after the end of the signal will be zero'd.
 *
 * \return number of samples generated, buf_len unless signal ended
 */
size_t SAU_Interp_run(SAU_Interp *restrict o,
		int16_t *restrict buf, size_t buf_len) {
	int16_t *sp = buf;
	uint32_t i, len = buf_len;
	for (i = len; i--; sp += 2) {
		sp[0] = 0;
		sp[1] = 0;
	}
	sp = buf;
	uint32_t skip_len, last_len, gen_len = 0;
PROCESS:
	skip_len = 0;
	while (o->event < o->ev_count) {
		EventNode *e = o->events[o->event];
		if (o->event_pos < e->wait) {
			/*
			 * Limit voice running len to wait.
			 *
			 * Split processing into two blocks when needed to
			 * ensure event handling runs before voices.
			 */
			uint32_t wait = e->wait - o->event_pos;
			if (wait < len) {
				skip_len = len - wait;
				len = wait;
			}
			o->event_pos += len;
			break;
		}
		handle_event(o, e);
		++o->event;
		o->event_pos = 0;
	}
	last_len = run_for_time(o, len, sp);
	if (skip_len > 0) {
		gen_len += len;
		sp += len+len; /* stereo double */
		len = skip_len;
		goto PROCESS;
	} else {
		gen_len += last_len;
	}
	/*
	 * Advance starting voice and check for end of signal.
	 */
	for(;;) {
		VoiceNode *vn;
		if (o->voice == o->vo_count) {
			if (o->event != o->ev_count) break;
			/*
			 * The end.
			 */
			check_final_state(o);
			return gen_len;
		}
		vn = &o->voices[o->voice];
		if (vn->duration != 0) break;
		++o->voice;
	}
	/*
	 * Further calls needed to complete signal.
	 */
	return buf_len;
}

static void print_graph(const SAU_ProgramOpRef *restrict graph,
		uint32_t count) {
	static const char *const uses[SAU_POP_USES] = {
		"CA",
		"FM",
		"PM",
		"AM"
	};
	if (!graph)
		return;

	uint32_t i = 0;
	uint32_t max_indent = 0;
	fputs("\n\t    [", stdout);
	for (;;) {
		const uint32_t indent = graph[i].level * 2;
		if (indent > max_indent) max_indent = indent;
		fprintf(stdout, "%6d:  ", graph[i].id);
		for (uint32_t j = indent; j > 0; --j)
			putc(' ', stdout);
		fputs(uses[graph[i].use], stdout);
		if (++i == count) break;
		fputs("\n\t     ", stdout);
	}
	for (uint32_t j = max_indent; j > 0; --j)
		putc(' ', stdout);
	putc(']', stdout);
}

/**
 * Print information about contents to be interpreted.
 */
void SAU_Interp_print(const SAU_Interp *restrict o) {
	SAU_Program_print_info(o->prg, "Program: \"", "\"");
	for (size_t ev_id = 0; ev_id < o->ev_count; ++ev_id) {
		const EventNode *ev = o->events[ev_id];
		const SAU_ProgramEvent *prg_ev = ev->prg_e;
		const SAU_ProgramVoData *prg_vd = prg_ev->vo_data;
		fprintf(stdout,
			"\\%d \tEV %zd \t(VO %hd)",
			prg_ev->wait_ms, ev_id, prg_ev->vo_id);
		if (prg_vd != NULL) {
			SAU_ProgramEvent_print_voice(prg_ev);
			if (prg_vd->params & SAU_PVOP_GRAPH)
				print_graph(ev->graph, ev->graph_count);
		}
		SAU_ProgramEvent_print_operators(prg_ev);
		putc('\n', stdout);
	}
}
