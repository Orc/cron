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
runjobprocess(crontab *tab,cron *job,struct passwd *usr)
{
    FILE *f;
    pid_t jpid;
    char *home = usr->pw_dir ? usr->pw_dir : "/tmp";
    fd_set readers, errors;
    int i, rc, status;
    int mailpipe[2], jobinput[2];
    char peek[1];
    char subject[120];
    char *shell, *to;
    pid_t pid;

    if ( (shell=jobenv(tab, "SHELL")) == 0 )
	shell = _PATH_BSHELL;

    setsid();
    signal(SIGCHLD, SIG_DFL);

    if ( setregid(usr->pw_gid, usr->pw_gid) == -1)
	fatal("setregid(%d,%d): %s", usr->pw_gid, usr->pw_gid, strerror(errno));
    if ( setreuid(usr->pw_uid, usr->pw_uid) == -1)
	fatal("setreuid(%d,%d): %s", usr->pw_uid, usr->pw_uid, strerror(errno));

    if ( chdir(home) == -1 )
	fatal("chdir(\"%s\"): %s", home, strerror(errno));

    if ( socketpair(AF_UNIX, SOCK_STREAM, 0, mailpipe) == -1 )
	fatal("socketpair: %s", strerror(errno));

    switch (pid=fork()) {
    case -1: fatal("fork(): %s", strerror(errno));
    case 0: dup2(mailpipe[1], 1);
	    dup2(mailpipe[1], 2);
	    close(mailpipe[0]);

	    if ( pipe(jobinput) == -1 )
		fatal("cannot create input pipe: %s", strerror(errno));

	    switch (pid=fork()) {
	    case -1:fatal("fork(): %s", strerror(errno));
		    break;
	    case 0: setsid();
		    dup2(jobinput[0], 0);
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
	    close(mailpipe[1]);

	    if ( recv(mailpipe[0], peek, 1, MSG_PEEK) == 1 ) {

		if ( (to=jobenv(tab,"MAILTO")) == 0) to = usr->pw_name;

		snprintf(subject, sizeof subject,
			 "Cron <%s> %s", to, job->command);

		dup2(mailpipe[0], 0);
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
