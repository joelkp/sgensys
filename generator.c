#include "sgensys.h"
#include "osc.h"
#include "env.h"
#include "program.h"
#include <stdio.h>
#include <stdlib.h>

enum {
  SGS_FLAG_INIT = 1<<0,
  SGS_FLAG_EXEC = 1<<1
};

typedef struct ParameterValit {
  uint time, pos;
  float goal;
  uchar type;
} ParameterValit;

typedef struct OperatorNode {
  uint time, silence;
  const SGSProgramGraphAdjcs *adjcs;
  uchar type, attr;
  float freq, dynfreq;
  SGSOscLuv *osctype;
  SGSOsc osc;
  float amp, dynamp;
  ParameterValit valitamp, valitfreq;
} OperatorNode;

typedef struct VoiceNode {
  int pos; /* negative for wait time */
  uchar flag, attr;
  const SGSProgramGraph *graph;
  float panning;
  ParameterValit valitpanning;
} VoiceNode;

typedef union Data {
  int i;
  float f;
  void *v;
} Data;

typedef struct EventNode {
  void *node;
  uint waittime;
} EventNode;

typedef struct SetNode {
  int voiceid, operatorid;
  uint params;
  Data data[1]; /* sized for number of parameters set */
} SetNode;

static uint count_flags(uint flags) {
  uint i, count = 0;
  for (i = 0; i < (8 * sizeof(uint)); ++i) {
    if (flags & 1) ++count;
    flags >>= 1;
  }
  return count;
}

typedef union BufData {
  int i;
  float f;
} BufData;

#define BUF_LEN 256
typedef BufData Buf[BUF_LEN];

#define NO_DELAY_OFFS (0x80000000)
struct SGSGenerator {
  uint srate;
  Buf *bufs;
  uint bufc;
  double osc_coeff;
  uint event, eventc;
  uint eventpos;
  EventNode *events;
  uint voice, voicec;
  VoiceNode *voices;
  OperatorNode operators[1]; /* sized to total number of nodes */
  /* actual nodes of varying type stored here */
};

static void upsize_bufs(SGSGenerator *o) {//, OperatorNode *n) {
  uint count = 3;//calc_bufs(n, 0);
  if (count > o->bufc) {
    o->bufs = realloc(o->bufs, sizeof(Buf) * count);
    o->bufc = count;
  }
}

