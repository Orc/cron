/*
 * run a job
 */
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>

#include "cron.h"

/* pull a MAILTO value out of the job environment, if any
 * was defined
 */
static char *
mailto(crontab *tab)
{
    int i;

    for (i=0; i < tab->nre; ++i)
	if ( strncmp(tab->env[i], "MAILTO=", 7) == 0 )
	    return tab->env[i] + 7;
    return 0;
}


/* run a job, mail output (if any) to the user.
 */
void
runjob(crontab *tab, int job)
{
    pid_t pid;
    int io[2];
    struct passwd *pwd;

    if ( (pwd = getpwuid(tab->user)) == 0 ) {
	/* if we can't find a password entry for this crontab, 
	 * touch mtime so that it will be rescanned the next
	 * time cron checks the CRONDIR for new crontabs.
	 */
	error("no password entry for user %d\n", tab->user);
	time(&tab->mtime);
	return;
    }

#if DEBUG
    printtrig(&tab->list[job].trigger);
    printf("%s", tab->list[job].command);
    if (tab->list[job].input)
	printf("<< \\EOF\n%sEOF\n", tab->list[job].input);
    else
	putchar('\n');
#endif

    if ( (pid = fork()) == -1 ) { error("fork(): %s", strerror(errno)); return; }

    if (pid > 0) return;


    /* from this point on, we're the child process and should fatal() if anything
     * goes wrong
     */

    if ( setregid(pwd->pw_gid, pwd->pw_gid) == -1)
	fatal("setregid(%d,%d): %s", pwd->pw_gid, pwd->pw_gid, strerror(errno));
    if ( setreuid(pwd->pw_uid, pwd->pw_uid) == -1)
	fatal("setreuid(%d,%d): %s", pwd->pw_uid, pwd->pw_uid, strerror(errno));

    if ( chdir(pwd->pw_dir ? pwd->pw_dir : "/tmp") == -1 )
	fatal("chdir(\"%s\"): %s", pwd->pw_dir ? pwd->pw_dir : "/tmp", strerror(errno));

    if ( pipe(io) == -1 ) fatal("pipe: %s", strerror(errno));

    pid = fork();

    if (pid == -1) fatal("fork(): %s", strerror(errno));

    if (pid == 0) {
	FILE *f;
	int i;

	syslog(LOG_INFO, "(%s) CMD (%s)", pwd->pw_name, tab->list[job].command);

	fflush(stdout);
	fflush(stderr);

	dup2(io[1], 1);
	dup2(io[1], 2);
	close(io[0]);

	for (i=0; i < tab->nre; i++)
	    putenv(tab->env[i]);

	if ( f = popen(tab->list[job].command, "w") ) {
	    if (tab->list[job].input) {
		fputs(tab->list[job].input, f);
		fputc('\n', f);
	    }
	    pclose(f);
	}
	else fatal("popen(): %s", strerror(errno));
	waitpid(pid, &i, 0);
    }
    else {
	fd_set readers, errors;
	int rc,status;
	char peek[1];

	fflush(stdin);
	dup2(io[0], 0);
	close(io[1]);

	do {
	    FD_ZERO(&readers); FD_SET(0, &readers);
	    FD_ZERO(&errors);  FD_SET(0, &errors);
	} while (select(1, &readers, 0, &errors, 0) == 0);

	if (FD_ISSET(0, &readers) && (recv(0, peek, 1, MSG_PEEK) == 1) ) {
	    char subject[120];
	    char *to = mailto(tab);

	    if (to == 0) to = pwd->pw_name;

	    snprintf(subject, sizeof subject, "Cron <%s> %s", to, tab->list[job].command);
	    execl("/bin/mail", "mail", "-s", subject, to, 0L);

	    fatal("can't execl(\"/bin/mail\",...): %s", strerror(errno));
	}
    }
    exit(0);
}
