#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>

#include "cron.h"

int    interactive = 1;

crontab *tabs = 0;
int    nrtabs = 0;
int    sztabs = 0;

static int ct_add, ct_update, ct_inact;


static void
eat()
{
    int status;

    while (waitpid(0, &status, WNOHANG) != -1)
	;
}


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
	exit(1);
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
	EXPAND(line,szl,nrl);

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
cmptabs(const void *c1, const void *c2)
{
    crontab *a = (crontab*)c1,
	    *b = (crontab*)c2;

    return a->user - b->user;
}


void
process(char *file)
{
    struct stat st;
    FILE *f;
    struct passwd *user;
    crontab tab, *ent;

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

    tab.user = user->pw_uid;

    ent = bsearch(&tab, tabs, nrtabs, sizeof tabs[0], cmptabs);

    if (ent) {
	if (ent->mtime == st.st_mtime) {
	    if (ent->nrl) ent->flags |= ACTIVE;
	    return;
	}
	zerocrontab(ent);
	ct_update++;
    }
    else {
	EXPAND(tabs,sztabs,nrtabs);
	ent = &tabs[nrtabs++];
	bzero(ent, sizeof tabs[0]);
	ent->user = user->pw_uid;
	ct_add++;
    }

    if ( f = fopen(file,"r") ) {
	readcrontab(ent, f);
	if (ent->nrl) ent->flags |= ACTIVE;
	ent->mtime = st.st_mtime;
	fclose(f);
    }
    else
	error("can't open crontab for %s: %s", file, strerror(errno));
}


void
zerocrontab(crontab *tab)
{
    int i;

    if (tab == 0) return;

    for (i=0; i < tab->nre; ++i)
	free(tab->env[i]);
    tab->nre = 0;

    for (i=0; i < tab->nrl; ++i)
	free(tab->list[i].command);
    tab->nrl = 0;
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


#if DEBUG
void
printcrontab(crontab *tab, int nrtab)
{
    int i, j;

    for (i=0; i < nrtab; i++)
	if (tabs[i].nrl ) {
	    struct passwd *pwd = getpwuid(tabs[i].user);

	    if (pwd) {
		printf("crontab for %s (id %d.%d):\n", pwd->pw_name, pwd->pw_uid, pwd->pw_gid);
#if 0
		for (j=0; j<tabs[i].nre; j++)
		    printf("%s\n", tabs[i].env[j]);
#endif
		for (j=0; j<tabs[i].nrl; j++) {
		    printf("    ");
		    printtrig( &tabs[i].list[j].trigger);
		    printf("%s\n", tabs[i].list[j].command);
		}
	    }
	}
}
#else
#define printcrontab(t,s)	1
#endif


void
scanctdir()
{
    DIR *d;
    struct dirent *ent;
    int i;

    for (i=0; i < nrtabs; i++)
	tabs[i].flags &= ~ACTIVE;

    ct_add = ct_update = ct_inact = 0;

    if ( d = opendir(".") ) {
	while (ent = readdir(d))
	    process(ent->d_name);
	closedir(d);
	qsort(tabs, nrtabs, sizeof tabs[0], cmptabs);
    }
    for (i=0; i < nrtabs; i++)
	if ( !(tabs[i].flags&ACTIVE) ) ct_inact++;
}


main(int argc, char **argv)
{
    time_t ticks;
    Evmask Now;
    int i, j;
    int interval = 1;
    int left;
    struct stat st;
    time_t ct_dirtime;

    if ( argc > 1)
	interval = atoi(argv[1]);

    if (interval > 60) {
	error("there are only 60 minutes to the hour.");
	exit(1);
    }
    else if (interval < 1)
	interval = 1;


    if (chdir(CRONDIR) != 0) {
	error("can't chdir into crontabs: %s", strerror(errno) );
	exit(1);
    }
    if (!securepath(CRONDIR))
	exit(1);

    openlog("cron", 0, LOG_CRON);

    ct_dirtime = 0;

    signal(SIGCHLD, eat);

    while (1) {
	time_t newtime = mtime(".");

	if (newtime != ct_dirtime) {
	    scanctdir();
#if DEBUG
	    printf("scanctdir:  time WAS %s", ctime(&ct_dirtime));
	    printf("                  IS %s", ctime(&newtime));
	    printf("total %d, added %d, updated %d, active %d\n", nrtabs, ct_add, ct_update, nrtabs - ct_inact);
#endif
	    ct_dirtime = newtime;
	    printcrontab(tabs,nrtabs);
	}

	time(&ticks);
	tmtoEvmask(localtime(&ticks),interval,&Now);

#if DEBUG
	printf("run Evmask:");printtrig(&Now); putchar('\n');
#endif

	for (i=0; i < nrtabs; i++)
	    for (j=0; j < tabs[i].nrl; j++)
		if ( (tabs[i].flags & ACTIVE) && triggered(&Now, &(tabs[i].list[j].trigger)) )
		    runjob(&tabs[i], j);

	for (left = 60 * interval; left > 0; left = sleep(left))
	    ;
    }
}
