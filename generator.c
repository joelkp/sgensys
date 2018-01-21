#include "sgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct IndexNode {
  void *node;
  int pos; /* negative for delay/time shift */
  uchar type, flag;
} IndexNode;

typedef struct OperatorNode {
  uint time, silence;
  uchar type, attr;
  float freq, dynfreq;
  struct OperatorNode *fmodchain;
  struct OperatorNode *pmodchain;
  short *osctype;
  SGSOsc osc;
  float amp, dynamp;
  struct OperatorNode *amodchain;
  struct OperatorNode *link;
  union {
    float panning;
    uint parentid;
  } spec;
} OperatorNode;

typedef union Data {
  int i;
  float f;
} Data;

typedef struct SetNode {
  uint nodeid;
  ushort values;
  uchar mods;
  Data data[1]; /* sized for number of things set */
} SetNode;

static uint count_flags(uint flags) {
  uint i, count = 0;
  for (i = 0; i < (8 * sizeof(uint)); ++i) {
    if (flags & 1) ++count;
    flags >>= 1;
  }
  return count;
}

typedef struct EventNode {
  void *node;
  uint waittime;
  uchar type;
} EventNode;

#if 0
struct EventNode;

typedef struct EventIndexNode {
  uint pos, wait;
  struct EventNode *event;
  uchar type;
} EventIndexNode;

static struct Event *get_event(OperatorNode *n, uchar type) {
  uint i, eventc;
  EventNode *events = n->events;
  if (!events)
    return 0;
  eventc = n->eventc;
  for (i = 0; i < eventc; ++i) {
    EventNode *en = events[i];
    if (en->wait > en->pos)
      return 0;
    if (en->type == type)
      return en->event;
  }
  return 0;
}
#endif

#define BUF_LEN 256
typedef Data Buf[BUF_LEN];

#define NO_DELAY_OFFS (0x80000000)
struct SGSGenerator {
  uint srate;
  Buf *bufs;
  uint bufc;
  double osc_coeff;
  int delay_offs;
  uint event, eventc;
  EventNode *events;
  uint node, nodec;
  IndexNode nodes[1]; /* sized to number of nodes */
  /* actual nodes of varying type stored here */
};

static int calc_bufs_waveenv(OperatorNode *n);

static int calc_bufs(OperatorNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_waveenv(n->fmodchain);
  ++count, --i;
  if (n->amodchain) {j = calc_bufs_waveenv(n->amodchain); if (i < j) i = j;}
  if (n->pmodchain) {j = calc_bufs(n->pmodchain); if (i < j) i = j;}
  if (!n->link) return (i > 0 ? count + i : count);
  n = n->link;
  ++count, --i; /* need separate accumulating buf */
  goto BEGIN;
}

static int calc_bufs_waveenv(OperatorNode *n) {
  int count = 1, i = 0, j;
BEGIN:
  ++count;
  if (n->fmodchain) i = calc_bufs_waveenv(n->fmodchain);
  if (n->pmodchain) {j = calc_bufs(n->pmodchain); if (i < j) i = j;}
  if (!n->link) return (i > 0 ? count + i : count);
  n = n->link;
  ++count, --i; /* need separate multiplying buf */
  goto BEGIN;
}

