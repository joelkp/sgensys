#include "sgensys.h"
#include "program.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define IS_WHITESPACE(c) \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

typedef struct SGSParser {
  FILE *f;
  const char *fn;
  SGSProgram *prg;
  SGSSymtab *st;
  uint line;
  uint reclevel;
  /* node state */
  uint level;
  uint setdef, setnode;
  uint nestedopc;
  SGSProgramEvent *last_event;
  SGSProgramEvent *undo_last;
  /* settings/ops */
  float ampmult;
  uint def_time_ms;
  float def_freq, def_ratio;
} SGSParser;

typedef struct NodeTarget {
  int *idtarget;
  uint topopid;
  uchar modtype;
} NodeTarget;

/* things that need to be separate for each nested parse_level() go here */
typedef struct NodeData {
  SGSProgramEvent *event; /* state for tentative event until end_event() */
  SGSProgramEvent *last;
  SGSProgramEvent *oplast; /* last event for last operator */
  NodeTarget *target;
  char *setsym;
  /* timing/delay */
  SGSProgramEvent *composite; /* allows specifying events out of linear order*/
  SGSProgramEvent *group;
  uchar end_composite;
  uchar end_group;
  uchar wait_duration;
  uint next_wait_ms, /* added for next event; adjusted in parser */
       acc_wait_ms; /* accumulates next_wait_ms while composite events made */
  uint add_wait_ms; /* added to event's wait in end_event() */
} NodeData;

static void new_event(SGSParser *o, NodeData *nd, SGSProgramEvent *opevent,
                      uchar composite);
static void end_operator(SGSParser *o, NodeData *nd);
static void end_event(SGSParser *o, NodeData *nd);

static void new_operator(SGSParser *o, NodeData *nd, NodeTarget *target,
                         uchar wave) {
  SGSProgram *p = o->prg;
  SGSProgramEvent *e;
  end_operator(o, nd);
  new_event(o, nd, 0, 0);
  e = nd->event;
  e->wave = wave;
  nd->target = target;
  if (!target) {
    e->optype = SGS_TYPE_TOP;
    e->opid = p->topopc++;
    e->topopid = e->opid;
  } else {
    e->optype = SGS_TYPE_NESTED;
    e->opid = o->nestedopc++;
    e->topopid = target->topopid;
    if (*target->idtarget < 0)
      *target->idtarget = e->opid;
    else {
      nd->oplast->params |= SGS_LINK;
      nd->oplast->linkid = e->opid;
    }
  }
  nd->oplast = e;

  /* Set defaults */
  e->amp = 1.f;
  if (e->optype == SGS_TYPE_TOP) {
    e->time_ms = -1; /* later fitted or set to default */
    e->panning = .5f; /* default - center */
  } else
    e->time_ms = o->def_time_ms;
  e->freq = o->def_freq;
}

static void new_event(SGSParser *o, NodeData *nd, SGSProgramEvent *opevent,
                      uchar composite) {
  SGSProgram *p = o->prg;
  SGSProgramEvent *e, *pe;
  end_event(o, nd);
  e = nd->event = calloc(1, sizeof(SGSProgramEvent));
  pe = e->opprev = opevent;
  e->id = p->eventc++;
  if (pe) {
    e->opid = pe->opid;
    e->topopid = pe->topopid;
    e->optype = pe->optype;
    e->attr = pe->attr;
    e->wave = pe->wave;
    e->freq = pe->freq;
    e->dynfreq = pe->dynfreq;
    e->amp = pe->amp;
    e->dynamp = pe->dynamp;
    e->panning = pe->panning;
    e->pmodid = pe->pmodid;
    e->fmodid = pe->fmodid;
    e->amodid = pe->amodid;
    e->linkid = pe->linkid;
  } else { /* init event - everything set to defaults */
    e->opfirst = 1;
    e->params |= SGS_PMOD |
                 SGS_FMOD |
                 SGS_AMOD |
                 SGS_LINK |
                 SGS_WAVE |
                 SGS_TIME |
                 SGS_SILENCE |
                 SGS_FREQ  |
                 SGS_DYNFREQ  |
                 SGS_PHASE  |
                 SGS_AMP  |
                 SGS_DYNAMP  |
                 SGS_PANNING  |
                 SGS_ATTR;
    e->pmodid = e->fmodid = e->amodid = e->linkid = -1;
  }

  if (composite) {
    if (!nd->composite) {
      nd->composite = nd->last;
      if (nd->composite->time_ms < 0)
        nd->composite->time_ms = o->def_time_ms;
    }
    /* Composite timing - add previous time to wait */
    e->wait_ms += nd->last->time_ms;
    e->time_ms = -1; /* defaults to time of previous step of composite */

    pe = nd->last;
  } else {
    nd->composite = 0;
    if (!nd->group)
      nd->group = e;

    /* Linkage */
    o->undo_last = o->last_event;
    if (!p->events)
      p->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;

    pe = o->last_event;
  }
  /* Prepare timing adjustment */
  nd->add_wait_ms += nd->next_wait_ms;
  nd->next_wait_ms = 0;
  if (nd->wait_duration) {
    if (pe)
      nd->add_wait_ms += pe->time_ms;
    nd->wait_duration = 0;
  }
}

