#include "sgensys.h"
#include "program.h"
#include "symtab.h"
#include "math.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * General-purpose functions
 */

#define IS_WHITESPACE(c) \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

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

static int getinum(FILE *f) {
  char c;
  int num = -1;
  c = getc(f);
  if (c >= '0' && c <= '9') {
    num = c - '0';
    for (;;) {
      c = getc(f);
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        break;
    }
  }
  ungetc(c, f);
  return num;
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

/*
 * Parsing code
 */

enum {
  MAIN_ADJCS = 1<<0,
  FMOD_ADJCS = 1<<1,
  AMOD_ADJCS = 1<<2,
  OLD_VOICE_EVENT = 1<<3,
  ADD_WAIT_DURATION = 1<<4,
  SILENCE_ADDED = 1<<5
};

typedef struct EventNode {
  void *next, *lvnext;
  void *groupfrom;
  int wait_ms;
  uint id;
  uint params;
  uint nestlevel;
  uchar type;
  uchar parseflags;
  /* operator graph (voice) or adjacents of node (operator) */
  uchar mainc;
  uchar fmodc;
  uchar amodc;
  struct OperatorEvent **main_adjcs; /* PMs or graph entry points */
  struct OperatorEvent **fmod_adjcs; /* FMs */
  struct OperatorEvent **amod_adjcs; /* AMs */
} EventNode;

typedef struct VoiceEvent {
  EventNode e;
  uint voiceid;
  /* parameters: */
  float panning;
  SGSProgramValit valitpanning;
} VoiceEvent;

typedef struct OperatorEvent {
  EventNode e;
  struct OperatorEvent *opprev, *opnext; /* per-operator linked list */
  struct OperatorEvent *composite;
  uint operatorid;
  VoiceEvent *voice;
  /* parameters: */
  uchar attr;
  uchar wave;
  int time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
} OperatorEvent;

typedef struct SGSParser {
  FILE *f;
  const char *fn;
  SGSProgram *prg;
  SGSSymtab *st;
  uint line;
  uint calllevel;
  uint nestlevel;
  char nextc;
  /* node state */
  EventNode *events;
  uint eventc;
  uint opc;
  uint voicec;
  EventNode *last_event;
  EventNode *undo_last;
  /* settings/ops */
  float ampmult;
  uint def_time_ms;
  float def_freq, def_A4tuning, def_ratio;
} SGSParser;

typedef struct NodeTarget {
  EventNode *parent;
  uchar linktype;
} NodeTarget;

/* things that need to be separate for each nested parse_level() go here */
typedef struct NodeData {
  uchar setdef; /* adjusting default values */
  char scope;
  VoiceEvent *voice;
  OperatorEvent *operator; /* state for tentative event until end_event() */
  OperatorEvent *first, *current, *last;
  NodeTarget *target;
  char *setsym;
  /* timing/delay */
  void *group;
  OperatorEvent *composite; /* allows specifying events out of linear order*/
  uchar end_composite;
  uint next_wait_ms; /* added for next event; adjusted in parser */
  uint add_wait_ms; /* added to event's wait in end_event() */
} NodeData;

static void add_adjc(EventNode *o, OperatorEvent *adjc, uchar type) {
  OperatorEvent ***adjcs = 0;
  uchar *count = 0;
  switch (type) {
  case MAIN_ADJCS:
    count = &o->mainc;
    adjcs = &o->main_adjcs;
    break;
  case FMOD_ADJCS:
    count = &o->fmodc;
    adjcs = &o->fmod_adjcs;
    break;
  case AMOD_ADJCS:
    count = &o->amodc;
    adjcs = &o->amod_adjcs;
    break;
  }
  if (*count && !(o->parseflags & type)) { /* adjacents were inherited */
    *count = 0;
    *adjcs = 0;
  }
  ++(*count);
  *adjcs = realloc(*adjcs, sizeof(OperatorEvent*) * (*count));
  (*adjcs)[*count - 1] = adjc;
  o->parseflags |= type;
}

static void end_operator(SGSParser *o, NodeData *nd);

static void new_event(SGSParser *o, NodeData *nd, EventNode *previous,
                      uchar type, uchar composite) {
  EventNode *e = 0;
  if (previous)
    type = previous->type;
  switch (type) {
  case SGS_EVENT_VOICE: {
    VoiceEvent *ve, *pve;
    nd->voice = calloc(1, sizeof(VoiceEvent));
    ve = nd->voice;
    ve->e.id = o->eventc++;
    ve->e.nestlevel = o->nestlevel;
    ve->e.type = SGS_EVENT_VOICE;
    pve = (VoiceEvent*)previous;
    if (pve) {
      ve->e.mainc = pve->e.mainc;
      ve->e.main_adjcs = pve->e.main_adjcs;
      ve->voiceid = pve->voiceid;
      ve->panning = pve->panning;
    } else { /* first event - everything set to defaults */
      ve->e.params |= SGS_ADJC |
                      SGS_PANNING;
      ve->voiceid = o->voicec++;
      ve->panning = 0.5f; /* center (default) */
    }
    e = &ve->e;
    break; }
  case SGS_EVENT_OPERATOR: {
    OperatorEvent *oe, *poe;
    end_operator(o, nd);
    nd->operator = calloc(1, sizeof(OperatorEvent));
    oe = nd->operator;
    oe->e.id = o->eventc++;
    oe->e.nestlevel = o->nestlevel;
    oe->e.type = SGS_EVENT_OPERATOR;
    poe = (OperatorEvent*)previous;
    if (poe) {
      nd->voice = poe->voice;
      oe->e.parseflags |= OLD_VOICE_EVENT; /* FIXME: handling must work */
      oe->e.mainc = poe->e.mainc;
      oe->e.fmodc = poe->e.fmodc;
      oe->e.amodc = poe->e.amodc;
      oe->e.main_adjcs = poe->e.main_adjcs;
      oe->e.fmod_adjcs = poe->e.fmod_adjcs;
      oe->e.amod_adjcs = poe->e.amod_adjcs;
      oe->opprev = poe;
      oe->operatorid = poe->operatorid;
      oe->attr = poe->attr;
      oe->wave = poe->wave;
      oe->freq = poe->freq;
      oe->dynfreq = poe->dynfreq;
      oe->amp = poe->amp;
      oe->dynamp = poe->dynamp;
    } else { /* first event - everything set to defaults */
      oe->e.params |= SGS_ADJC |
                      SGS_WAVE |
                      SGS_TIME |
                      SGS_SILENCE |
                      SGS_FREQ |
                      SGS_DYNFREQ |
                      SGS_PHASE |
                      SGS_AMP |
                      SGS_DYNAMP |
                      SGS_ATTR;
      oe->operatorid = o->opc++;
    }
    if (!nd->voice)
      new_event(o, nd, 0, SGS_EVENT_VOICE, 0);
    oe->voice = nd->voice;
    if (composite) {
      if (!nd->composite)
        nd->composite = nd->last;
      oe->time_ms = -1; /* defaults to time of previous step of composite */
    } else {
      e = &oe->e;
      nd->current = oe; /* remains the same for composite events */
      nd->composite = 0;
    }
    break; }
  }
  if (e) {
    /* Linkage */
    o->undo_last = o->last_event;
    if (!o->events)
      o->events = e;
    else
      o->last_event->next = e;
    o->last_event = e;
  }
  /* Prepare timing adjustment */
  nd->add_wait_ms += nd->next_wait_ms;
  nd->next_wait_ms = 0;
}

static void end_voice(SGSParser *o, NodeData *nd) {
  VoiceEvent *ve = nd->voice;
  if (!ve)
    return; /* nothing to do */
  nd->voice = 0;

  if (ve->valitpanning.type)
    ve->e.params |= SGS_ATTR | SGS_VALITPANNING;
//    if (!oe->nestlevel) {
//      if (oe->topop.panning != poe->topop.panning)
//        oe->e.params |= SGS_PANNING;
//    }

  ve->e.wait_ms += nd->add_wait_ms;
  nd->add_wait_ms = 0;
}

static void new_operator(SGSParser *o, NodeData *nd, NodeTarget *target,
                         uchar wave) {
  OperatorEvent *oe;
  EventNode *parent;
  uchar linktype;
  end_operator(o, nd);
  new_event(o, nd, 0, SGS_EVENT_OPERATOR, 0);
  oe = nd->operator;
  oe->wave = wave;
  nd->target = target;
  if (target) {
    parent = target->parent;
    linktype = target->linktype;
  } else {
    parent = &nd->voice->e;
    linktype = MAIN_ADJCS;
  }
  parent->params |= SGS_ADJC;
  add_adjc(parent, oe, linktype);

  /* Set defaults */
  oe->amp = 1.f;
  if (!oe->e.nestlevel) {
    oe->time_ms = -1; /* later fitted or set to default */
    oe->freq = o->def_freq;
  } else {
    oe->time_ms = o->def_time_ms;
    oe->freq = o->def_ratio;
    oe->attr |= SGS_ATTR_FREQRATIO;
  }
}

static void end_operator(SGSParser *o, NodeData *nd) {
  OperatorEvent *oe = nd->operator, *poe;
  if (!oe)
    return; /* nothing to do */
  nd->operator = 0;

  poe = oe->opprev;
  if (oe->valitfreq.type)
    oe->e.params |= SGS_ATTR | SGS_VALITFREQ;
  if (oe->valitamp.type)
    oe->e.params |= SGS_ATTR | SGS_VALITAMP;
  if (poe) { /* Check what the event changes */
    if (oe->attr != poe->attr)
      oe->e.params |= SGS_ATTR;
    if (oe->wave != poe->wave)
      oe->e.params |= SGS_WAVE;
    /* SGS_TIME set when time set */
    if (oe->silence_ms)
      oe->e.params |= SGS_SILENCE;
    /* SGS_FREQ set when freq set */
    if (oe->dynfreq != poe->dynfreq)
      oe->e.params |= SGS_DYNFREQ;
    /* SGS_PHASE set when phase set */
    /* SGS_AMP set when amp set */
    if (oe->dynamp != poe->dynamp)
      oe->e.params |= SGS_DYNAMP;

    /* Link previous event */
    poe->opnext = oe;
  }

  if (!nd->group)
    nd->group = oe;
  if (nd->composite) {
    if (!nd->composite->composite)
      nd->composite->composite = oe;
    else
      nd->last->e.next = oe;
  }
  oe->e.wait_ms += nd->add_wait_ms;
  nd->add_wait_ms = 0;

  if (!oe->e.nestlevel)
    oe->amp *= o->ampmult; /* Adjust for "voice" (output level) only */
 
  if (!nd->first)
    nd->first = oe;
  nd->last = oe;

  if (nd->setsym) {
    SGSSymtab_set(o->st, nd->setsym, oe);
    free(nd->setsym);
    nd->setsym = 0;
  }
}

/*
 * Parsing routines
 */

#define NEWLINE '\n'
static char read_char(SGSParser *o) {
  char c;
  eatws(o->f);
  if (o->nextc) {
    c = o->nextc;
    o->nextc = 0;
  } else {
    c = getc(o->f);
  }
  if (c == '#')
    while ((c = getc(o->f)) != '\n' && c != '\r' && c != EOF) ;
  if (c == '\n') {
    testgetc('\r', o->f);
    c = NEWLINE;
  } else if (c == '\r') {
    testgetc('\n', o->f);
    c = NEWLINE;
  } else {
    eatws(o->f);
  }
  return c;
}

static void read_ws(SGSParser *o) {
  char c;
  do {
    c = getc(o->f);
    if (c == ' ' || c == '\t')
      continue;
    if (c == '\n') {
      ++o->line;
      testgetc('\r', o->f);
    } else if (c == '\r') {
      ++o->line;
      testgetc('\n', o->f);
    } else if (c == '#') {
      while ((c = getc(o->f)) != '\n' && c != '\r' && c != EOF) ;
    } else {
      ungetc(c, o->f);
      break;
    }
  } while (c != EOF);
}

static float read_num_r(SGSParser *o, float (*read_symbol)(SGSParser *o), char *buf, uint len, uchar pri, uint level) {
  char *p = buf;
  uchar dot = 0;
  float num;
  char c;
  c = getc(o->f);
  if (level)
    read_ws(o);
  if (c == '(') {
    return read_num_r(o, read_symbol, buf, len, 255, level+1);
  }
  if (read_symbol &&
      ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    ungetc(c, o->f);
    num = read_symbol(o);
    if (num == num) { /* not NAN; was recognized */
      c = getc(o->f);
      goto LOOP;
    }
  }
  if (c == '-') {
    *p++ = c;
    c = getc(o->f);
    if (level)
      read_ws(o);
  }
  while ((c >= '0' && c <= '9') || (!dot && (dot = (c == '.')))) {
    if ((p+1) == (buf+len)) {
      break;
    }
    *p++ = c;
    c = getc(o->f);
  }
  if (p == buf) {
    ungetc(c, o->f);
    return NAN;
  }
  *p = '\0';
  num = strtod(buf, 0);
LOOP:
  for (;;) {
    if (level)
      read_ws(o);
    switch (c) {
    case '(':
      num *= read_num_r(o, read_symbol, buf, len, 255, level+1);
      break;
    case ')':
      if (pri < 255)
        ungetc(c, o->f);
      return num;
      break;
    case '^':
      num = exp(log(num) * read_num_r(o, read_symbol, buf, len, 0, level));
      break;
    case '*':
      num *= read_num_r(o, read_symbol, buf, len, 1, level);
      break;
    case '/':
      num /= read_num_r(o, read_symbol, buf, len, 1, level);
      break;
    case '+':
      if (pri < 2)
        return num;
      num += read_num_r(o, read_symbol, buf, len, 2, level);
      break;
    case '-':
      if (pri < 2)
        return num;
      num -= read_num_r(o, read_symbol, buf, len, 2, level);
      break;
    default:
      ungetc(c, o->f);
      return num;
    }
    if (num != num) {
      ungetc(c, o->f);
      return num;
    }
    c = getc(o->f);
  }
}
static uchar read_num(SGSParser *o, float (*read_symbol)(SGSParser *o),
                      float *var) {
  char buf[64];
  float num = read_num_r(o, read_symbol, buf, 64, 254, 0);
  if (num != num)
    return 0;
  *var = num;
  return 1;
}

static void warning(SGSParser *o, const char *s, char c) {
  char buf[4] = {'\'', c, '\'', 0};
  printf("warning: %s [line %d, at %s] - %s\n", o->fn, o->line,
         (c == EOF ? "EOF" : buf), s);
}
#define WARN_INVALID "invalid character"

#define OCTAVES 11
static float read_note(SGSParser *o) {
  static const float octaves[OCTAVES] = {
    (1.f/16.f),
    (1.f/8.f),
    (1.f/4.f),
    (1.f/2.f),
    1.f, /* no. 4 - standard tuning here */
    2.f,
    4.f,
    8.f,
    16.f,
    32.f,
    64.f
  };
  static const float notes[3][8] = {
    { /* flat */
      48.f/25.f,
      16.f/15.f,
      6.f/5.f,
      32.f/25.f,
      36.f/25.f,
      8.f/5.f,
      9.f/5.f,
      96.f/25.f
    },
    { /* normal (9/8 replaced with 10/9 for symmetry) */
      1.f,
      10.f/9.f,
      5.f/4.f,
      4.f/3.f,
      3.f/2.f,
      5.f/3.f,
      15.f/8.f,
      2.f
    },
    { /* sharp */
      25.f/24.f,
      75.f/64.f,
      125.f/96.f,
      25.f/18.f,
      25.f/16.f,
      225.f/128.f,
      125.f/64.f,
      25.f/12.f
    }
  };
  float freq;
  char c = getc(o->f);
  int octave;
  int semitone = 1, note;
  int subnote = -1;
  if (c >= 'a' && c <= 'g') {
    subnote = c - 'c';
    if (subnote < 0) /* a, b */
      subnote += 7;
    c = getc(o->f);
  }
  if (c < 'A' || c > 'G') {
    warning(o, "invalid note specified - should be C, D, E, F, G, A or B", c);
    return NAN;
  }
  note = c - 'C';
  if (note < 0) /* A, B */
    note += 7;
  c = getc(o->f);
  if (c == 's')
    semitone = 2;
  else if (c == 'f')
    semitone = 0;
  else
    ungetc(c, o->f);
  octave = getinum(o->f);
  if (octave < 0) /* none given, default to 4 */
    octave = 4;
  else if (octave >= OCTAVES) {
    warning(o, "invalid octave specified for note - valid range 0-10", c);
    octave = 4;
  }
  freq = o->def_A4tuning * (3.f/5.f); /* get C4 */
  freq *= octaves[octave] * notes[semitone][note];
  if (subnote >= 0)
    freq *= 1.f + (notes[semitone][note+1] / notes[semitone][note] - 1.f) *
                  (notes[1][subnote] - 1.f);
  return freq;
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
    if (IS_WHITESPACE(c) || c == EOF) {
      ungetc(c, o->f);
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
  static const char *const wavetypes[] = {
    "sin",
    "srs",
    "tri",
    "sqr",
    "saw",
    0
  };
  int wave = strfind(o->f, wavetypes);
  if (wave < 0)
    warning(o, "invalid wave type follows; sin, sqr, tri, saw available", lastc);
  return wave;
}

static uchar read_valit(SGSParser *o, float (*read_symbol)(SGSParser *o),
                        SGSProgramValit *vi) {
  static const char *const valittypes[] = {
    "lin",
    "exp",
    "log",
    0
  };
  char c;
  uchar goal = 0;
  int type;
  vi->time_ms = -1;
  vi->type = SGS_VALIT_LIN; /* default */
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case NEWLINE:
      ++o->line;
      break;
    case 'c':
      type = strfind(o->f, valittypes);
      if (type >= 0) {
        vi->type = type + SGS_VALIT_LIN;
        break;
      }
      goto INVALID;
    case 't': {
      float time;
      if (read_num(o, 0, &time)) {
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time", c);
          break;
        }
        SET_I2F(vi->time_ms, time*1000.f);
      }
      break; }
    case 'v':
      if (read_num(o, read_symbol, &vi->goal))
        goal = 1;
      break;
    case ']':
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID, c);
      break;
    }
  }
  warning(o, "end of file without closing ']'", c);
