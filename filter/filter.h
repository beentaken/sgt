#ifndef FILTER_FILTER_H
#define FILTER_FILTER_H

#define lenof(x) ( sizeof((x)) / sizeof(*(x)) )

#define FALSE 0
#define TRUE 1

typedef struct tstate tstate;

int pty_get(char *name);

/* Return values from tstate_option */
#define OPT_UNKNOWN -1
#define OPT_SPURIOUSARG -2
#define OPT_MISSINGARG -3
#define OPT_OK 0

tstate *tstate_init(void);
int tstate_option(tstate *state, int shortopt, char *longopt, char *value);
void tstate_argument(tstate *state, char *arg);
void tstate_ready(tstate *state, double *idelay, double *odelay);
char *translate(tstate *state, char *data, int inlen, int *outlen,
		double *delay, int input);
void tstate_done(tstate *state);

#endif
