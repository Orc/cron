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
    int status;
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
	info("(%s) CMD (%s)",usr->pw_name,job->command);

	setsid();

	close(input[1]);
	close(output[0]);

	close(0); dup2(input[0], 0); close(input[0]);
	close(1); dup2(output[1], 1);
	close(2); dup2(1, 2); close(output[1]);

	execle(shell, "sh", "-c", job->command, (char*)0, tab->env);
	perror(shell);
    }
    else {					/* runjobprocess() */
	close(input[0]);
	close(output[1]);

	if (job->input && (fork() == 0) ) {
	    close(output[0]);
	    write(input[1], job->input, strlen(job->input));
	    close(input[1]);
	    exit(0);
	}
	close(input[1]);

	if ( fork() == 0 ) {
	    if (recv(output[0], peek, 1, MSG_PEEK) == 1 ) {
		close(0);dup2(output[0],0);
		if ( (to=jobenv(tab,"MAILTO")) == 0)
		    to = usr->pw_name;
		snprintf(subject, sizeof subject,
			 "Cron <%s> %s", to, job->command);

		execle(PATH_MAIL, "mail", "-s", subject, to, (char*)0, tab->env);
		fatal("can't exec(\"%s\"): %s", PATH_MAIL, strerror(errno));
	    }
	    exit(0);
	}
	close(output[0]);
	/* wait for all subprocesses to finish */
	while ( wait(&status) != -1 )
	    ;
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
    if ( (pid = fork()) == -1 )
	error("fork(): %s", strerror(errno));
    else if ( pid == 0 ) {
	runjobprocess(tab,job,pwd);
	exit(0);
    }
}