static void end_operator(SGSParser *o, NodeData *nd) {
  SGSProgram *p = o->prg;
  SGSProgramEvent *oe = nd->event;
  end_event(o, nd);
  if (!oe)
    return; /* nothing to do */

  ++p->operatorc;

  if (nd->setsym) {
    SGSSymtab_set(o->st, nd->setsym, oe);
    free(nd->setsym);
    nd->setsym = 0;
  }
}

static void end_event(SGSParser *o, NodeData *nd) {
  SGSProgram *p = o->prg;
  SGSProgramEvent *e = nd->event, *pe;
  if (!e)
    return; /* nothing to do */
  nd->event = 0;

  pe = e->opprev;
  if (pe) { /* Check what the event changes */
    if (e->amodid != pe->amodid)
      e->params |= SGS_AMOD;
    if (e->fmodid != pe->fmodid)
      e->params |= SGS_FMOD;
    if (e->pmodid != pe->pmodid)
      e->params |= SGS_PMOD;
    if (e->linkid != pe->linkid)
      e->params |= SGS_LINK;
    if (e->attr != pe->attr)
      e->params |= SGS_ATTR;
    if (e->wave != pe->wave)
      e->params |= SGS_WAVE;
    /* SGS_TIME set when time set */
    /* SGS_SILENCE set when silence set */
    if (e->freq != pe->freq)
      e->params |= SGS_FREQ;
    if (e->dynfreq != pe->dynfreq)
      e->params |= SGS_DYNFREQ;
    /* SGS_PHASE set when phase set */
    if (e->amp != pe->amp)
      e->params |= SGS_AMP;
    if (e->dynamp != pe->dynamp)
      e->params |= SGS_DYNAMP;
    if (e->panning != pe->panning)
      e->params |= SGS_PANNING;

    if (!e->params) { /* Remove empty event */
      if (nd->group == e)
        nd->group = 0;
      if (o->last_event == e) {
        o->last_event = o->undo_last;
        o->undo_last->next = 0;
      }
      --p->eventc;
      free(e);
      return;
    } else { /* Link previous event */
      pe->opnext = e;
    }
  }

  if (e->optype == SGS_TYPE_TOP) /* Adjust for "voice" (output level) only */
    e->amp *= o->ampmult;

  if (nd->composite) {
    if (nd->add_wait_ms) { /* Simulate with silence */
      e->silence_ms += nd->add_wait_ms;
      e->params |= SGS_SILENCE;
      nd->acc_wait_ms += nd->add_wait_ms; /* ...and keep for non-composite */
      nd->add_wait_ms = 0;
    }
    /* Add time of composites */
    if (e->time_ms < 0)
      e->time_ms = nd->last->time_ms - nd->last->silence_ms;
    nd->composite->time_ms += e->time_ms + e->silence_ms;
    e->params &= ~SGS_TIME;
    if (!e->params) {
      free(e);
      return;
    }
    if (!nd->composite->composite)
      nd->composite->composite = e;
    else
      nd->last->next = e;
  } else {
    /* Timing of |-terminated sequence */
    if (nd->end_group) {
      int wait = 0, waitcount = 0;
      SGSProgramEvent *step;
      for (step = nd->group; step != e; step = step->next) {
        if (step->optype == SGS_TYPE_NESTED)
          continue;
        if (step->next == e && step->time_ms < 0) /* Set and use default for last node in group */
          step->time_ms = o->def_time_ms;
        if (wait < step->time_ms)
          wait = step->time_ms;
        wait -= step->next->wait_ms;
        waitcount += step->next->wait_ms;
      }
      for (step = nd->group; step != e; step = step->next) {
        if (step->time_ms < 0)
          step->time_ms = wait + waitcount; /* fill in sensible default time */
        waitcount -= step->next->wait_ms;
      }
      nd->add_wait_ms += wait;
      nd->group = e;
      nd->end_group = 0;
    }
    e->wait_ms += nd->add_wait_ms + nd->acc_wait_ms;
    nd->add_wait_ms = 0;
    nd->acc_wait_ms = 0;
  }
  if (e->time_ms >= 0)
    e->time_ms += e->silence_ms;
 
  nd->last = e;
  if (nd->oplast->optype == e->optype &&
    nd->oplast->opid == e->opid)
  nd->oplast = e;
}

