#ifndef _CRON_H
#define _CRON_H

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"

/*
 * eventmask is
 * 60 bits for minute,
 * 31 bits for mday,
 * 24 bits for hour,
 * 12 bits for month,
 *  7 bits for wday
 */

#define CBIT(t)		(1 << (t))

typedef struct {
    DWORD minutes[2];	/* 60 bits worth */
    DWORD hours;	/* 24 bits worth */
    DWORD mday;		/* 31 bits worth */
    WORD  wday;		/*  7 bits worth */
    WORD  month;	/* 12 bits worth */
} Evmask;


typedef struct {
    Evmask trigger;
    char *command;
    char *input;
} cron;


typedef struct {
    uid_t user;		/* user who owns the crontab */
    time_t mtime;	/* when the crontab was changed */
    int flags;		/* various flags */
#define ACTIVE	0x01
    char **env;		/* passed-in environment entries */
    int nre;
    int sze;
    cron *list;		/* the entries */
    int nrl;
    int szl;
} crontab;


int readcrontab(crontab *, FILE*);
void zerocrontab(crontab *);
void tmtoEvmask(struct tm *,int,Evmask*);
time_t mtime(char *);
void runjob(crontab *, cron *);

#define EXPAND(t,s,n)	do { \
			    int _n = (n); \
			    int _s = (s); \
			    if (_n >= _s) { \
				_s = _n ? (_n * 10) : 200; \
				t = xrealloc(t, _s, sizeof t[0]); \
			    } \
			} while(0)

void *xrealloc(void*,int,int);
char *fgetlol(FILE*);
char *firstnonblank(char*);
void error(char*,...);
void fatal(char*,...);
void info(char*,...);
char *jobenv(crontab*,char*);
void xchdir(char*);


extern int interactive;
extern int lineno;

#endif
