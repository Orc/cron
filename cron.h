#ifndef _CRON_H
#define _CRON_H

#include <unistd.h>
#include <sys/types.h>

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
} cron;


typedef struct {
    uid_t user;		/* user who owns the crontab */
    time_t age;		/* how old is the crontab */
    int nre;
    int sze;
    cron *list;		/* the entries */
} crontab;

#endif
