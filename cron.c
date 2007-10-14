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

int    interactive = 1;

crontab *tabs = 0;
int    nrtabs = 0;
int    sztabs = 0;

#define EXPAND(t,s,n)	if (n >= s) { \
			    s = n ? (n * 10) : 200; \
			    t = t ? realloc(t, s * (sizeof t[0]) ) \
				  :    malloc( s * (sizeof t[0]) ); }

typedef void (*ef)(Evmask*,int);

typedef struct {
    int min;
    int max;
    char *units;
    ef setter;
} constraint;

void
sminute(Evmask *mask, int min)
{
    if (min >= 32)
	mask->minutes[1] |= 1 << (min-32);
    else
	mask->minutes[0] |= 1 << (min);
}

void
shour(Evmask *mask, int hour)
{
    mask->hours |= 1 << (hour);
}

void
smday(Evmask *mask, int mday)
{
    mask->mday |= 1 << (mday);
}

void
smonth(Evmask *mask, int month)
{
    mask->month |= 1 << (month);
}

void
swday(Evmask *mask, int wday)
{
    if (wday == 0) wday = 7;
    mask->wday |= 1 << (wday);
}


constraint minutes = { 0, 59, "minutes", sminute };
constraint hours =   { 0, 23, "hours", shour };
constraint mday =    { 0, 31, "day of the month", smday };
constraint months =  { 1, 12, "months",  smonth };
constraint wday =    { 0,  7, "day of the week", swday };

char*
firstnonblank(char *s)
{
    while (*s && isspace(*s)) ++s;

    return s;
}


void
error(char *fmt, ...)
{
    va_list ptr;

    va_start(ptr,fmt);
    if (interactive) {
	vfprintf(stderr, fmt, ptr);
	fputc('\n',stderr);
    }
    else
	vsyslog(LOG_ERR, fmt, ptr);
    va_end(ptr);
}


char*
fgetlol(FILE *f)
{
    static char *line = 0;
    static int szl = 0;
    int nrl = 0;
    register c;

    while ( (c = fgetc(f)) != EOF ) {
	EXPAND(line,szl,nrl)

	if ( c == '\n' ) {
	    if (nrl && (line[nrl-1] == '\\')) {
		line[nrl-1] = c;
		continue;
	    }
	    else {
		line[nrl] = 0;
		return line;
	    }
	}
	line[nrl++] = c;
    }
    if (nrl) {
	line[nrl] = 0;
	return line;
    }
    return 0;
}


int
constrain(int num, constraint *limit)
{
    return (num >= limit->min) || (num <= limit->max);
}


int
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


void
assign(int time, Evmask *mask, ef setter)
{
    (*setter)(mask, time);
}


char *
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


void
anotherjob(crontab *tab, cron *job)
{
    EXPAND(tab->list, tab->sze, tab->nre);

    tab->list[tab->nre++] = *job;
}



void
readcrontab(char *file)
{
    struct stat st;
    FILE *f;
    char *s;
    struct passwd *user;
    cron job;
    crontab ct;


    if (stat(file,&st) != 0)
	return;

    if ((user = getpwnam(file)) == 0 )
	return;

    if (st.st_uid != 0) {
	error("crontab for %s is not owned by root", file);
	return;
    }

    if ( st.st_mode & (S_IWOTH|S_IWGRP|S_IROTH|S_IRGRP) ) {
	error("bad permissions for crontab %s", file);
	return;
    }

    if ( !S_ISREG(st.st_mode) ) {
	error("crontab for %s in not a regular file", file);
	return;
    }

    if ( (f = fopen(file,"r")) == 0 ) {
	error("can't open crontab for %s: %s", file, strerror(errno));
	return;
    }

    bzero(&ct, sizeof ct);
    ct.user = user->pw_uid;
    time(&ct.age);

    while ( s = fgetlol(f) ) {
	s = firstnonblank(s);

	if (*s == 0 || *s == '#' || *s == '\n')
	    continue;

	if (isalpha(*s)) /* vixie-style environment variable assignment */
	    continue;

	if (s = getdatespec(s, &job)) {
	    job.command = strdup(s);
	    anotherjob(&ct, &job);
	}
    }
    EXPAND(tabs,sztabs,nrtabs);
    tabs[nrtabs++] = ct;
}