static void upsize_bufs(SGSGenerator *o, OperatorNode *n) {
  uint count = calc_bufs(n);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

SGSGenerator* SGSGenerator_create(uint srate, struct SGSProgram *prg) {
  SGSGenerator *o;
  SGSProgramNode *step;
  void *data;
  uint i, node, nodec, event, indexdelay;
  uint size, indexsize, nodessize, eventssize, eventc;
  size = sizeof(SGSGenerator) - sizeof(IndexNode);
  indexsize = nodessize = eventssize = 0;
  nodec = eventc = 0;
  for (step = prg->nodelist; step; step = step->next) {
    if (step->type & SGS_TYPE_SET) {
      if (step->spec.set.values & SGS_TIME &&
          step->time != 0.f) /* new index node takes over at entry */
        indexsize += sizeof(IndexNode);
      eventssize += sizeof(EventNode);
      ++eventc;
      nodessize += sizeof(SetNode) +
                   (sizeof(Data) *
                    (count_flags((step->spec.set.values << 8) |
                                 step->spec.set.mods) - 1));
      
    } else {
      indexsize += sizeof(IndexNode);
      nodessize += sizeof(OperatorNode);
    }
  }
  o = calloc(1, size + indexsize + nodessize + eventssize);
  o->srate = srate;
  o->osc_coeff = SGSOsc_COEFF(srate);
  o->node = 0;
  o->nodec = prg->topc; /* only loop top-level nodes */
  o->event = 0;
  o->eventc = eventc;
  o->events = (void*)(((uchar*)data) + size + indexsize);
  data = (void*)(((uchar*)o->events) + eventssize);
  SGSOsc_init();
  step = prg->nodelist;
  for (i = node = event = 0; node < prg->nodec; ++node) {
    if (step->type == SGS_TYPE_TOP ||
        step->type == SGS_TYPE_NESTED) {
      OperatorNode *n = data;
      IndexNode *in = &o->nodes[nodec++];
      uint delay = step->delay * srate;
      in->node = data;
      in->pos = -delay;
      in->type = step->type;
      in->flag = step->flag;
      indexdelay = 0; /* used for set nodes following */
      n->time = step->time * srate;
      n->silence = step->silence * srate;
      switch (step->wave) {
      case SGS_WAVE_SIN:
        n->osctype = SGSOsc_sin;
        break;
      case SGS_WAVE_SQR:
        n->osctype = SGSOsc_sqr;
        break;
      case SGS_WAVE_TRI:
        n->osctype = SGSOsc_tri;
        break;
      case SGS_WAVE_SAW:
        n->osctype = SGSOsc_saw;
        break;
      }
      n->attr = step->attr;
      n->freq = step->freq;
      n->dynfreq = step->dynfreq;
      n->amp = step->amp;
      n->dynamp = step->dynamp;
      SGSOsc_SET_PHASE(&n->osc, SGSOsc_PHASE(step->phase));
      if (step->type == SGS_TYPE_TOP)
        n->spec.panning = step->panning;
      /* mods init part one - replaced with proper entries next loop */
      n->amodchain = (void*)step->amod.chain;
      n->fmodchain = (void*)step->fmod.chain;
      n->pmodchain = (void*)step->pmod.chain;
      n->link = (void*)step->spec.nested.link;
      data = (void*)(((uchar*)data) + sizeof(OperatorNode));
    } else if (step->type & SGS_TYPE_SET) {
      SetNode *n = data;
      EventNode *e = &o->events[event++];
      Data *set = n->data;
      SGSProgramNode *ref = step->spec.set.ref;
      n->nodeid = ref->id;
      if (ref->type == SGS_TYPE_NESTED)
        n->nodeid += prg->topc;
      n->values = step->spec.set.values;
      n->mods = step->spec.set.mods;
      e->node = n;
      e->waittime = step->delay * srate;
      e->type = step->type;
      indexdelay += e->waittime;
      if (ref->type == SGS_TYPE_TOP &&
          n->values & SGS_TIME &&
          step->time != 0.f) { /* insert indexnode for takeover upon event */
        IndexNode *in = &o->nodes[i++];
        in->node = ref;
        in->pos = -indexdelay;
        in->type = ref->type;
        in->flag = SGS_FLAG_EXEC;
        indexdelay = 0; /* used for set nodes following */
      }
      if (n->values & SGS_TIME)
        (*set++).i = step->time * srate;
      if (n->values & SGS_SILENCE)
        (*set++).i = step->silence * srate;
      if (n->values & SGS_FREQ)
        (*set++).f = step->freq;
      if (n->values & SGS_DYNFREQ)
        (*set++).f = step->dynfreq;
      if (n->values & SGS_PHASE)
        (*set++).i = SGSOsc_PHASE(step->phase);
      if (n->values & SGS_AMP)
        (*set++).f = step->amp;
      if (n->values & SGS_DYNAMP)
        (*set++).f = step->dynamp;
      if (n->values & SGS_PANNING)
        (*set++).i = step->panning;
      if (n->values & SGS_ATTR)
        (*set++).i = step->attr;
      if (n->mods & SGS_AMODS)
        (*set++).i = step->amod.chain->id + prg->topc;
      if (n->mods & SGS_FMODS)
        (*set++).i = step->fmod.chain->id + prg->topc;
      if (n->mods & SGS_PMODS)
        (*set++).i = step->pmod.chain->id + prg->topc;
      data = (void*)(((uchar*)data) +
                     sizeof(SetNode) +
                     (sizeof(Data) *
                      (count_flags((step->spec.set.values << 16) |
                                   step->spec.set.mods) - 1)));
    }
    step = step->next;
  }
  /* mods init part two - give proper entries */
  for (i = 0; i < prg->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->type == SGS_TYPE_TOP ||
        in->type == SGS_TYPE_NESTED) {
      OperatorNode *n = in->node;
      if (n->amodchain) {
        uint id = ((SGSProgramNode*)n->amodchain)->id + prg->topc;
        n->amodchain = o->nodes[id].node;
        n->amodchain->spec.parentid = i;
      }
      if (n->fmodchain) {
        uint id = ((SGSProgramNode*)n->fmodchain)->id + prg->topc;
        n->fmodchain = o->nodes[id].node;
        n->fmodchain->spec.parentid = i;
      }
      if (n->pmodchain) {
        uint id = ((SGSProgramNode*)n->pmodchain)->id + prg->topc;
        n->pmodchain = o->nodes[id].node;
        n->pmodchain->spec.parentid = i;
      }
      if (n->link) {
        uint id = ((SGSProgramNode*)n->link)->id + prg->topc;
        n->link = o->nodes[id].node;
        n->link->spec.parentid = i;
      }
    }
  }
  return o;
}

