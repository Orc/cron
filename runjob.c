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

extern char *pgm;


/* run a job, mail output (if any) to the user.
 */
void
runjob(crontab *tab, int job)
{
    pid_t pid;
    int mailpipe[2], jobinput[2];
    struct passwd *pwd;
    char *shell;

    if ( (pwd = getpwuid(tab->user)) == 0 ) {
	/* if we can't find a password entry for this crontab, 
	 * touch mtime so that it will be rescanned the next
	 * time cron checks the CRONDIR for new crontabs.
	 */
	error("no password entry for user %d\n", tab->user);
	time(&tab->mtime);
	return;
    }

    if ( (shell = jobenv(tab, "SHELL")) == 0 ) {
	error("corrupt job structure for %s", pwd->pw_name);
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


    strcpy(pgm, "runj");
    /* from this point on, we're the child process and should fatal() if
     * anything goes wrong
     */

    if ( setregid(pwd->pw_gid, pwd->pw_gid) == -1)
	fatal("setregid(%d,%d): %s", pwd->pw_gid, pwd->pw_gid, strerror(errno));
    if ( setreuid(pwd->pw_uid, pwd->pw_uid) == -1)
	fatal("setreuid(%d,%d): %s", pwd->pw_uid, pwd->pw_uid, strerror(errno));

    if ( chdir(pwd->pw_dir ? pwd->pw_dir : "/tmp") == -1 )
	fatal("chdir(\"%s\"): %s", pwd->pw_dir ? pwd->pw_dir : "/tmp", strerror(errno));

    if ( socketpair(AF_UNIX, SOCK_STREAM, 0, mailpipe) == -1 ) fatal("socketpair: %s", strerror(errno));

    pid = fork();

    if (pid == -1) fatal("fork(): %s", strerror(errno));

    if (pid == 0) {
	FILE *f;
	int i;
	pid_t jpid;

	strcpy(pgm,"cmdj");
	syslog(LOG_INFO, "(%s) CMD (%s)", pwd->pw_name, tab->list[job].command);

	dup2(mailpipe[1], 1);
	dup2(mailpipe[1], 2);
	close(mailpipe[0]);

	if ( pipe(jobinput) == -1 )
	    fatal("cannot create input pipe: %s", strerror(errno));

	if ( (jpid = fork()) == -1 )
	    fatal("cannot fork: %s", strerror(errno));

	if (jpid == 0) {
	    dup2(jobinput[0], 0);
	    close(jobinput[1]);
	    execle(shell, "sh", "-c", tab->list[job].command, 0L, tab->env);
	    fatal("cannot exec %s: %s", shell, strerror(errno));
	}
	close(jobinput[0]);
	if (tab->list[job].input)
	    write(jobinput[1], tab->list[job].input,
			strlen(tab->list[job].input));
    }
    else {
	fd_set readers, errors;
	int rc,status;
	char peek[1];

	fflush(stdin);
	dup2(mailpipe[0], 0);
	close(mailpipe[1]);

	strcpy(pgm,"selj");
	do {
	    FD_ZERO(&readers); FD_SET(0, &readers);
	    FD_ZERO(&errors);  FD_SET(0, &errors);
	} while (select(1, &readers, 0, &errors, 0) == 0);

	strcpy(pgm, "maij");
	if (FD_ISSET(0, &readers) && (recv(0, peek, 1, MSG_PEEK) == 1) ) {
	    char subject[120];
	    char *to = jobenv(tab, "MAILTO");

	    if (to == 0) to = pwd->pw_name;

	    snprintf(subject, sizeof subject,
			     "Cron <%s> %s", to, tab->list[job].command);
	    execle(PATH_MAIL, "mail", "-s", subject, to, 0L, tab->env);

	    fatal("can't execl(\"%s\",...): %s", PATH_MAIL, strerror(errno));
	}
    }
    exit(0);
}
