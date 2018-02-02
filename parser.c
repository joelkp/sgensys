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
  uint nestedc;
  SGSProgramNode *nested; /* list added to end of top nodes at end of parsing */
  SGSProgramNode *last_top, *last_nested;
  SGSProgramNode *undo_last;
  /* settings/ops */
  uchar n_mode;
  float n_ampmult;
  float n_time;
  float n_freq, n_ratio;
} SGSParser;

/* things that need to be separate for each nested parse_level() go here */
typedef struct NodeData {
  SGSProgramNode *node; /* state for tentative node until end_node() */
  SGSProgramNodeChain *target;
  SGSProgramNode *last;
  char *setsym;
  /* timing/delay */
  SGSProgramNode *n_begin;
  uchar n_end;
  uchar n_time_delay;
  float n_add_delay; /* added to node's delay in end_node */
  float n_next_add_delay;
} NodeData;

static void end_node(SGSParser *o, NodeData *nd);

static void new_node(SGSParser *o, NodeData *nd, SGSProgramNodeChain *target, uchar type) {
  SGSProgram *p = o->prg;
  SGSProgramNode *n;
  end_node(o, nd);
  n = nd->node = calloc(1, sizeof(SGSProgramNode));
  nd->target = target;
  n->type = type;
  /* defaults */
  n->amp = 1.f;
  n->mode = o->n_mode;
  if (type == SGS_TYPE_TOP)
    n->time = -1.f; /* set later */
  else if (type == SGS_TYPE_NESTED)
    n->time = o->n_time;
  n->freq = o->n_freq;

  /* tentative linking */
  if (!nd->target) {
    if (!p->nodelist)
      p->nodelist = n;
    else
      o->last_top->next = n;
  } else {
    if (!o->nested)
      o->nested = n;
    else
      o->last_nested->next = n;
    n->id = o->nestedc++;
  }

  /* prepare timing adjustment */
  nd->n_add_delay += nd->n_next_add_delay;
  if (nd->n_time_delay) {
    if (o->last_top)
      nd->n_add_delay += o->last_top->time;
    nd->n_time_delay = 0;
  }
  nd->n_next_add_delay = 0.f;

  if (nd->target) {
    o->undo_last = o->last_nested;
    o->last_nested = n; /* can't wait due to recursion for nesting */
  }
}