static double getnum_r(FILE *f, char *buf, uint len, uchar pri) {
  char *p = buf;
  uchar dot = 0;
  double num;
  char c;
  do {
    c = getc(f);
  } while (IS_WHITESPACE(c));
  if (c == '(') {
    return getnum_r(f, buf, len, 255);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = getc(f);
  }
  if (p == buf) {
    ungetc(c, f);
    return NAN;
  }
  *p = '\0';
  num = strtod(buf, 0);
  for (;;) {
    while (IS_WHITESPACE(c))
      c = getc(f);
    switch (c) {
    case '(':
      num *= getnum_r(f, buf, len, 255);
      break;
    case ')':
      if (pri < 255)
        ungetc(c, f);
      return num;
      break;
    case '^':
      num = exp(log(num) * getnum_r(f, buf, len, 0));
      break;
    case '*':
      num *= getnum_r(f, buf, len, 1);
      break;
    case '/':
      num /= getnum_r(f, buf, len, 1);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += getnum_r(f, buf, len, 2);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= getnum_r(f, buf, len, 2);
      break;
    default:
      return num;
    }
    if (num != num) {
      ungetc(c, f);
      return num;
    }
    c = getc(f);
  }
}
static double getnum(FILE *f) {
  char buf[64];
  char *p = buf;
  uchar dot = 0;
  if ((*p = getc(f)) == '(')
    return getnum_r(f, buf, 64, 255);
  do {
    if ((*p >= '0' && *p <= '9') || (!dot && (dot = (*p == '.'))))
      ++p;
    else
      break;
  } while ((*p = getc(f)) && p < (buf+64));
  ungetc(*p, f);
  *p = '\0';
  return strtod(buf, 0);
}

static int strfind(FILE *f, const char *const*str) {
  int search, ret;
  uint i, len, pos, matchpos;
  char c, undo[256];
  uint strc;
  const char **s;

  for (len = 0, strc = 0; str[strc]; ++strc)
    if ((i = strlen(str[strc])) > len) len = i;
  s = malloc(sizeof(const char*) * strc);
  for (i = 0; i < strc; ++i)
    s[i] = str[i];
  search = ret = -1;
  pos = matchpos = 0;
  while ((c = getc(f)) != EOF) {
    undo[pos] = c;
    for (i = 0; i < strc; ++i) {
      if (!s[i]) continue;
      else if (!s[i][pos]) {
        s[i] = 0;
        if (search == (int)i) {
          ret = i;
          matchpos = pos-1;
        }
      } else if (c != s[i][pos]) {
        s[i] = 0;
        search = -1;
      } else
        search = i;
    }
    if (pos == len) break;
    ++pos;
  }
  free(s);
  for (i = pos; i > matchpos; --i) ungetc(undo[i], f);
  return ret;
}

static void eatws(FILE *f) {
  char c;
  while ((c = getc(f)) == ' ' || c == '\t') ;
  ungetc(c, f);
}

static uchar testc(char c, FILE *f) {
  char gc = getc(f);
  ungetc(gc, f);
  return (gc == c);
}

