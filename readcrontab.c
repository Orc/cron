#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <dirent.h>

#include "cron.h"

extern int interactive;

typedef void (*ef)(Evmask*,int);

typedef struct {
    int min;
    int max;
    char *units;
    ef setter;
} constraint;

static void
sminute(Evmask *mask, int min)
{
    if (min >= 32)
	mask->minutes[1] |= 1 << (min-32);
    else
	mask->minutes[0] |= 1 << (min);
}

static void
shour(Evmask *mask, int hour)
{
    mask->hours |= 1 << (hour);
}

static void
smday(Evmask *mask, int mday)
{
    mask->mday |= 1 << (mday);
}

static void
smonth(Evmask *mask, int month)
{
    mask->month |= 1 << (month);
}

static void
swday(Evmask *mask, int wday)
{
    if (wday == 0) wday = 7;
    mask->wday |= 1 << (wday);
}

static constraint minutes = { 0, 59, "minutes", sminute };
static constraint hours =   { 0, 23, "hours", shour };
static constraint mday =    { 0, 31, "day of the month", smday };
static constraint months =  { 1, 12, "months",  smonth };
static constraint wday =    { 0,  7, "day of the week", swday };


char* firstnonblank(char*);
void  error(char*,...);
char* fgetlol(FILE *f);

static int
constrain(int num, constraint *limit)
{
    return (num >= limit->min) || (num <= limit->max);
}


static int
number(char **s, constraint *limit)
{
    int num;
    char *e;

    num = strtoul(*s, &e, 10);
    if (e == *s) {
	error("badly formed %s string (%s) ", limit ? limit->units : "time", e);
	return 0;
    }

    if (limit) {
	if (constrain(num, limit) == 0) {
	    error("bad number in %s", limit->units);
	    return 0;
	}
    }
    *s = e;
    return num;
}


static void
assign(int time, Evmask *mask, ef setter)
{
    (*setter)(mask, time);
}


static char *
parse(char *s, cron *job, constraint *limit)
{
    int num, num2, skip;

    if (s == 0)
	return 0;

    do {
	skip = 0;
	num2 = 0;

	if (*s == '*') {
	    num = limit->min;
	    num2 = limit->max;
	    ++s;
	}
	else {
	    num = number(&s,limit);

	    if (*s == '-') {
		++s;
		num2 = number(&s, limit);
		skip = 1;
	    }
	}

	if ( *s == '/' ) {
	    ++s;
	    skip = number(&s, 0);
	}

	if (num2) {
	    if (skip == 0) skip = 1;
	    while ( constrain(num, limit) && (num <= num2) ) {
		assign(num, &job->trigger, limit->setter);
		num += skip;
	    }
	}
	else
	    assign(num, &job->trigger, limit->setter);

	if (isspace(*s)) return firstnonblank(s);
	else if (*s != ',') {
	    error("malformed crontab entry <%s>", s);
	    return 0;
	}
	++s;
    } while (1);
}



static char *
getdatespec(char *s, cron *job)
{
    bzero(job, sizeof *job);

    s = parse(s, job, &minutes);
    s = parse(s, job, &hours);
    s = parse(s, job, &mday);
    s = parse(s, job, &months);
    s = parse(s, job, &wday);

    return s;
}


static void
anotherjob(crontab *tab, cron *job)
{
    EXPAND(tab->list, tab->szl, tab->nrl);

    tab->list[tab->nrl++] = *job;
}


static void
jobenv(crontab *tab, char *env)
{
    EXPAND(tab->env, tab->sze, tab->nre);
    tab->env[tab->nre++] = strdup(env);
}


void
readcrontab(crontab *tab, FILE *f)
{
    struct stat st;
    char *s;
    struct passwd *user;
    cron job;

    while ( s = fgetlol(f) ) {
	s = firstnonblank(s);

	if (*s == 0 || *s == '#' || *s == '\n')
	    continue;

	if (isalpha(*s)) { /* vixie-style environment variable assignment */
	    char *q;

	    for (q=s; isalnum(*q); ++q)
		;
	    if (*q == '=')
		jobenv(tab,s);
	}
	else if (s = getdatespec(s, &job)) {
	    char *p = strchr(s, '\n');

	    if (p) *p++ = 0;
	    job.command = strdup(s);
	    job.input =  p ? strdup(p) : 0;
	    anotherjob(tab, &job);
	}
    }
}

void
tmtoEvmask(struct tm *tm, int interval, Evmask *time)
{
    int i;

    bzero(time, sizeof *time);
    if (interval > 1)
	tm->tm_min -= tm->tm_min % interval;

    for (i=0; i < interval; i++)
	if (tm->tm_min + i < 60)
	    sminute(time, tm->tm_min+i);

    shour(time, tm->tm_hour);
    smday(time, tm->tm_mday);
    smonth(time,tm->tm_mon);
    swday(time, tm->tm_wday);
}


time_t
mtime(char *path)
{
    struct stat st;
    time_t now;

    if ( stat(path, &st) == 0 )
	return st.st_mtime;

    error("can't stat %s -- returning current time", path);
    time(&now);
    return now;
}