static void adjust_time(SGSGenerator *o, OperatorNode *n) {
  int pos_offs;
  int time = n->time - n->silence;
  /* click reduction: increase time to make it end at wave cycle's end */
  SGSOsc_WAVE_OFFS(&n->osc, o->osc_coeff, n->freq, time, pos_offs);
  n->time -= pos_offs;
  if ((uint)o->delay_offs == NO_DELAY_OFFS || o->delay_offs > pos_offs)
    o->delay_offs = pos_offs;
}

static void SGSGenerator_enter_node(SGSGenerator *o, IndexNode *in) {
  switch (in->type) {
  case SGS_TYPE_TOP:
    upsize_bufs(o, in->node);
    adjust_time(o, in->node);
  case SGS_TYPE_NESTED:
    break;
  case SGS_TYPE_SETTOP:
  case SGS_TYPE_SETNESTED: {
    SetNode *setn = in->node;
    IndexNode *refin = &o->nodes[setn->nodeid];
    OperatorNode *refn = refin->node;
    Data *data = setn->data;
    uchar adjtime = 0;
    /* set state */
    if (setn->values & SGS_TIME) {
      refn->time = (*data++).i;
      refin->pos = 0;
      if (refn->time) {
        if (refin->type == SGS_TYPE_TOP)
          refin->flag |= SGS_FLAG_EXEC;
        adjtime = 1;
      } else
        refin->flag &= ~SGS_FLAG_EXEC;
    }
    if (setn->values & SGS_SILENCE) {
      refn->silence = (*data++).i;
      adjtime = 1;
    }
    if (setn->values & SGS_FREQ) {
      refn->freq = (*data++).f;
      adjtime = 1;
    }
    if (setn->values & SGS_DYNFREQ)
      refn->dynfreq = (*data++).f;
    if (setn->values & SGS_PHASE)
      SGSOsc_SET_PHASE(&refn->osc, (uint)(*data++).i);
    if (setn->values & SGS_AMP)
      refn->amp = (*data++).f;
    if (setn->values & SGS_DYNAMP)
      refn->dynamp = (*data++).f;
    if (setn->values & SGS_PANNING)
      refn->spec.panning = (*data++).f;
    if (setn->values & SGS_ATTR)
      refn->attr = (uchar)(*data++).i;
    if (setn->mods & SGS_AMODS)
      refn->amodchain = o->nodes[(*data++).i].node;
    if (setn->mods & SGS_FMODS)
      refn->fmodchain = o->nodes[(*data++).i].node;
    if (setn->mods & SGS_PMODS)
      refn->pmodchain = o->nodes[(*data++).i].node;
    if (refn->type == SGS_TYPE_TOP) {
      upsize_bufs(o, refn);
      if (adjtime) /* here so new freq also used if set */
        adjust_time(o, refn);
    } else {
      IndexNode *topin = refin;
      while (topin->type == SGS_TYPE_NESTED)
        topin = &o->nodes[((OperatorNode*)topin->node)->spec.parentid];
      upsize_bufs(o, topin->node);
    }
    /* take over place of ref'd node */
    *in = *refin;
    refin->flag &= ~SGS_FLAG_EXEC;
    break; }
  case SGS_TYPE_ENV:
    break;
  }
  in->flag |= SGS_FLAG_ENTERED;
}