RETURN:
  if (!goal) {
    warning(o, "ignoring gradual parameter change with no target value", c);
    vi->type = SGS_VALIT_NONE;
    return 0;
  }
  return 1;
}

static uchar read_waittime(SGSParser *o, NodeData *nd, char c) {
  if (testgetc('t', o->f)) {
    if (!nd->last) {
      warning(o, "add wait for last duration before any parts given", c);
      return 0;
    }
    nd->last->parseflags |= ADD_WAIT_DURATION;
  } else {
    float wait;
    int wait_ms;
    read_num(o, 0, &wait);
    if (wait < 0.f) {
      warning(o, "ignoring '\\' with sub-zero time", c);
      return 0;
    }
    SET_I2F(wait_ms, wait*1000.f);
    nd->next_wait_ms += wait_ms;
  }
  return 1;
}

/*
 * Main parser functions
 */

enum {
  SCOPE_SAME = 0,
  SCOPE_TOP = 1
};

static uchar parse_settings(SGSParser *o) {
  char c;
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case 'a':
      read_num(o, 0, &o->ampmult);
      break;
    case 'f':
      read_num(o, read_note, &o->def_freq);
      break;
    case 'n': {
      float freq;
      read_num(o, 0, &freq);
      if (freq < 1.f) {
        warning(o, "ignoring tuning frequency smaller than 1.0", c);
        break;
      }
      o->def_A4tuning = freq;
      break; }
    case 'r':
      if (read_num(o, 0, &o->def_ratio))
        o->def_ratio = 1.f / o->def_ratio;
      break;
    case 't': {
      float time;
      read_num(o, 0, &time);
      if (time < 0.f) {
        warning(o, "ignoring 't' with sub-zero time", c);
        break;
      }
      SET_I2F(o->def_time_ms, time*1000.f);
      break; }
    default:
    /*UNKNOWN:*/
      o->nextc = c;
      return 1; /* let parse_level() take care of it */
    }
  }
  return 0;
}

