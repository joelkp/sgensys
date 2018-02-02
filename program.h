/* operator types */
enum {
  SGS_TYPE_TOP = 0,
  SGS_TYPE_NESTED,
};

/* operator parameters */
enum {
  /* operator linkage */
  SGS_PMOD = 1<<0,
  SGS_FMOD = 1<<1,
  SGS_AMOD = 1<<2,
  SGS_LINK = 1<<3,
  /* operator values */
  SGS_WAVE = 1<<4,
  SGS_TIME = 1<<5,
  SGS_SILENCE = 1<<6,
  SGS_FREQ = 1<<7,
  SGS_DYNFREQ = 1<<8,
  SGS_PHASE = 1<<9,
  SGS_AMP = 1<<10,
  SGS_DYNAMP = 1<<11,
  SGS_ATTR = 1<<12,
  /* top-operator-specific values */
  SGS_PANNING = 1<<13
};

/* operator wave types */
enum {
  SGS_WAVE_SIN = 0,
  SGS_WAVE_SQR,
  SGS_WAVE_TRI,
  SGS_WAVE_SAW
};

/* operator atttributes */
enum {
  SGS_ATTR_FREQRATIO = 1<<1,
  SGS_ATTR_DYNFREQRATIO = 1<<2,
  SGS_ATTR_VALITFREQ = 1<<3,
  SGS_ATTR_VALITFREQRATIO = 1<<4,
  SGS_ATTR_VALITAMP = 1<<5,
  SGS_ATTR_VALITPANNING = 1<<6
};

/* value iteration types */
enum {
  SGS_VALIT_NONE = 0, /* when none given */
  SGS_VALIT_LIN,
  SGS_VALIT_EXP,
  SGS_VALIT_LOG
};

typedef struct SGSProgramValit {
  int time_ms, pos_ms;
  float goal;
  uchar type;
} SGSProgramValit;

typedef struct SGSProgramEvent {
  struct SGSProgramEvent *next;
  struct SGSProgramEvent *lvnext;
  struct SGSProgramEvent *opprev, *opnext; /* linked list per topopid */
  struct SGSProgramEvent *composite; /* only used during parsing */
  int wait_ms;
  uint id;
  uint opid; /* counts up from 0 separately for different optypes */
  uchar optype;
  uchar opfirst;
  uint topopid; /* top operator for operator set */
  /* operator parameters possibly set: (-1 id = none) */
  ushort params;
  uchar attr;
  uchar wave;
  int time_ms, silence_ms;
  float freq, dynfreq, phase, amp, dynamp;
  SGSProgramValit valitfreq, valitamp;
  int pmodid, fmodid, amodid;
  int linkid;
  /* struct ends here if event not for top operator */
  struct SGSProgramEventExt {
    float panning;
    SGSProgramValit valitpanning;
  } topop;
} SGSProgramEvent;

struct SGSProgram {
  SGSProgramEvent *events;
  uint eventc;
  uint operatorc,
       topopc; /* top-level operators */
};