SGSGenerator* SGSGenerator_create(uint srate, struct SGSProgram *prg) {
  SGSGenerator *o;
  const SGSProgramEvent *step;
  void *data;
  uint i, indexwaittime;
  uint size, eventssize, voicessize, operatorssize, setssize;
  size = sizeof(SGSGenerator) - sizeof(OperatorNode);
  eventssize = sizeof(EventNode) * prg->eventc;
  voicessize = sizeof(VoiceNode) * prg->voicec;
  operatorssize = sizeof(OperatorNode) * prg->operatorc;
  setssize = 0;
  for (i = 0; i < prg->eventc; ++i) {
    step = &prg->events[i];
    setssize += sizeof(SetNode) +
                (sizeof(Data) *
                 (count_flags(step->params) +
                  count_flags(step->params & (SGS_VALITFREQ |
                                              SGS_VALITAMP |
                                              SGS_VALITPANNING))*2 - 1));
  }
  o = calloc(1, size + operatorssize + eventssize + voicessize + setssize);
  o->srate = srate;
  o->osc_coeff = SGSOsc_COEFF(srate);
  o->event = 0;
  o->eventc = prg->eventc;
  o->eventpos = 0;
  o->events = (void*)(((uchar*)o) + size + operatorssize);
  o->voice = 0;
  o->voicec = prg->voicec;
  o->voices = (void*)(((uchar*)o) + size + operatorssize + eventssize);
  data      = (void*)(((uchar*)o) + size + operatorssize + eventssize + voicessize);
  SGSOsc_init();
  indexwaittime = 0;
  for (i = 0; i < prg->eventc; ++i) {
    EventNode *e;
    SetNode *s;
    Data *set;
    step = &prg->events[i];
    e = &o->events[i];
    s = data;
    set = s->data;
    e->node = s;
    e->waittime = ((float)step->wait_ms) * srate * .001f;
    indexwaittime += e->waittime;
    s->params = step->params;
    if (step->voice) {
      const SGSProgramVoiceData *vd = step->voice;
      s->voiceid = step->voiceid;
      if (s->params & SGS_GRAPH)
        (*set++).v = (void*)vd->graph;
      if (s->params & SGS_VOATTR)
        (*set++).i = vd->attr;
      if (s->params & SGS_PANNING)
        (*set++).f = vd->panning;
      if (s->params & SGS_VALITPANNING) {
        (*set++).i = ((float)vd->valitpanning.time_ms) * srate * .001f;
        (*set++).f = vd->valitpanning.goal;
        (*set++).i = vd->valitpanning.type;
      }
      o->voices[s->voiceid].pos = -indexwaittime;
      indexwaittime = 0;
    } else {
      s->voiceid = -1;
    }
    if (step->operator) {
      const SGSProgramOperatorData *od = step->operator;
      s->voiceid = step->voiceid;
      s->operatorid = od->operatorid;
      if (s->params & SGS_ADJCS)
        (*set++).v = (void*)od->adjcs;
      if (s->params & SGS_OPATTR)
        (*set++).i = od->attr;
      if (s->params & SGS_WAVE)
        (*set++).i = od->wave;
      if (s->params & SGS_TIME)
        (*set++).i = ((float)od->time_ms) * srate * .001f;
      if (s->params & SGS_SILENCE)
        (*set++).i = ((float)od->silence_ms) * srate * .001f;
      if (s->params & SGS_FREQ)
        (*set++).f = od->freq;
      if (s->params & SGS_VALITFREQ) {
        (*set++).i = ((float)od->valitfreq.time_ms) * srate * .001f;
        (*set++).f = od->valitfreq.goal;
        (*set++).i = od->valitfreq.type;
      }
      if (s->params & SGS_DYNFREQ)
        (*set++).f = od->dynfreq;
      if (s->params & SGS_PHASE)
        (*set++).i = SGSOsc_PHASE(od->phase);
      if (s->params & SGS_AMP)
        (*set++).f = od->amp;
      if (s->params & SGS_VALITAMP) {
        (*set++).i = ((float)od->valitamp.time_ms) * srate * .001f;
        (*set++).f = od->valitamp.goal;
        (*set++).i = od->valitamp.type;
      }
      if (s->params & SGS_DYNAMP)
        (*set++).f = od->dynamp;
    } else {
      s->operatorid = -1;
    }
    data = (void*)(((uchar*)data) +
                   (sizeof(SetNode) - sizeof(Data)) +
                   (((uchar*)set) - ((uchar*)s->data)));
  }
  return o;
}