static uchar testgetc(char c, FILE *f) {
  char gc;
  if ((gc = getc(f)) == c) return 1;
  ungetc(gc, f);
  return 0;
}

static void warning(SGSParser *o, const char *s, char c) {
  char buf[4] = {'\'', c, '\'', 0};
  if (c == EOF) strcpy(buf, "EOF");
  printf("warning: %s [line %d, at %s] - %s\n", o->fn, o->line, buf, s);
}

#define SYMKEY_LEN 80
#define SYMKEY_LEN_A "80"
static uchar read_sym(SGSParser *o, char **sym, char op) {
  uint i = 0;
  char nosym_msg[] = "ignoring ? without symbol name";
  nosym_msg[9] = op; /* replace ? */
  if (!*sym)
    *sym = malloc(SYMKEY_LEN);
  for (;;) {
    char c = getc(o->f);
    if (IS_WHITESPACE(c)) {
      if (i == 0)
        warning(o, nosym_msg, c);
      else END_OF_SYM: {
        (*sym)[i] = '\0';
        return 1;
      }
      break;
    } else if (i == SYMKEY_LEN) {
      warning(o, "ignoring symbol name from "SYMKEY_LEN_A"th digit", c);
      goto END_OF_SYM;
    }
    (*sym)[i++] = c;
  }
  return 0;
}

static int read_wavetype(SGSParser *o, char lastc) {
  static const char *wavetypes[] = {
    "sin",
    "sqr",
    "tri",
    "saw",
    0
  };
  int wave = strfind(o->f, wavetypes);
  if (wave < 0)
    warning(o, "invalid wave type follows; sin, sqr, tri, saw available", lastc);
  return wave;
}

static void parse_level(SGSParser *o, NodeTarget *chaintarget);

static SGSProgram* parse(FILE *f, const char *fn, SGSParser *o) {
  memset(o, 0, sizeof(SGSParser));
  o->f = f;
  o->fn = fn;
  o->prg = calloc(1, sizeof(SGSProgram));
  o->st = SGSSymtab_create();
  o->line = 1;
  o->ampmult = 1.f; /* default until changed */
  o->def_time_ms = 1000; /* default until changed */
  o->def_freq = 100.f; /* default until changed */
  o->def_ratio = 1.f; /* default until changed */
  parse_level(o, 0);
  SGSSymtab_destroy(o->st);
  /* Flatten composites */
  {/**/
  SGSProgramEvent *e;
  uint id = 0;
  for (e = o->prg->events; e; e = e->next) {
    SGSProgramEvent *ce = e->composite;
    if (ce) {
      SGSProgramEvent *se = e->next, *se_prev = e;
      int wait_ms = 0;
      int added_wait_ms = 0;
      for (;;) {
        if (!se) {
          se_prev->next = ce;
          break;
        }
        wait_ms += se->wait_ms;
        if (se->next) {
          if ((wait_ms + se->next->wait_ms) <= (ce->wait_ms + added_wait_ms)) {
            se_prev = se;
            se = se->next;
            continue;
          }
        }
        if (se->wait_ms >= (ce->wait_ms + added_wait_ms)) {
          SGSProgramEvent *ce_next = ce->next;
          se->wait_ms -= ce->wait_ms + added_wait_ms;
          added_wait_ms = 0;
          wait_ms = 0;
          se_prev->next = ce;
          se_prev = ce;
          se_prev->next = se;
          ce = ce_next;
          if (!ce) break;
        } else {
          SGSProgramEvent *se_next, *ce_next;
          se_next = se->next;
          ce_next = ce->next;
          ce->wait_ms -= wait_ms;
          added_wait_ms += ce->wait_ms;
          wait_ms = 0;
          se->next = ce;
          ce->next = se_next;
          se_prev = ce;
          se = se_next;
          ce = ce_next;
          if (!ce) break;
        }
      }
      e->composite = 0;
    }
    e->id = id++;
  }
  /**/}
{SGSProgramEvent *e = o->prg->events;
putchar('\n');
do{
printf("ev %d, op %d (%s): \t/=%d \tt=%d\n", e->id, e->opid, e->optype ? "nested" : "top", e->wait_ms, e->time_ms);
} while ((e = e->next));}
  return o->prg;
}