int
securepath(char *path)
{
    char *line = alloca(strlen(path)+1);
    char *p, *part;
    struct stat sb;


    if (line == 0) {
	error("%s: %m", path, strerror(errno));
	return 0;
    }
    strcpy(line, path);

    /* check each part of the path */
    while ( (p = strrchr(line, '/')) ) {
	*p = 0;

	part = line[0] ? line : "/";

	if ( stat(part, &sb) != 0 ) { error("%s: can't stat", part); return 0; }
	if ( sb.st_uid != 0 ) { error("%s: not user 0", part); return 0; }
	if ( sb.st_gid != 0 ) { error("%s: not group 0", part); return 0; }
	if ( !S_ISDIR(sb.st_mode) ) { error("%s: not a directory", part); return 0; }
	if ( sb.st_mode & (S_IWGRP|S_IWOTH) ) { error("%s: group or world writable", part); return 0; }
    }
    return 1;
}


void
printtrig(Evmask *m)
{
    printf("%08x%08x ", m->minutes[0], m->minutes[1]);
    printf("%08x ", m->hours);
    printf("%08x ", m->mday);
    printf("%04x ", m->month);
    printf("%04x ", m->wday);
}


int
triggered(Evmask *t, Evmask *m)
{
    if ( ( (t->minutes[0] & m->minutes[0]) || (t->minutes[1] & m->minutes[1]) )
	&& (t->hours & m->hours)
	&& (t->mday & m->mday)
	&& (t->month & m->month)
	&& (t->wday & m->wday) ) return 1;
    return 0;
}


main(int argc, char **argv)
{
    DIR *d;
    struct dirent *ent;
    time_t ticks;
    struct tm *clock;
    Evmask Now;
    int i;
    int interval = 5;

    if ( argc > 1)
	interval = atoi(argv[1]);

    if (interval > 60) {
	error("there are only 60 minutes to the hour.");
	exit(1);
    }
    else if (interval < 1)
	interval = 1;

    time(&ticks);
    clock = localtime(&ticks);
    bzero(&Now, sizeof Now);

    if (interval > 1)
	clock->tm_min -= clock->tm_min % interval;

    for (i=0; i < 5; i++)
	if (clock->tm_min + i < 60)
	    sminute(&Now, clock->tm_min+i);

    shour(&Now, clock->tm_hour);
    smday(&Now, clock->tm_mday);
    smonth(&Now,clock->tm_mon);
    swday(&Now, clock->tm_wday);

    printf("----");printtrig(&Now); printf("Current time\n");


    if (chdir("/var/spool/cron/crontabs") != 0) {
	error("can't chdir into crontabs: %s", strerror(errno) );
	exit(1);
    }
    if (!securepath("/var/spool/cron/crontabs"))
	exit(1);

    if ( d = opendir(".") ) {
	while (ent = readdir(d))
	    readcrontab(ent->d_name);
	closedir(d);
    }
    {   int i, j;
	for (i=0; i < nrtabs; i++) {
	    if (tabs[i].nre ) {
		struct passwd *pwd = getpwuid(tabs[i].user);

		if (pwd) {
		    printf("crontab for %s (id %d.%d):\n", pwd->pw_name, pwd->pw_uid, pwd->pw_gid);
		    for (j=0; j<tabs[i].nre; j++) {
			if ( triggered(&Now, &(tabs[i].list[j].trigger)) )
			    printf("--->");
			else
			    printf("    ");
			printtrig( &tabs[i].list[j].trigger);
			printf("%s\n", tabs[i].list[j].command);
		    }
		}
	    }
	}

    }
    exit(0);
}
