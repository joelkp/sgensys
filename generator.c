#include "mgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct MGSGeneratorNode {
  int pos;
  uint time;
  uchar type, flag, mode;
  uchar amodc, fmodc, pmodc;
  ushort nodesize;
  short *osctype;
  MGSOsc osc;
  float freq, dynfreq, amp, dynampdiff;
  struct MGSGeneratorNode *ref;
  struct MGSGeneratorNode *mods[1]; /* sized to (amodc+fmodc+pmodc) */
} MGSGeneratorNode;

#define MGSGeneratorNode_NEXT(o) \
  ((MGSGeneratorNode*)(((uchar*)(o)) + (o)->nodesize))

#define NO_DELAY_OFFS (0x80000000)
struct MGSGenerator {
  const struct MGSProgram *program;
  uint srate;
  double osc_coeff;
  int delay_offs;
  MGSGeneratorNode *node, *end,
                   *nodes; /* node size varies, so no direct indexing! */
};

MGSGenerator* MGSGenerator_create(uint srate, struct MGSProgram *prg) {
  MGSGenerator *o;
  MGSProgramNode *step;
  MGSGeneratorNode *n, *ref;
  uint i, j, size, nodessize;
  size = sizeof(MGSGenerator);
  nodessize = sizeof(MGSGeneratorNode) * prg->stepc;
  for (step = prg->steps; step; step = step->next) {
    nodessize += (step->amodc + step->fmodc + step->pmodc - 1) *
                 sizeof(MGSGeneratorNode*);
  }
  o = calloc(1, size + nodessize);
  o->program = prg;
  o->srate = srate;
  o->osc_coeff = MGSOsc_COEFF(srate);
  o->node = o->nodes = (void*)(((uchar*)o) + size);
  o->end = (void*)(((uchar*)o->node) + nodessize);
  MGSOsc_init();
  step = prg->steps;
  for (n = o->nodes; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    uint delay = step->delay * srate;
    uint time = step->time * srate;
    MGSGeneratorNode **mods;
    if (!step->ref) {
      n->ref = 0;
    } else {
      ref = o->nodes;
      for (i = step->ref->id; i; --i)
        ref = MGSGeneratorNode_NEXT(ref);
      n->ref = ref;
    }
    n->pos = -delay;
    n->time = time;
    switch (step->wave) {
    case MGS_WAVE_SIN:
      n->osctype = MGSOsc_sin;
      break;
    case MGS_WAVE_SQR:
      n->osctype = MGSOsc_sqr;
      break;
    case MGS_WAVE_TRI:
      n->osctype = MGSOsc_tri;
      break;
    case MGS_WAVE_SAW:
      n->osctype = MGSOsc_saw;
      break;
    }
    n->type = step->type;
    n->flag = step->flag;
    n->mode = step->mode;
    n->amp = step->amp;
    n->dynampdiff = step->dynamp - step->amp;
    n->freq = step->freq;
    n->dynfreq = step->dynfreq;
    n->amodc = step->amodc;
    n->fmodc = step->fmodc;
    n->pmodc = step->pmodc;
    n->nodesize = sizeof(MGSGeneratorNode) +
                  ((n->amodc + n->fmodc + n->pmodc - 1) *
                   sizeof(MGSGeneratorNode*));
    /* mods init part one - replaced with proper entries below */
    mods = n->mods;
    for (i = 0; i < n->amodc; ++i)
      *mods++ = (void*)step->amods[i];
    for (i = 0; i < n->fmodc; ++i)
      *mods++ = (void*)step->fmods[i];
    for (i = 0; i < n->pmodc; ++i)
      *mods++ = (void*)step->pmods[i];
    step = step->next;
  }
  for (n = o->nodes, j = 0; n != o->end; n = MGSGeneratorNode_NEXT(n), ++j) {
    uint modc = n->amodc + n->fmodc + n->pmodc;
    for (i = 0; i < modc; ++i) {
      uint id = 0;
      ref = o->nodes;
      step = (void*)n->mods[i];
      while (id < step->id) {
        ref = MGSGeneratorNode_NEXT(ref);
        ++id;
      }
      n->mods[i] = ref; /* now given proper entry */
    }
  }
  return o;
}

static void MGSGenerator_enter_node(MGSGenerator *o, MGSGeneratorNode *n) {
  int pos_offs;
  switch (n->type) {
  case MGS_TYPE_WAVE:
    if (!n->ref) { /* beginning */
      MGSOsc_SET_PHASE(&n->osc, 0);
    } else { /* continuation */
      MGSGeneratorNode *ref = n->ref;
      if (n->flag & MGS_FLAG_REFTIME) {
        n->time = ref->time;
        n->pos = ref->pos;
      }
      ref->pos = ref->time;
      MGSOsc_SET_PHASE(&n->osc, MGSOsc_PHASE(&ref->osc));
    }
    /* click reduction: increase time to make it end at wave cycle's end */
    MGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->freq, n->time, pos_offs);
    n->time -= pos_offs;
    if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
      o->delay_offs = pos_offs;
    break;
  case MGS_TYPE_ENV:
    break;
  }
  n->flag |= MGS_FLAG_ENTERED;
}

