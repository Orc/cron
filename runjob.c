/*
 * run a job, mail output (if any) to the user
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
#include <paths.h>
#include <signal.h>

#include "cron.h"

extern char *pgm;


/* the child process that actually runs a job
 */
static void
runjobprocess(crontab *tab,cron *job,struct passwd *user)
{
    FILE *f;
    pid_t jpid;
    char *home = user->pw_dir ? user->pw_dir : "/tmp";
    fd_set readers, errors;
    int i, rc, status;
    int mailpipe[2], jobinput[2];
    char peek[1];
    char *shell = jobenv(tab, "SHELL");
    pid_t pid;

    if ( shell == 0 )
	shell = _PATH_BSHELL;

    strcpy(pgm, "crjp");
    setsid();
    signal(SIGCHLD, SIG_DFL);

    if ( setregid(user->pw_gid, user->pw_gid) == -1)
	fatal("setregid(%d,%d): %s", user->pw_gid, user->pw_gid, strerror(errno));
    if ( setreuid(user->pw_uid, user->pw_uid) == -1)
	fatal("setreuid(%d,%d): %s", user->pw_uid, user->pw_uid, strerror(errno));

    if ( chdir(home) == -1 )
	fatal("chdir(\"%s\"): %s", home, strerror(errno));

    if ( socketpair(AF_UNIX, SOCK_STREAM, 0, mailpipe) == -1 )
	fatal("socketpair: %s", strerror(errno));

    switch (pid=fork()) {
    case -1: fatal("fork(): %s", strerror(errno));
    case 0: strcpy(pgm,"jrun");

	    dup2(mailpipe[1], 1);
	    dup2(mailpipe[1], 2);
	    close(mailpipe[0]);

	    if ( pipe(jobinput) == -1 )
		fatal("cannot create input pipe: %s", strerror(errno));

	    switch (pid=fork()) {
	    case -1:fatal("fork(): %s", strerror(errno));
		    break;
	    case 0: dup2(jobinput[0], 0);
		    close(jobinput[1]);
		    execle(shell, "sh", "-c", job->command, 0L, tab->env);
		    fatal("cannot exec %s: %s", shell, strerror(errno));
		    break;
	    default:close(jobinput[0]);
		    if (job->input)
			write(jobinput[1], job->input, strlen(job->input));
		    close(jobinput[1]);
		    waitpid(pid, &status, 0);
		    break;
	    }
	    break;
    default:
	    fflush(stdin);
	    dup2(mailpipe[0], 0);
	    close(mailpipe[1]);

	    strcpy(pgm,"jwai");
	    do {
		FD_ZERO(&readers); FD_SET(0, &readers);
		FD_ZERO(&errors);  FD_SET(0, &errors);
	    } while (select(1, &readers, 0, &errors, 0) == 0);

	    strcpy(pgm, "jmai");
	    alarm(60);
	    if (FD_ISSET(0, &readers) && (recv(0, peek, 1, MSG_PEEK) == 1) ) {
		char subject[120];
		char *to = jobenv(tab, "MAILTO");
		alarm(0);

		if (to == 0) to = user->pw_name;

		snprintf(subject, sizeof subject,
			 "Cron <%s> %s", to, job->command);
		execle(PATH_MAIL, "mail", "-s", subject, to, 0L, tab->env);
		fatal("can't execl(\"%s\"): %s", PATH_MAIL, strerror(errno));
	    }
	    break;
    }
    exit(0);
}


/* validate the user and fork off a childprocess to run the job
 */
void
runjob(crontab *tab, cron *job)
{
    pid_t pid;
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
    printtrig(&job->trigger);
    printf("%s", job->command);
    if (job->input)
	printf("<< \\EOF\n%sEOF\n", job->input);
    else
	putchar('\n');
#endif
    syslog(LOG_INFO, "(%s) CMD (%s)", pwd->pw_name, job->command);

    switch (fork()) {
    case 0: runjobprocess(tab,job,pwd);		/* should never return */
	    fatal("runjobprocess returned?");	/* but better safe than sorry */

    case -1:error("fork(): %s", strerror(errno));
    }
}