static void SGSGenerator_handle_event(SGSGenerator *o, EventNode *e) {
  if (1) {
    const SetNode *s = e->node;
    VoiceNode *vn;
    OperatorNode *on;
    const Data *data = s->data;
    /* set state */
    if (s->voiceid >= 0) {
      vn = &o->voices[s->voiceid];
      if (s->params & SGS_GRAPH)
        vn->graph = (*data++).v;
      if (s->params & SGS_VOATTR) {
        uchar attr = (uchar)(*data++).i;
        vn->attr = attr;
      }
      if (s->params & SGS_PANNING)
        vn->panning = (*data++).f;
      if (s->params & SGS_VALITPANNING) {
        vn->valitpanning.time = (*data++).i;
        vn->valitpanning.pos = 0;
        vn->valitpanning.goal = (*data++).f;
        vn->valitpanning.type = (*data++).i;
      }
      upsize_bufs(o);//, topn);
      vn->flag |= SGS_FLAG_INIT | SGS_FLAG_EXEC;
      vn->pos = 0;
      if ((int)o->voice > s->voiceid) /* go back to re-activated node */
        o->voice = s->voiceid;
    }
    if (s->operatorid >= 0) {
      on = &o->operators[s->operatorid];
      if (s->params & SGS_ADJCS)
        on->adjcs = (*data++).v;
      if (s->params & SGS_OPATTR) {
        uchar attr = (uchar)(*data++).i;
        if (!(s->params & SGS_FREQ)) {
          /* May change during processing; preserve state of FREQRATIO flag */
          attr &= ~SGS_ATTR_FREQRATIO;
          attr |= on->attr & SGS_ATTR_FREQRATIO;
        }
        on->attr = attr;
      }
      if (s->params & SGS_WAVE) switch ((*data++).i) {
      case SGS_WAVE_SIN:
        on->osctype = SGSOsc_sin;
        break;
      case SGS_WAVE_SRS:
        on->osctype = SGSOsc_srs;
        break;
      case SGS_WAVE_TRI:
        on->osctype = SGSOsc_tri;
        break;
      case SGS_WAVE_SQR:
        on->osctype = SGSOsc_sqr;
        break;
      case SGS_WAVE_SAW:
        on->osctype = SGSOsc_saw;
        break;
      }
      if (s->params & SGS_TIME)
        on->time = (*data++).i;
      if (s->params & SGS_SILENCE)
        on->silence = (*data++).i;
      if (s->params & SGS_FREQ)
        on->freq = (*data++).f;
      if (s->params & SGS_VALITFREQ) {
        on->valitfreq.time = (*data++).i;
        on->valitfreq.pos = 0;
        on->valitfreq.goal = (*data++).f;
        on->valitfreq.type = (*data++).i;
      }
      if (s->params & SGS_DYNFREQ)
        on->dynfreq = (*data++).f;
      if (s->params & SGS_PHASE)
        SGSOsc_SET_PHASE(&on->osc, (uint)(*data++).i);
      if (s->params & SGS_AMP)
        on->amp = (*data++).f;
      if (s->params & SGS_VALITAMP) {
        on->valitamp.time = (*data++).i;
        on->valitamp.pos = 0;
        on->valitamp.goal = (*data++).f;
        on->valitamp.type = (*data++).i;
      }
      if (s->params & SGS_DYNAMP)
        on->dynamp = (*data++).f;
    }
  }
}

void SGSGenerator_destroy(SGSGenerator *o) {
  free(o->bufs);
  free(o);
}

/*
 * node block processing
 */

static uchar run_param(BufData *buf, uint buflen, ParameterValit *vi,
                       float *state, const BufData *modbuf) {
  uint i, end, len, filllen;
  double coeff;
  float s0 = *state;
  if (!vi) {
    filllen = buflen;
    goto FILL;
  }
  coeff = 1.f / vi->time;
  len = vi->time - vi->pos;
  if (len > buflen) {
    len = buflen;
    filllen = 0;
  } else {
    filllen = buflen - len;
  }
  switch (vi->type) {
  case SGS_VALIT_LIN:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      (*buf++).f = s0 + (vi->goal - s0) * (i * coeff);
    }
    break;
  case SGS_VALIT_EXP:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = 1.f - i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = vi->goal + (s0 - vi->goal) * mod;
    }
    break;
  case SGS_VALIT_LOG:
    for (i = vi->pos, end = i + len; i < end; ++i) {
      double mod = i * coeff,
             modp2 = mod * mod,
             modp3 = modp2 * mod;
      mod = modp3 + (modp2 * modp3 - modp2) *
                    (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
      (*buf++).f = s0 + (vi->goal - s0) * mod;
    }
    break;
  }
  if (modbuf) {
    buf -= len;
    for (i = 0; i < len; ++i) {
      (*buf++).f *= (*modbuf++).f;
    }
  }
  vi->pos += len;
  if (vi->time == vi->pos) {
    s0 = *state = vi->goal;
  FILL:
    if (modbuf) {
      for (i = 0; i < filllen; ++i)
        buf[i].f = s0 * modbuf[i].f;
    } else for (i = 0; i < filllen; ++i)
      buf[i].f = s0;
    return (vi != 0);
  }
  return 0;
}