static uchar parse_level(SGSParser *o, NodeData *parentnd,
                         NodeTarget *target, char newscope);

static uchar parse_step(SGSParser *o, NodeData *nd) {
  char c;
  OperatorEvent *e = nd->event;
  NodeTarget *target = nd->target;
  while ((c = read_char(o)) != EOF) {
    switch (c) {
    case '\\':
      if (read_waittime(o, nd, c)) {
        end_event(o, nd);
        new_event(o, nd, nd->last, 0);
        e = nd->event;
      }
      break;
    case 'a':
      if (target &&
          (target->linktype == AMOD_ADJCS ||
           target->linktype == FMOD_ADJCS))
        goto UNKNOWN;
      if (testgetc('!', o->f)) {
        if (!testc('{', o->f)) {
          read_num(o, 0, &e->dynamp);
        }
        if (testgetc('{', o->f)) {
          NodeTarget nt = {e, 0, AMOD_ADJCS};
          if (e->params & SGS_ADJC)
            e->adjcs->amodc = 0;
          ++o->nestlevel;
          parse_level(o, nd, &nt, '{');
          --o->nestlevel;
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, 0, &e->valitamp))
          e->attr |= SGS_ATTR_VALITAMP;
      } else {
        read_num(o, 0, &e->amp);
        e->params |= SGS_AMP;
        if (!e->valitamp.type)
          e->attr &= ~SGS_ATTR_VALITAMP;
      }
      break;
    case 'b':
      if (e->nestlevel)
        goto UNKNOWN;
      if (testgetc('[', o->f)) {
        if (read_valit(o, 0, &e->topop.valitpanning))
          e->attr |= SGS_ATTR_VALITPANNING;
      } else if (read_num(o, 0, &e->topop.panning)) {
        if (!e->topop.valitpanning.type)
          e->attr &= ~SGS_ATTR_VALITPANNING;
      }
      break;
    case 'f':
      if (testgetc('!', o->f)) {
        if (!testc('{', o->f)) {
          if (read_num(o, 0, &e->dynfreq)) {
            e->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (testgetc('{', o->f)) {
          NodeTarget nt = {e, 0, FMOD_ADJCS};
          if (e->params & SGS_ADJC)
            e->adjcs->fmodc = 0;
          ++o->nestlevel;
          parse_level(o, nd, &nt, '{');
          --o->nestlevel;
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, read_note, &e->valitfreq)) {
          e->attr |= SGS_ATTR_VALITFREQ;
          e->attr &= ~SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, read_note, &e->freq)) {
        e->attr &= ~SGS_ATTR_FREQRATIO;
        e->params |= SGS_FREQ;
        if (!e->valitfreq.type)
          e->attr &= ~(SGS_ATTR_VALITFREQ |
                              SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 'p':
      if (read_num(o, 0, &e->phase)) {
        e->phase = fmod(e->phase, 1.f);
        if (e->phase < 0.f)
          e->phase += 1.f;
        e->params |= SGS_PHASE;
      }
      break;
    case 'r':
      if (!target)
        goto UNKNOWN;
      if (testgetc('!', o->f)) {
        if (!testc('{', o->f)) {
          if (read_num(o, 0, &e->dynfreq)) {
            e->dynfreq = 1.f / e->dynfreq;
            e->attr |= SGS_ATTR_DYNFREQRATIO;
          }
        }
        if (testgetc('{', o->f)) {
          NodeTarget nt = {e, 0, FMOD_ADJCS};
          if (e->params & SGS_ADJC)
            e->adjcs->fmodc = 0;
          ++o->nestlevel;
          parse_level(o, nd, &nt, '{');
          --o->nestlevel;
        }
      } else if (testgetc('[', o->f)) {
        if (read_valit(o, read_note, &e->valitfreq)) {
          e->valitfreq.goal = 1.f / e->valitfreq.goal;
          e->attr |= SGS_ATTR_VALITFREQ |
                            SGS_ATTR_VALITFREQRATIO;
        }
      } else if (read_num(o, 0, &e->freq)) {
        e->freq = 1.f / e->freq;
        e->attr |= SGS_ATTR_FREQRATIO;
        e->params |= SGS_FREQ;
        if (!e->valitfreq.type)
          e->attr &= ~(SGS_ATTR_VALITFREQ |
                              SGS_ATTR_VALITFREQRATIO);
      }
      break;
    case 's': {
      float silence;
      read_num(o, 0, &silence);
      if (silence < 0.f) {
        warning(o, "ignoring 's' with sub-zero time", c);
        break;
      }
      SET_I2F(e->silence_ms, silence*1000.f);
      break; }
    case 't':
      if (testgetc('*', o->f))
        e->time_ms = -1; /* later fitted or set to default */
      else {
        float time;
        read_num(o, 0, &time);
        if (time < 0.f) {
          warning(o, "ignoring 't' with sub-zero time", c);
          break;
        }
        SET_I2F(e->time_ms, time*1000.f);
      }
      e->params |= SGS_TIME;
      break;
    case 'w': {
      int wave = read_wavetype(o, c);
      if (wave < 0)
        break;
      e->wave = wave;
      break; }
    default:
    UNKNOWN:
      o->nextc = c;
      return 1; /* let parse_level() take care of it */
    }
  }
  return 0;
}

enum {
  HANDLE_DEFER = 1<<1,
  DEFERRED_STEP = 1<<2,
  DEFERRED_SETTINGS = 1<<4
};
static uchar parse_level(SGSParser *o, NodeData *parentnd,
                         NodeTarget *target, char newscope) {
  char c;
  uchar endscope = 0;
  uchar flags = 0;
  NodeData nd;
  ++o->calllevel;
  memset(&nd, 0, sizeof(NodeData));
  nd.scope = newscope;
  if (parentnd) {
    nd.setdef = parentnd->setdef;
    if (newscope == SCOPE_SAME)
      nd.scope = parentnd->scope;
    nd.voice = parentnd->voice;
  }
  while ((c = read_char(o)) != EOF) {
    flags &= ~HANDLE_DEFER;
    switch (c) {
    case NEWLINE:
      ++o->line;
      if (nd.scope == SCOPE_TOP) {
        end_operator(o, &nd);
        if (o->calllevel > 1)
          goto RETURN;
        flags = 0;
        nd.first = 0;
        nd.setdef = 0;
      }
      break;
    case '-': {
      OperatorEvent *first, *last;
      NodeTarget nt;
      uchar ret;
      end_operator(o, &nd);
      first = nd.first;
      last = nd.last;
      if (!last) {
        if (o->calllevel == 1) NO_CARRIER: {
          warning(o, "no preceding carrier operators", c);
          break;
        }
        first = parentnd->first;
        last = (parentnd->event ? parentnd->event : parentnd->last);
        if (!last)
          goto NO_CARRIER;
      }
      nt = (NodeTarget){last, 0, MAIN_ADJCS};
      if (first && first != last) {
        warning(o, "multiple carriers not yet supported", c);
        break;
      }
      if (last->params & SGS_ADJC)
        last->adjcs->mainc = 0;
      ++o->nestlevel;
      ret = parse_level(o, &nd, &nt, SCOPE_SAME);
      --o->nestlevel;
      if (ret)
        goto RETURN;
      break; }
    case ':':
      end_operator(o, &nd);
      if (nd.setsym)
        warning(o, "ignoring label assignment to label reference", c);
      nd.setdef = 0;
      if (read_sym(o, &nd.setsym, ':')) {
        OperatorEvent *ref = SGSSymtab_get(o->st, nd.setsym);
        if (!ref)
          warning(o, "ignoring reference to undefined label", c);
        else {
          new_event(o, &nd, ref, 0);
          flags = parse_step(o, &nd) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
        }
      }
      break;
    case ';':
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      if (nd.setdef || (!nd.event && !nd.last))
        goto INVALID;
      end_event(o, &nd);
      new_event(o, &nd, nd.last, 1);
      flags = parse_step(o, &nd) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break;
    case '<':
      if (parse_level(o, &nd, target, '<'))
        goto RETURN;
      break;
    case '>':
      if (nd.scope != '<') {
        warning(o, "closing '>' without opening '<'", c);
        break;
      }
      end_operator(o, &nd);
      endscope = 1;
      goto RETURN;
    case 'O': {
      int wave = read_wavetype(o, c);
      if (wave < 0)
        break;
      nd.setdef = 0;
      new_operator(o, &nd, target, wave);
      flags = parse_step(o, &nd) ? (HANDLE_DEFER | DEFERRED_STEP) : 0;
      break; }
    case 'Q':
      goto FINISH;
    case 'S':
      nd.setdef = 1;
      flags = parse_settings(o) ? (HANDLE_DEFER | DEFERRED_SETTINGS) : 0;
      break;
    case '\\':
      if (nd.setdef ||
          (nd.event && nd.event->nestlevel))
        goto INVALID;
      read_waittime(o, &nd, c);
      break;
    case '\'':
      end_operator(o, &nd);
      if (nd.setsym) {
        warning(o, "ignoring label assignment to label assignment", c);
        break;
      }
      read_sym(o, &nd.setsym, '\'');
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '|':
      if (nd.setdef ||
          (nd.event && nd.event->nestlevel))
        goto INVALID;
      if (newscope == SCOPE_SAME) {
        o->nextc = c;
        goto RETURN;
      }
      end_operator(o, &nd);
      if (!nd.last) {
        warning(o, "end of sequence before any parts given", c);
        break;
      }
      if (nd.group) {
        nd.current->groupfrom = nd.group;
        nd.group = 0;
      }
      break;
    case '}':
      if (nd.scope != '{') {
        warning(o, "closing '}' without opening '{'", c);
        break;
      }
      endscope = 1;
      goto RETURN;
    default:
    INVALID:
      warning(o, WARN_INVALID, c);
      break;
    }
    /* Return to sub-parsing routines. */
    if (flags && !(flags & HANDLE_DEFER)) {
      uchar test = flags;
      flags = 0;
      if (test & DEFERRED_STEP) {
        if (parse_step(o, &nd))
          flags = HANDLE_DEFER | DEFERRED_STEP;
      } else if (test & DEFERRED_SETTINGS)
        if (parse_settings(o))
          flags = HANDLE_DEFER | DEFERRED_SETTINGS;
    }
  }
FINISH:
  if (newscope == '<')
    warning(o, "end of file without closing '>'s", c);
  if (newscope == '{')
    warning(o, "end of file without closing '}'s", c);
RETURN:
  if (nd.event) {
    if (nd.event->time_ms < 0)
      nd.event->time_ms = o->def_time_ms; /* use default */
    end_operator(o, &nd);
  }
  if (nd.current)
    nd.current->groupfrom = nd.group;
  if (nd.setsym)
    free(nd.setsym);
  --o->calllevel;
  /* Should return from the calling scope if/when the parent scope is ended. */
  return (endscope && nd.scope != newscope);
}

static void parse(FILE *f, const char *fn, SGSParser *o) {
  NodeTarget nt = {0, 0, 0};
  memset(o, 0, sizeof(SGSParser));
  o->f = f;
  o->fn = fn;
  o->prg = calloc(1, sizeof(SGSProgram));
  o->st = SGSSymtab_create();
  o->line = 1;
  o->ampmult = 1.f; /* default until changed */
  o->def_time_ms = 1000; /* default until changed */
  o->def_freq = 444.f; /* default until changed */
  o->def_A4tuning = 444.f; /* default until changed */
  o->def_ratio = 1.f; /* default until changed */
  parse_level(o, 0, &nt, SCOPE_TOP);
  SGSSymtab_destroy(o->st);
}

static void group_events(OperatorEvent *to, int def_time_ms) {
  OperatorEvent *ge, *from = to->groupfrom, *until;
  int wait = 0, waitcount = 0;
  for (until = to->next;
       until && until->nestlevel;
       until = until->next) ;
  for (ge = from; ge != until; ge = ge->next) {
    if (ge->nestlevel)
      continue;
    if (ge->next == until && ge->time_ms < 0) /* Set and use default for last node in group */
      ge->time_ms = def_time_ms;
    if (wait < ge->time_ms)
      wait = ge->time_ms;
    if (ge->next) {
      //wait -= ge->next->wait_ms;
      waitcount += ge->next->wait_ms;
    }
  }
  for (ge = from; ge != until; ge = ge->next) {
    if (ge->time_ms < 0)
      ge->time_ms = wait + waitcount; /* fill in sensible default time */
    if (ge->next)
      waitcount -= ge->next->wait_ms;
  }
  to->groupfrom = 0;
  if (until)
    until->wait_ms += wait;
}

static void time_event(OperatorEvent *e) {
  /* Fill in blank valit durations */
  if (e->valitfreq.time_ms < 0)
    e->valitfreq.time_ms = e->time_ms;
  if (e->valitamp.time_ms < 0)
    e->valitamp.time_ms = e->time_ms;
  if (!e->nestlevel) {
    if (e->topop.valitpanning.time_ms < 0)
      e->topop.valitpanning.time_ms = e->time_ms;
  }
  if (e->time_ms >= 0 && !(e->parseflags & SILENCE_ADDED)) {
    e->time_ms += e->silence_ms;
    e->parseflags |= SILENCE_ADDED;
  }
  if (e->parseflags & ADD_WAIT_DURATION) {
    if (e->next)
      e->next->wait_ms += e->time_ms;
    e->parseflags &= ~ADD_WAIT_DURATION;
  }
}

static void flatten_events(OperatorEvent *e) {
  OperatorEvent *ce = e->composite;
  OperatorEvent *se = e->next, *se_prev = e;
  int wait_ms = 0;
  int added_wait_ms = 0;
  if (!ce)
    return;
  /* Flatten composites */
  do {
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
      OperatorEvent *ce_next = ce->next;
      se->wait_ms -= ce->wait_ms + added_wait_ms;
      added_wait_ms = 0;
      wait_ms = 0;
      se_prev->next = ce;
      se_prev = ce;
      se_prev->next = se;
      ce = ce_next;
    } else {
      OperatorEvent *se_next, *ce_next;
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
    }
  } while (ce);
  e->composite = 0;
}

static SGSProgram* build(SGSParser *o) {
  SGSProgram *prg = o->prg;
  OperatorEvent *e;
  SGSProgramEvent *oe;
  uint id;
  uint alloc = 0;
  /* Pass #1 - perform timing adjustments */
  for (e = o->events; e; e = e->next) {
    time_event(e);
    /* Handle composites (flatten in next loop) */
    if (e->composite) {
      OperatorEvent *ce = e->composite, *ce_prev = e;
      OperatorEvent *se = e->next;
      if (ce->time_ms < 0)
        ce->time_ms = o->def_time_ms;
      /* Timing for composites */
      do {
        if (ce->wait_ms) { /* Simulate delay with silence */
          ce->silence_ms += ce->wait_ms;
          ce->params |= SGS_SILENCE;
          if (se)
            se->wait_ms += ce->wait_ms;
          ce->wait_ms = 0;
        }
        ce->wait_ms += ce_prev->time_ms;
        if (ce->time_ms < 0)
          ce->time_ms = ce_prev->time_ms - ce_prev->silence_ms;
        time_event(ce);
        e->time_ms += ce->time_ms;
        ce_prev = ce;
        ce = ce->next;
      } while (ce);
    }
    /* Time |-terminated sequences */
    if (e->groupfrom)
      group_events(e, o->def_time_ms);
  }
  /* Pass #2 - flatten list */
  for (id = 0, e = o->events; e; e = e->next) {
    if (e->composite)
      flatten_events(e);
    e->id = id++;
  }
  /* Pass #3 - produce output */
  for (id = 0, e = o->events; e; ) {
    OperatorEvent *e_next = e->next;
    /* Add to final output list */
    if (id >= alloc) {
      if (!alloc) alloc = 1;
      alloc <<= 1;
      prg->events = realloc(prg->events, sizeof(SGSProgramEvent) * alloc);
    }
    oe = &prg->events[id];
    oe->opprevid = (e->opprev ? (int)e->opprev->id : -1);
    oe->opnextid = (e->opnext ? (int)e->opnext->id : -1);
    oe->type = e->type;
    oe->wait_ms = e->wait_ms;
    oe->e.params = e->params;
    if (oe->type == SGS_EVENT_VOICE) {
      oe->data.voice.id = e->voiceid;
      oe->data.voice.graph = 0;
      oe->data.voice.panning = e->topop.panning;
      oe->data.voice.valitpanning = e->topop.valitpanning;
    } else {
      oe->data.operator.id = e->opid;
      oe->data.operator.adjc = 0;
      oe->data.operator.attr = e->attr;
      oe->data.operator.wave = e->wave;
      oe->data.operator.time_ms = e->time_ms;
      oe->data.operator.silence_ms = e->silence_ms;
      oe->data.operator.freq = e->freq;
      oe->data.operator.dynfreq = e->dynfreq;
      oe->data.operator.phase = e->phase;
      oe->data.operator.amp = e->amp;
      oe->data.operator.dynamp = e->dynamp;
      oe->data.operator.valitfreq = e->valitfreq;
      oe->data.operator.valitamp = e->valitamp;
    }
    ++id;
    free(e);
    e = e_next;
  }
  prg->eventc = id;
#if 1
  /* Debug printing */
  oe = prg->events;
  putchar('\n');
  for (id = 0; id < prg->eventc; ++id) {
    oe = &prg->events[id];
    if (oe->type == SGS_EVENT_VOICE)
      printf("ev %d, vo %d: \t/=%d", id, oe->data.voice.id, oe->wait_ms);
    else
      printf("ev %d, op %d: \t/=%d \tt=%d\n", id, oe->data.operator.id, oe->wait_ms, oe->data.operator.time_ms);
  }
#else
  /* Debug printing */
  e = o->events;
  putchar('\n');
  do{
    printf("ev %d, op %d (%s): \t/=%d \tt=%d\n", e->id, e->opid, e->optype ? "nested" : "top", e->wait_ms, e->time_ms);
  } while ((e = e->next));
#endif

  return o->prg;
}

SGSProgram* SGSProgram_create(const char *filename) {
  SGSParser p;
  FILE *f = fopen(filename, "r");
  if (!f) return 0;

  parse(f, filename, &p);
  fclose(f);
  return build(&p);
}

void SGSProgram_destroy(SGSProgram *o) {
  uint i;
  for (i = 0; i < o->eventc; ++i) {
  }
  free(o->events);
}
