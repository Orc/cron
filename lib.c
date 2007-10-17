/*
 * misc functions
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <syslog.h>

#include "cron.h"

int interactive = 1;
int lineno;


/* (re)allocate memory or die.
 */
void*
xrealloc(void *elp, int nrel, int szel)
{
    void *ret;

    if (ret = elp ? realloc(elp, nrel*szel) : malloc(nrel*szel))
	return ret;

    fatal("xrealloc: %s", strerror(errno));
}


/* complain about something, sending the message either to stderr or
 * syslog, depending on whether (interactive) is set.
 */
static void
_error(char *fmt, va_list ptr)
{
    if (interactive) {
	vfprintf(stderr, fmt, ptr);
	fputc('\n',stderr);
    }
    else
	vsyslog(LOG_ERR, fmt, ptr);
}


/* complain about something
 */
void
error(char *fmt, ...)
{
    va_list ptr;

    va_start(ptr,fmt);
    _error(fmt,ptr);
    va_end(ptr);
}


/* complain about something, then die.
 */
void
fatal(char *fmt,...)
{
    va_list ptr;

    va_start(ptr,fmt);
    _error(fmt,ptr);
    va_end(ptr);
    exit(1);
}


/* get a logical line of unlimited size.  Convert unescaped
 * %'s into newlines, and concatenate lines together if escaped
 * with \ at eol.
 */
char*
fgetlol(FILE *f)
{
    static char *line = 0;
    static int szl = 0;
    int nrl = 0;
    register c;

    while ( (c = fgetc(f)) != EOF ) {
	EXPAND(line,szl,nrl);

	if ( c == '\\')  {
	    if ( (c = fgetc(f)) == EOF )
		break;
	    if (c == '\n') ++lineno;
	    line[nrl++] = c;
	    continue;
	}
	else if ( c == '%' )
	    c = '\n';
	else if ( c == '\n' ) {
	    ++lineno;
	    line[nrl] = 0;
	    return line;
	}

	line[nrl++] = c;
    }
    if (nrl) {
	line[nrl] = 0;
	return line;
    }
    return 0;
}


/* return the mtime of a file (or the current time of day if
 * the stat() failed to work
 */
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


/* erase the contents of a crontab, leaving the arrays allocated for
 * later use.
 */
void
zerocrontab(crontab *tab)
{
    int i;

    if (tab == 0) return;

    for (i=0; i < tab->nre; ++i)
	free(tab->env[i]);
    tab->nre = 0;

    for (i=0; i < tab->nrl; ++i) {
	free(tab->list[i].command);
	if (tab->list[i].input)
	    free(tab->list[i].input);
    }
    tab->nrl = 0;
}


/* eat leading blanks on a line
 */
char*
firstnonblank(char *s)
{
    while (*s && isspace(*s)) ++s;

    return s;
}