void SGSGenerator_destroy(SGSGenerator *o) {
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static void run_block_waveenv(Buf *bufs, uint buflen, OperatorNode *n,
                              Data *parentfreq, double osc_coeff);

static void run_block(Buf *bufs, uint buflen, OperatorNode *n,
                      Data *parentfreq, double osc_coeff) {
  uchar acc = 0;
  uint i, len;
  Data *sbuf, *freq, *amp, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  sbuf = *bufs;
  len = buflen;
  if (n->silence) {
    uint zerolen = n->silence;
    if (zerolen > len)
      zerolen = len;
    if (!acc) for (i = 0; i < zerolen; ++i)
      sbuf[i].i = 0;
    len -= zerolen;
    n->silence -= zerolen;
    if (!len)
      goto NEXT;
    sbuf += zerolen;
  }
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq, osc_coeff);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  if (n->amodchain) {
    float dynampdiff = n->dynamp - n->amp;
    run_block_waveenv(nextbuf, len, n->amodchain, freq, osc_coeff);
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp + amp[i].f * dynampdiff;
  } else {
    amp = *(nextbuf++);
    for (i = 0; i < len; ++i)
      amp[i].f = n->amp;
  }
  pm = 0;
  if (n->pmodchain) {
    run_block(nextbuf, len, n->pmodchain, freq, osc_coeff);
    pm = *(nextbuf++);
  }
  for (i = 0; i < len; ++i) {
    int s, spm = 0;
    float sfreq = freq[i].f, samp = amp[i].f;
    if (pm)
      spm = pm[i].i;
    SGSOsc_RUN_PM(&n->osc, n->osctype, osc_coeff, sfreq, spm, samp, s);
    if (acc)
      s += sbuf[i].i;
    sbuf[i].i = s;
  }
NEXT:
  if (!n->link) return;
  acc = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate accumulating buf */
  goto BEGIN;
}