void MGSGenerator_destroy(MGSGenerator *o) {
  free(o);
}

/*
 * node sample processing
 */

static float run_waveenv_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff);

static uint run_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff) {
  uint i;
  int s;
  float freq = n->freq;
  float amp = n->amp;
  int pm = 0;
  MGSGeneratorNode **mods = n->mods;
  if (n->flag & MGS_FLAG_FREQRATIO)
    freq *= freq_mult;
  if (n->amodc) {
    float am = n->dynampdiff;
    i = 0;
    do {
      am *= run_waveenv_sample(*mods++, freq, osc_coeff);
    } while (++i < n->amodc);
    amp += am;
  }
  if (n->fmodc) {
    float fm = n->dynfreq;
    if (n->flag & MGS_FLAG_DYNFREQRATIO)
      fm *= freq_mult;
    fm -= freq;
    i = 0;
    do {
      fm *= run_waveenv_sample(*mods++, freq, osc_coeff);
    } while (++i < n->fmodc);
    freq += fm;
  }
  for (i = 0; i < n->pmodc; ++i) {
    pm += run_sample(*mods++, freq, osc_coeff);
  }
  MGSOsc_RUN_PM(&n->osc, n->osctype, osc_coeff, freq, pm, amp, s);
  return s;
}

static float run_waveenv_sample(MGSGeneratorNode *n, float freq_mult, double osc_coeff) {
  uint i;
  float s = 1.f;
  float freq = n->freq;
  int pm = 0;
  MGSGeneratorNode **mods = n->mods + n->amodc;
  if (n->flag & MGS_FLAG_FREQRATIO)
    freq *= freq_mult;
  if (n->fmodc) {
    float fm = n->dynfreq;
    if (n->flag & MGS_FLAG_DYNFREQRATIO)
      fm *= freq_mult;
    fm -= freq;
    i = 0;
    do {
      fm *= run_waveenv_sample(*mods++, freq, osc_coeff);
    } while (++i < n->fmodc);
    freq += fm;
  }
  for (i = 0; i < n->pmodc; ++i) {
    pm += run_sample(*mods++, freq, osc_coeff);
  }
  MGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, osc_coeff, freq, pm, s);
  return s;
}

/*
 * node block processing
 */

static void run_node(MGSGeneratorNode *n, short *sp, uint len, double osc_coeff) {
  uint time = n->time - n->pos;
  if (time > len)
    time = len;
  n->pos += time;
  if (n->type != MGS_TYPE_WAVE) return;
  if (n->mode == MGS_MODE_RIGHT) ++sp;
  for (; time; --time, sp += 2) {
    int s;
    s = run_sample(n, /* dummy value */0.f, osc_coeff);
    sp[0] += s;
    if (n->mode == MGS_MODE_CENTER)
      sp[1] += s;
  }
  if ((uint)n->pos == n->time)
    n->flag &= ~MGS_FLAG_PLAY;
}

/*
 * main run-function
 */

uchar MGSGenerator_run(MGSGenerator *o, short *buf, uint len) {
  MGSGeneratorNode *n;
  short *sp;
  uint i, skiplen;
  sp = buf;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
PROCESS:
  skiplen = 0;
  for (n = o->node; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    if (n->pos < 0) {
      uint delay = -n->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS)
        delay -= o->delay_offs; /* delay inc == previous time inc */
      if (delay <= len) {
        /* Split processing so that len is no longer than delay, avoiding
         * cases where the node prior to a node disabling it plays too
         * long.
         */
        skiplen = len - delay;
        len = delay;
      }
      break;
    }
    if (!(n->flag & MGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      MGSGenerator_enter_node(o, n);
  }
  for (n = o->node; n != o->end; n = MGSGeneratorNode_NEXT(n)) {
    if (n->pos < 0) {
      uint delay = -n->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        n->pos += o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (delay >= len) {
        n->pos += len;
        break; /* end for now; delays accumulate across steps */
      }
      buf += delay+delay; /* doubled due to stereo interleaving */
      len -= delay;
      n->pos = 0;
    } else
    if (!(n->flag & MGS_FLAG_ENTERED))
      MGSGenerator_enter_node(o, n);
    if (n->flag & MGS_FLAG_PLAY)
      run_node(n, buf, len, o->osc_coeff);
  }
  if (skiplen) {
    buf += len+len; /* doubled due to stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->end)
      return 0;
    if (o->node->flag & MGS_FLAG_PLAY)
      break;
    o->node = MGSGeneratorNode_NEXT(o->node);
  }
  return 1;
}
