/*
 * run a job, mail output (if any) to the user
 */
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
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
    pid_t jpid;
    char *home = usr->pw_dir ? usr->pw_dir : "/tmp";
    int i, rc, size, status;
    int input[2], output[2];
    char peek[1];
    char subject[120];
    char *shell, *to;

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

    if ( (socketpair(AF_UNIX,SOCK_STREAM,0,output) == -1)||(pipe(input) == -1) )
	fatal("pipe: %s", strerror(errno));

    if ( (jpid=fork()) == -1 )
	fatal("job fork(): %s", strerror(errno));
    else if ( jpid == 0 ) {			/* the job to run */
	syslog(LOG_INFO, "(%s) CMD (%s)",usr->pw_name,job->command);
	closelog();

	dup2(input[0], 0);
	close(input[1]);

	dup2(output[1], 1);
	dup2(output[1], 2);
	close(output[0]);

	setsid();

	execle(shell, "sh", "-c", job->command, 0L, tab->env);
	perror(shell);
    }
    else {					/* runjobprocess() */
	close(output[1]);
	close(input[0]);

	if (job->input)
	    write(input[1], job->input, strlen(job->input));
	close(input[1]);

	/* horrible kludge:  on linux 2.0.28, running smtpd as a cron
	 * job results in a zombied smtpd and a runjob subprocess hanging
	 * on the recv() or a read().  So, wait a second and then check
	 * to see if the process exists before peeking at output[0]
	 */
	sleep(1);
	if ( waitpid(jpid, &status, WNOHANG) == 0 ) {
	    /* if there's any output waiting, fire off a mailer to
	     * send that to the MAILTO address
	     */
	    if ( recv(output[0], peek, 1, MSG_PEEK) == 1 ) {
		dup2(output[0],0);
		if ( (to=jobenv(tab,"MAILTO")) == 0)
		    to = usr->pw_name;
		snprintf(subject, sizeof subject,
			 "Cron <%s> %s", to, job->command);

		execle(PATH_MAIL, "mail", "-s", subject, to, 0L, tab->env);
		fatal("can't exec(\"%s\"): %s", PATH_MAIL, strerror(errno));
	    }
	    waitpid(jpid, &status, 0);		/* wait for job to finish */
	}
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
    switch (fork()) {
    case 0: runjobprocess(tab,job,pwd);		/* should never return */
	    fatal("runjobprocess returned?");	/* but better safe than sorry */

    case -1:error("fork(): %s", strerror(errno));
    }
}