static void end_node(SGSParser *o, NodeData *nd) {
  SGSProgram *p = o->prg;
  SGSProgramNode *n = nd->node;
  if (!n)
    return; /* nothing to do */
  nd->node = 0;
  if (n->type == SGS_TYPE_SETTOP ||
      n->type == SGS_TYPE_SETNESTED) {
    /* check what the set-node changes */
    SGSProgramNode *ref = n->spec.set.ref;
    /* SGS_TIME set when time set */
    if (n->freq != ref->freq)
      n->spec.set.values |= SGS_FREQ;
    if (n->dynfreq != ref->dynfreq)
      n->spec.set.values |= SGS_DYNFREQ;
    if (n->phase != ref->phase)
      n->spec.set.values |= SGS_PHASE;
    if (n->amp != ref->amp)
      n->spec.set.values |= SGS_AMP;
    if (n->dynamp != ref->dynamp)
      n->spec.set.values |= SGS_DYNAMP;
    if (n->attr != ref->attr)
      n->spec.set.values |= SGS_ATTR;
    if (n->amod.chain != ref->amod.chain)
      n->spec.set.mods |= SGS_AMODS;
    if (n->fmod.chain != ref->fmod.chain)
      n->spec.set.mods |= SGS_FMODS;
    if (n->pmod.chain != ref->pmod.chain)
      n->spec.set.mods |= SGS_PMODS;

    if (!n->spec.set.values && !n->spec.set.mods) {
      /* Remove no-operation set node; made simpler
       * by all set nodes being top nodes.
       */
      if (o->last_nested == n)
        o->last_nested = o->undo_last;
      if (o->last_top == n)
        o->last_top->next = 0;
      free(n);
      return;
    }
  }

  if (!nd->target) {
    n->flag |= SGS_FLAG_EXEC;
    o->last_top = n;
    n->id = p->topc++;
  } else {
    if (!nd->target->chain)
      nd->target->chain = n;
    else
      nd->last->spec.nested.link = n;
    ++nd->target->count;
    /*o->last_nested = n;*/ /* already done */
  }
  nd->last = n;
  ++p->nodec;

  if (n->type != SGS_TYPE_NESTED) /* don't mess with modulation depth */
    n->amp *= o->n_ampmult;
  /* node-to-| sequence timing */
  if (!nd->n_begin)
    nd->n_begin = n;
  else if (nd->n_end) {
    double delay = 0.f, delaycount = 0.f;
    SGSProgramNode *step;
    for (step = nd->n_begin; step != n; step = step->next) {
      if (step->next == n && step->time < 0.f)
        step->time = o->n_time; /* set and use default for last node in group */
      if (delay < step->time)
        delay = step->time;
      delay -= step->next->delay;
      delaycount += step->next->delay;
    }
    for (step = nd->n_begin; step != n; step = step->next) {
      if (step->time < 0.f)
        step->time = delay + delaycount; /* fill in sensible default time */
      delaycount -= step->next->delay;
    }
    nd->n_add_delay += delay;
    nd->n_begin = n;
    nd->n_end = 0;
  }
  n->delay += nd->n_add_delay;
  nd->n_add_delay = 0.f;

  if (nd->setsym) {
    SGSSymtab_set(o->st, nd->setsym, n);
    free(nd->setsym);
    nd->setsym = 0;
  }
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

static void parse_level(SGSParser *o, SGSProgramNodeChain *chain, uchar modtype);

static SGSProgram* parse(FILE *f, const char *fn, SGSParser *o) {
  memset(o, 0, sizeof(SGSParser));
  o->f = f;
  o->fn = fn;
  o->prg = calloc(1, sizeof(SGSProgram));
  o->st = SGSSymtab_create();
  o->line = 1;
  o->n_mode = SGS_MODE_CENTER; /* default until changed */
  o->n_ampmult = 1.f; /* default until changed */
  o->n_time = 1.f; /* default until changed */
  o->n_freq = 100.f; /* default until changed */
  o->n_ratio = 1.f; /* default until changed */
  parse_level(o, 0, 0);
  SGSSymtab_destroy(o->st);
  /* concatenate linked lists */
  if (o->last_top)
    o->last_top->next = o->nested;
  return o->prg;
}

static void parse_level(SGSParser *o, SGSProgramNodeChain *chain, uchar modtype) {
  char c;
  NodeData nd;
  uint entrylevel = o->level;
  ++o->reclevel;
  memset(&nd, 0, sizeof(NodeData));
  if (chain) {
    chain->count = 0;
    chain->chain = 0;
  }
  while ((c = getc(o->f)) != EOF) {
    eatws(o->f);
    switch (c) {
    case '\n':
    EOL:
      if (!chain) {
        if (o->setdef > o->level)
          o->setdef = (o->level) ? (o->level - 1) : 0;
        else if (o->setnode > o->level) {
          o->setnode = (o->level) ? (o->level - 1) : 0;
          end_node(o, &nd);
        }
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
      if (o->setdef > o->setnode) goto INVALID;
      if (testgetc('t', o->f))
        nd.n_time_delay = 1;
      else {
        nd.n_time_delay = 0;
        nd.n_next_add_delay += getnum(o->f);
      }
      break;
    case '{':
      /* is always got elsewhere before a nesting call to this function */
      warning(o, "opening curly brace out of place", c);
      break;
    case '}':
      if (!chain)
        goto INVALID;
      if (o->level != entrylevel) {
        o->level = entrylevel;
        warning(o, "closing '}' before closing '>'s", c);
      }
      goto RETURN;
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
        end_node(o, &nd);
      }
      --o->level;
      break;
    case 'C':
      o->n_mode = SGS_MODE_CENTER;
      break;
    case 'E':
      new_node(o, &nd, 0, SGS_TYPE_ENV);
      o->setnode = o->level + 1;
      break;
    case 'L':
      o->n_mode = SGS_MODE_LEFT;
      break;
    case 'Q':
      goto FINISH;
    case 'R':
      o->n_mode = SGS_MODE_RIGHT;
      break;
    case 'S':
      o->setdef = o->level + 1;
      break;
    case 'W': {
      const char *simples[] = {
        "sin",
        "sqr",
        "tri",
        "saw",
        0
      };
      int wave;
      wave = strfind(o->f, simples) + SGS_WAVE_SIN;
      if (wave < SGS_WAVE_SIN) {
        warning(o, "invalid wave type follows W in file; sin, sqr, tri, saw available", c);
        break;
      }
      new_node(o, &nd, chain, (chain ? SGS_TYPE_NESTED : SGS_TYPE_TOP));
      nd.node->wave = wave;
      o->setnode = o->level + 1;
      break; }
    case '|':
      end_node(o, &nd);
      if (!nd.n_begin)
        warning(o, "end of sequence before any parts given", c);
      else
        nd.n_end = 1;
      break;
    case '\\':
      if (o->setdef > o->setnode)
        goto INVALID;
      else
        nd.node->delay += getnum(o->f);
      break;
    case '\'':
      end_node(o, &nd);
      if (nd.setsym) {
        warning(o, "ignoring label assignment to label assignment", c);
        break;
      }
      read_sym(o, &nd.setsym, '\'');
      break;
    case ':':
      end_node(o, &nd);
      if (nd.setsym)
        warning(o, "ignoring label assignment to label reference", c);
      else if (chain)
        goto INVALID;
      if (read_sym(o, &nd.setsym, ':')) {
        SGSProgramNode *ref = SGSSymtab_get(o->st, nd.setsym);
        if (!ref)
          warning(o, "ignoring reference to undefined label", c);
        else {
          uint type;
          switch (ref->type) {
          case SGS_TYPE_TOP:
          case SGS_TYPE_SETTOP:
            type = SGS_TYPE_SETTOP;
            break;
          case SGS_TYPE_NESTED:
          case SGS_TYPE_SETNESTED:
            type = SGS_TYPE_SETNESTED;
            break;
          default:
            type = 0; /* silence warning */
          }
          new_node(o, &nd, 0, type);
          nd.node->spec.set.ref = ref;
          nd.node->wave = ref->wave;
          nd.node->mode = ref->mode;
          nd.node->amp = ref->amp;
          nd.node->dynamp = ref->dynamp;
          nd.node->freq = ref->freq;
          nd.node->dynfreq = ref->dynfreq;
          nd.node->attr = ref->attr;
          nd.node->pmod = ref->pmod;
          nd.node->fmod = ref->fmod;
          nd.node->amod = ref->amod;
          o->setnode = o->level + 1;
        }
      }
      break;
    case 'a':
      if (o->setdef > o->setnode)
        o->n_ampmult = getnum(o->f);
      else if (o->setnode > 0) {
        if (modtype == SGS_AMODS ||
            modtype == SGS_FMODS)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.node->dynamp = getnum(o->f);
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->amod, SGS_AMODS);
          }
        } else {
          nd.node->amp = getnum(o->f);
        }
      } else
        goto INVALID;
      break;
    case 'f':
      if (o->setdef > o->setnode)
        o->n_freq = getnum(o->f);
      else if (o->setnode > 0) {
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.node->dynfreq = getnum(o->f);
            nd.node->attr &= ~SGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->fmod, SGS_FMODS);
          }
        } else {
          nd.node->freq = getnum(o->f);
          nd.node->attr &= ~SGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 'p': {
      if (o->setdef > o->setnode || o->setnode <= 0)
        goto INVALID;
      if (testgetc('!', o->f)) {
        if (testgetc('{', o->f)) {
          parse_level(o, &nd.node->pmod, SGS_PMODS);
        }
      } else {
        nd.node->phase = fmod(getnum(o->f), 1.f);
        if (nd.node->phase < 0.f)
          nd.node->phase += 1.f;
      }
      break; }
    case 'r':
      if (o->setdef > o->setnode)
        o->n_ratio = 1.f / getnum(o->f);
      else if (o->setnode > 0) {
        if (!chain)
          goto INVALID;
        if (testgetc('!', o->f)) {
          if (!testc('{', o->f)) {
            nd.node->dynfreq = 1.f / getnum(o->f);
            nd.node->attr |= SGS_ATTR_DYNFREQRATIO;
          }
          if (testgetc('{', o->f)) {
            parse_level(o, &nd.node->fmod, SGS_FMODS);
          }
        } else {
          nd.node->freq = 1.f / getnum(o->f);
          nd.node->attr |= SGS_ATTR_FREQRATIO;
        }
      } else
        goto INVALID;
      break;
    case 't':
      if (o->setdef > o->setnode)
        o->n_time = getnum(o->f);
      else if (o->setnode > 0) {
        nd.node->time = getnum(o->f);
        if (nd.node->type == SGS_TYPE_SETTOP ||
            nd.node->type == SGS_TYPE_SETNESTED)
          nd.node->spec.set.values |= SGS_TIME;
      } else
        goto INVALID;
      break;
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
  if (nd.node) {
    if (nd.node->time < 0.f)
      nd.node->time = o->n_time; /* use default */
    nd.n_end = 1; /* end grouping if any */
    end_node(o, &nd);
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
  SGSProgramNode *n = o->nodelist;
  while (n) {
    SGSProgramNode *nn = n->next;
    free(n);
    n = nn;
  }
}
