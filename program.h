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
  SGS_PANNING = 1<<12,
  SGS_ATTR = 1<<13
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
  SGS_ATTR_DYNFREQRATIO = 1<<2
};

typedef struct SGSProgramEvent {
  struct SGSProgramEvent *next;
  struct SGSProgramEvent *lvnext;
  struct SGSProgramEvent *opprev, *opnext; /* linked list per topopid */
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
  float freq, dynfreq, phase, amp, dynamp, panning;
  int pmodid, fmodid, amodid;
  int linkid;
} SGSProgramEvent;

struct SGSProgram {
  SGSProgramEvent *events;
  uint eventc;
  uint operatorc,
       topopc; /* top-level operators */
};