static void run_block(SGSGenerator *o, Buf *bufs, uint buflen,
                      OperatorNode *n, BufData *parentfreq,
                      uchar waveenv, uchar acc) {
  uint i, len;
  BufData *sbuf, *freq, *freqmod, *pm, *amp;
  Buf *nextbuf = bufs + 1;
  ParameterValit *vi;
  uchar fmodc, pmodc, amodc;
  fmodc = pmodc = amodc = 0;
  if (n->adjcs) {
    fmodc = n->adjcs->fmodc;
    pmodc = n->adjcs->pmodc;
    amodc = n->adjcs->amodc;
  }
  sbuf = *bufs;
  len = buflen;
  if (n->silence) {
    uint zerolen = n->silence;
    if (zerolen > len)
      zerolen = len;
    if (!acc) for (i = 0; i < zerolen; ++i)
      sbuf[i].i = 0;
    len -= zerolen;
    n->time -= zerolen;
    n->silence -= zerolen;
    if (!len)
      return;
    sbuf += zerolen;
  }
  freq = *(nextbuf++);
  if (n->attr & SGS_ATTR_VALITFREQ) {
    vi = &n->valitfreq;
    if (n->attr & SGS_ATTR_VALITFREQRATIO) {
      freqmod = parentfreq;
      if (!(n->attr & SGS_ATTR_FREQRATIO)) {
        n->attr |= SGS_ATTR_FREQRATIO;
        n->freq /= parentfreq[0].f;
      }
    } else {
      freqmod = 0;
      if (n->attr & SGS_ATTR_FREQRATIO) {
        n->attr &= ~SGS_ATTR_FREQRATIO;
        n->freq *= parentfreq[0].f;
      }
    }
  } else {
    vi = 0;
    freqmod = (n->attr & SGS_ATTR_FREQRATIO) ? parentfreq : 0;
  }
  if (run_param(freq, len, vi, &n->freq, freqmod))
    n->attr &= ~(SGS_ATTR_VALITFREQ|SGS_ATTR_VALITFREQRATIO);
  if (fmodc) {
    const int *fmods = &n->adjcs->adjcs[pmodc];
    BufData *fmbuf;
    for (i = 0; i < fmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[fmods[i]], freq, 1, i);
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
  if (pmodc) {
    const int *pmods = n->adjcs->adjcs;
    for (i = 0; i < pmodc; ++i)
      run_block(o, nextbuf, len, &o->operators[pmods[i]], freq, 0, i);
    pm = *(nextbuf++);
  }
  if (!waveenv) {
    if (amodc) {
      const int *amods = &n->adjcs->adjcs[pmodc+fmodc];
      float dynampdiff = n->dynamp - n->amp;
      for (i = 0; i < amodc; ++i)
        run_block(o, nextbuf, len, &o->operators[amods[i]], freq, 1, i);
      amp = *(nextbuf++);
      for (i = 0; i < len; ++i)
        amp[i].f = n->amp + amp[i].f * dynampdiff;
    } else {
      amp = *(nextbuf++);
      vi = (n->attr & SGS_ATTR_VALITAMP) ? &n->valitamp : 0;
      if (run_param(amp, len, vi, &n->amp, 0))
        n->attr &= ~SGS_ATTR_VALITAMP;
    }
    for (i = 0; i < len; ++i) {
      int s, spm = 0;
      float sfreq = freq[i].f, samp = amp[i].f;
      if (pm)
        spm = pm[i].i;
      SGSOsc_RUN_PM(&n->osc, n->osctype, o->osc_coeff, sfreq, spm, samp, s);
      if (acc)
        s += sbuf[i].i;
      sbuf[i].i = s;
    }
  } else {
    for (i = 0; i < len; ++i) {
      float s, sfreq = freq[i].f;
      int spm = 0;
      if (pm)
        spm = pm[i].i;
      SGSOsc_RUN_PM_ENVO(&n->osc, n->osctype, o->osc_coeff, sfreq, spm, s);
      if (acc)
        s *= sbuf[i].f;
      sbuf[i].f = s;
    }
  }
  n->time -= len;
}

static void run_voice(SGSGenerator *o, VoiceNode *vn, short *out, uint len) {
  const int *ops;
  uint i, opc, time = 0;
  uchar finished = 1;
  short *sp;
  if (!vn->graph)
    goto RETURN;
  opc = vn->graph->opc;
  ops = vn->graph->ops;
  time = len;
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    int t = n->time;
    if (t == 0)
      continue;
    if (time > t)
      time = t;
  }
  for (i = 0; i < opc; ++i) {
    OperatorNode *n = &o->operators[ops[i]];
    int t = time;
    if (n->time == 0)
      continue;
    sp = out;
    if (n->silence) {
      if (n->silence >= t) {
        n->time -= t;
        n->silence -= t;
        goto NEXT;
      }
      sp += n->silence + n->silence; /* doubled given stereo interleaving */
      t -= n->silence;
      n->time -= n->silence;
      n->silence = 0;
    }
    while (t) {
      len = BUF_LEN;
      if (len > t)
        len = t;
      t -= len;
      run_block(o, o->bufs, len, n, 0, 0, 0);
      if (n->attr & SGS_ATTR_VALITPANNING) {
        BufData *buf = o->bufs[1];
        if (run_param(buf, len, &vn->valitpanning, &vn->panning, 0))
          n->attr &= ~SGS_ATTR_VALITPANNING;
        for (i = 0; i < len; ++i) {
          int s = (*o->bufs)[i].i, p;
          SET_I2F(p, ((float)s) * buf[i].f);
          *sp++ += s - p;
          *sp++ += p;
        }
      } else for (i = 0; i < len; ++i) {
        int s = (*o->bufs)[i].i, p;
        SET_I2F(p, ((float)s) * vn->panning);
        *sp++ += s - p;
        *sp++ += p;
      }
    }
  NEXT:
    if (n->time != 0)
      finished = 0;
  }
  vn->pos += time;
RETURN:
  if (finished)
    vn->flag &= ~SGS_FLAG_EXEC;
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
  while (o->event < o->eventc) {
    EventNode *e = &o->events[o->event];
    if (o->eventpos < e->waittime) {
      uint waittime = e->waittime - o->eventpos;
      if (waittime < len) {
        /* Split processing so that len is no longer than waittime, ensuring
         * event is handled before its operator is used.
         */
        skiplen = len - waittime;
        len = waittime;
      }
      o->eventpos += len;
      break;
    }
    SGSGenerator_handle_event(o, e);
    ++o->event;
    o->eventpos = 0;
  }
  for (i = o->voice; i < o->voicec; ++i) {
    VoiceNode *vn = &o->voices[i];
    if (vn->pos < 0) {
      uint waittime = -vn->pos;
      if (waittime >= len) {
        vn->pos += len;
        break; /* end for now; waittimes accumulate across nodes */
      }
      buf += waittime+waittime; /* doubled given stereo interleaving */
      len -= waittime;
      vn->pos = 0;
    }
    if (vn->flag & SGS_FLAG_EXEC)
      run_voice(o, vn, buf, len);
  }
  if (skiplen) {
    buf += len+len; /* doubled given stereo interleaving */
    len = skiplen;
    goto PROCESS;
  }
  for(;;) {
    VoiceNode *vn;
    if (o->voice == o->voicec)
      return (o->event != o->eventc);
    vn = &o->voices[o->voice];
    if (!(vn->flag & SGS_FLAG_INIT) || vn->flag & SGS_FLAG_EXEC)
      break;
    ++o->voice;
  }
  return 1;
}