static void run_block_waveenv(Buf *bufs, uint buflen, OperatorNode *n,
                              Data *parentfreq, double osc_coeff) {
  uchar mul = 0;
  uint i, len;
  Data *sbuf, *freq, *pm;
  Buf *nextbuf = bufs;
BEGIN:
  sbuf = *bufs;
  len = buflen;
  if (n->silence) {
    uint zerolen = n->silence;
    if (zerolen > len)
      zerolen = len;
    if (!mul) for (i = 0; i < zerolen; ++i)
      sbuf[i].i = 0;
    len -= zerolen;
    n->silence -= zerolen;
    if (!len)
      goto NEXT;
    sbuf += zerolen;
  }
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_FREQRATIO) {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq * parentfreq[i].f;
  } else {
    for (i = 0; i < len; ++i)
      freq[i].f = n->freq;
  }
  if (n->fmodchain) {
    Data *fmbuf;
    run_block_waveenv(nextbuf, len, n->fmodchain, freq, osc_coeff);
    fmbuf = *nextbuf;
    if (n->attr & SGS_ATTR_FREQRATIO) {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq * parentfreq[i].f - freq[i].f) * fmbuf[i].f;
    } else {
      for (i = 0; i < len; ++i)
        freq[i].f += (n->dynfreq - freq[i].f) * fmbuf[i].f;
    }
  }
  pm = 0;
  if (n->pmodchain) {
    run_block(nextbuf, len, n->pmodchain, freq, osc_coeff);
    pm = *(nextbuf++);
  }
  for (i = 0; i < len; ++i) {
    float s, sfreq = freq[i].f;
    int spm = 0;
    if (pm)
      spm = pm[i].i;
    SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, osc_coeff, sfreq, spm, s);
    if (mul)
      s *= sbuf[i].f;
    sbuf[i].f = s;
  }
NEXT:
  if (!n->link) return;
  mul = 1;
  n = n->link;
  nextbuf = bufs+1; /* need separate multiplying buf */
  goto BEGIN;
}

static uint run_node(SGSGenerator *o, OperatorNode *n, short *sp, uint pos, uint len) {
  double osc_coeff = o->osc_coeff;
  uint i, ret, time = n->time - pos;
  if (time > len)
    time = len;
  if (n->silence) {
    if (n->silence >= time) {
      n->silence -= time;
      return time;
    }
    sp += n->silence + n->silence; /* doubled given stereo interleaving */
    time -= n->silence;
    n->silence = 0;
  }
  ret = time;
  do {
    len = BUF_LEN;
    if (len > time)
      len = time;
    time -= len;
    run_block(o->bufs, len, n, 0, osc_coeff);
    for (i = 0; i < len; ++i, sp += 2) {
      int s = (*o->bufs)[i].i, p;
      SET_I2F(p, ((float)s) * n->spec.panning);
      sp[0] += s - p;
      sp[1] += p;
    }
  } while (time);
  return ret;
}

/*
 * main run-function
 */

uchar SGSGenerator_run(SGSGenerator *o, short *buf, uint len) {
  short *sp;
  uint i, skiplen;
  sp = buf;
  for (i = len; i--; sp += 2) {
    sp[0] = 0;
    sp[1] = 0;
  }
PROCESS:
  skiplen = 0;
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
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
    if (!(in->flag & SGS_FLAG_ENTERED))
      /* After return to PROCESS, ensures disabling node is initialized before
       * disabled node would otherwise play.
       */
      SGSGenerator_enter_node(o, in);
  }
  for (i = o->node; i < o->nodec; ++i) {
    IndexNode *in = &o->nodes[i];
    if (in->pos < 0) {
      uint delay = -in->pos;
      if ((uint)o->delay_offs != NO_DELAY_OFFS) {
        in->pos += o->delay_offs; /* delay inc == previous time inc */
        o->delay_offs = NO_DELAY_OFFS;
      }
      if (delay >= len) {
        in->pos += len;
        break; /* end for now; delays accumulate across nodes */
      }
      buf += delay+delay; /* doubled given stereo interleaving */
      len -= delay;
      in->pos = 0;
    } else
    if (!(in->flag & SGS_FLAG_ENTERED))
      SGSGenerator_enter_node(o, in);
    if (in->flag & SGS_FLAG_EXEC) {
      OperatorNode *n = in->node;
      in->pos += run_node(o, n, buf, in->pos, len);
      if ((uint)in->pos == n->time)
        in->flag &= ~SGS_FLAG_EXEC;
    }
  }
  if (skiplen) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    if (o->node == o->nodec)
      return 0;
    if (o->nodes[o->node].flag & SGS_FLAG_EXEC)
      break;
    ++o->node;
  }
  return 1;
}
