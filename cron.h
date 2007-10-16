#ifndef _CRON_H
#define _CRON_H

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#ifndef CRONDIR
#define CRONDIR "/var/spool/cron/crontabs"
#endif

/*
 * eventmask is
 * 60 bits for minute, 31 bits for mday, 24 bits for hour, 12 bits for month, and
 * 7 bits for wday
 */
/* assume sizeof(long long) >= 8, sizeof(int) >= 4, sizeof(short) >= 2 */
#define CBIT(t)		(1 << (t))

typedef struct {
    unsigned long minutes[2];	/* 60 bits worth */
    unsigned int hours;		/* 24 bits worth */
    unsigned int mday;		/* 31 bits worth */
    unsigned short wday;	/*  7 bits worth */
    unsigned short month;	/* 12 bits worth */
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


void readcrontab(crontab *, FILE*);
void zerocrontab(crontab *);
void tmtoEvmask(struct tm *,int,Evmask*);
time_t mtime(char *);
void runjob(crontab *, int);

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
void error(char*,...);
void fatal(char*,...);

#endif