static void parse_level(SGSParser *o, NodeTarget *chaintarget) {
  char c;
  NodeData nd;
  uint entrylevel = o->level;
  ++o->reclevel;
  memset(&nd, 0, sizeof(NodeData));
  if (chaintarget)
    *chaintarget->idtarget = -1;
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case '\n':
    EOL:
      if (!chaintarget) {
        if (o->setdef > o->level)
          o->setdef = (o->level) ? (o->level - 1) : 0;
        else if (o->setnode > o->level)
          o->setnode = (o->level) ? (o->level - 1) : 0;
      }
      ++o->line;
      break;
    case '\t':
    case ' ':
      eatws(o->f);
      break;
    case '#':
      while ((c = getc(o->f)) != '\n' && c != EOF) ;
      goto EOL;
      break;
    case '/':
      if (o->setdef > o->setnode ||
          (nd.event && nd.event->optype == SGS_TYPE_NESTED))
        goto INVALID;
      if (testgetc('t', o->f))
        nd.wait_duration = 1;
      else {
        float wait = getnum(o->f);
        if (wait < 0.f) {
          warning(o, "ignoring '/' with sub-zero time", c);
          break;
        }
        nd.wait_duration = 0;
        nd.next_wait_ms += wait * 1000.f;
      }
      break;
    case ':':
      end_operator(o, &nd);
      if (nd.setsym)
        warning(o, "ignoring label assignment to label reference", c);
      else if (chaintarget)
        goto INVALID;
      if (read_sym(o, &nd.setsym, ':')) {
        SGSProgramEvent *ref = SGSSymtab_get(o->st, nd.setsym);
        if (!ref)
          warning(o, "ignoring reference to undefined label", c);
        else {
          new_event(o, &nd, ref, 0);
          o->setnode = o->level + 1;
        }
      }
      break;
    case ';':
      if (o->setdef > o->setnode || !nd.event)
        goto INVALID;
      new_event(o, &nd, nd.event, 1);
      o->setnode = o->level + 1;
      break;
    case '<':
      ++o->level;
      break;
    case '>':
      if (!o->level) {
        warning(o, "closing '>' without opening '<'", c);
        break;
      }
      if (o->setdef > o->level)
        o->setdef = (o->level) ? (o->level - 1) : 0;
      else if (o->setnode > o->level) {
        o->setnode = (o->level) ? (o->level - 1) : 0;
        end_operator(o, &nd);
      }
      --o->level;
      break;
    case 'Q':
      goto FINISH;
    case 'S':
      o->setdef = o->level + 1;
      break;
    case 'W': {
      int wave = read_wavetype(o, c);
      if (wave < 0)
        break;
      new_operator(o, &nd, chaintarget, wave);
      o->setnode = o->level + 1;
      break; }
    case '\\':
      if (o->setdef > o->setnode || !nd.event ||
          nd.event->optype == SGS_TYPE_NESTED)
        goto INVALID;
      else {
        float wait = getnum(o->f);
        if (wait < 0.f) {
          warning(o, "ignoring '\\' with sub-zero time", c);
          break;
        }
        nd.add_wait_ms += wait * 1000.f;
      }
      break;
    case '\'':
      end_operator(o, &nd);
      if (nd.setsym) {
        warning(o, "ignoring label assignment to label assignment", c);
        break;
      }
      read_sym(o, &nd.setsym, '\'');
      break;
    case 'a':
      if (o->setdef > o->setnode)
        o->ampmult = getnum(o->f);
      else if (o->setnode > 0) {
        if (chaintarget &&
            (chaintarget->modtype == SGS_AMOD ||
             chaintarget->modtype == SGS_FMOD))
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.event->dynamp = getnum(o->f);
          }
          if (testgetc('{', o->f)) {
            NodeTarget nt = {&nd.event->amodid, nd.event->topopid, SGS_AMOD};
            parse_level(o, &nt);
          }
        } else {
          nd.event->amp = getnum(o->f);
        }
      } else
        goto INVALID;
      break;
    case 'b':
      if (o->setdef > o->setnode || !o->setnode ||
          nd.event->optype == SGS_TYPE_NESTED)
        goto INVALID;
      nd.event->panning = getnum(o->f);
      break;
    case 'f':
      if (o->setdef > o->setnode)
        o->def_freq = getnum(o->f);
      else if (o->setnode > 0) {
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.event->dynfreq = getnum(o->f);
            nd.event->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            NodeTarget nt = {&nd.event->fmodid, nd.event->topopid, SGS_FMOD};
            parse_level(o, &nt);
          }
        } else {
          nd.event->freq = getnum(o->f);
          nd.event->attr &= ~SGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 'p': {
      if (o->setdef > o->setnode || !o->setnode)
        goto INVALID;
      if (testgetc('!', o->f)) {
        if (testgetc('{', o->f)) {
            NodeTarget nt = {&nd.event->pmodid, nd.event->topopid, SGS_PMOD};
            parse_level(o, &nt);
        }
      } else {
        nd.event->phase = fmod(getnum(o->f), 1.f);
        if (nd.event->phase < 0.f)
          nd.event->phase += 1.f;
        nd.event->params |= SGS_PHASE;
      }
      break; }
    case 'r':
      if (o->setdef > o->setnode)
        o->def_ratio = 1.f / getnum(o->f);
      else if (o->setnode > 0) {
        if (!chaintarget)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.event->dynfreq = 1.f / getnum(o->f);
            nd.event->attr |= SGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            NodeTarget nt = {&nd.event->fmodid, nd.event->topopid, SGS_FMOD};
            parse_level(o, &nt);
          }
        } else {
          nd.event->freq = 1.f / getnum(o->f);
          nd.event->attr |= SGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 's': {
      float silence;
      if (o->setdef > o->setnode || !o->setnode)
        goto INVALID;
      silence = getnum(o->f);
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time", c);
        break;
      }
      nd.event->silence_ms = silence * 1000.f;
      nd.event->params |= SGS_SILENCE;
      break; }
    case 't':
      if (o->setdef > o->setnode) {
        float time = getnum(o->f);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time", c);
          break;
        }
        o->def_time_ms = time * 1000.f;
      } else if (o->setnode > 0) {
        if (testgetc('*', o->f))
          nd.event->time_ms = -1; /* later fitted or set to default */
        else {
          float time;
          time = getnum(o->f);
          if (time < 0.f) {
            warning(o, "ignoring 't' with sub-zero time", c);
            break;
          }
          nd.event->time_ms = time * 1000.f;
        }
        nd.event->params |= SGS_TIME;
      } else
        goto INVALID;
      break;
    case 'w': {
      int wave;
      if (o->setdef > o->setnode || !o->setnode)
        goto INVALID;
      wave = read_wavetype(o, c);
      if (wave < 0)
        break;
      nd.event->wave = wave;
      break; }
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '|':
      if (o->setdef > o->setnode ||
          (nd.event && nd.event->optype == SGS_TYPE_NESTED))
        goto INVALID;
      end_operator(o, &nd);
      if (!nd.group)
        warning(o, "end of sequence before any parts given", c);
      else
        nd.end_group = 1;
      break;
    case '}':
      if (!chaintarget)
        goto INVALID;
      if (o->level != entrylevel) {
        o->level = entrylevel;
        warning(o, "closing '}' before closing '>'s", c);
      }
      goto RETURN;
    default:
    INVALID:
      warning(o, "invalid character", c);
      break;
    }
  }
FINISH:
  if (o->level)
    warning(o, "end of file without closing '>'s", c);
  if (o->reclevel > 1)
    warning(o, "end of file without closing '}'s", c);
RETURN:
  if (nd.event) {
    if (nd.event->time_ms < 0) {
      nd.event->time_ms = o->def_time_ms; /* use default */
      nd.event->time_ms += nd.event->silence_ms;
    }
    if (!o->reclevel)
      nd.end_group = 1; /* end grouping if any */
    end_operator(o, &nd);
  }
  if (nd.setsym)
    free(nd.setsym);
  --o->reclevel;
}

SGSProgram* SGSProgram_create(const char *filename) {
  SGSProgram *o;
  SGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  o = parse(f, filename, &p);
  fclose(f);
  return o;
}

void SGSProgram_destroy(SGSProgram *o) {
  SGSProgramEvent *e = o->events;
  while (e) {
    SGSProgramEvent *ne = e->next;
    free(e);
    e = ne;
  }
}
