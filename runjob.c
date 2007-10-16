#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
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

extern void error(char*,...);


static char *
mailto(crontab *tab)
{
    int i;

    for (i=0; i < tab->nre; ++i)
	if ( strncmp(tab->env[i], "MAILTO=", 7) == 0 )
	    return tab->env[i] + 7;
    return 0;
}


/*
 * run a job, mail output (if any) to the user.
 */
void
runjob(crontab *tab, int job)
{
    pid_t pid;
    int io[2];
    struct passwd *pwd;

    if ( (pwd = getpwuid(tab->user)) == 0 ) {
	error("no password entry for user %d\n", tab->user);
	return;
    }

    if ( (pid = fork()) == -1 ) {
	error("fork(): %s", strerror(errno));
	return;
    }

    if (pid != 0)
	return;

    if ( chdir("/tmp") == -1 ) { error("chdir(\"/tmp\"): %s", strerror(errno)); exit(1); }

    if ( setregid(pwd->pw_gid, pwd->pw_gid) == -1) { error("setregid(%d,%d): %s", pwd->pw_gid, pwd->pw_gid, strerror(errno)); exit(1); }
    if ( setreuid(pwd->pw_uid, pwd->pw_uid) == -1) { error("setreuid(%d,%d): %s", pwd->pw_uid, pwd->pw_uid, strerror(errno)); exit(1); }

    if (pwd->pw_dir)
	chdir(pwd->pw_dir);

    if ( pipe(io) == -1 ) { error("pipe: %s", strerror(errno)); exit(1); }

    pid = fork();

    if (pid == -1) { error("fork(): %s", strerror(errno)); exit(1); }

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
	else { error("popen(): %s", strerror(errno)); exit(1); }
    }
    else {
	fd_set readers, errors;
	int rc,status;

	fflush(stdin);
	dup2(io[0], 0);
	close(io[1]);

	do {
	    FD_ZERO(&readers); FD_SET(0, &readers);
	    FD_ZERO(&errors);  FD_SET(0, &errors);
	} while (select(1, &readers, 0, &errors, 0) == 0);

	if (FD_ISSET(0, &readers)) {
	    char subject[120];
	    char *to = mailto(tab);

	    if (to == 0) to = pwd->pw_name;

	    snprintf(subject, sizeof subject, "Cron <%s> %s", to, tab->list[job].command);
	    execl("/bin/mail", "mail", "-s", subject, to, 0L);

	    error("can't execl(\"/bin/mail\",...): %s", strerror(errno));
	    exit(1);
	}
    }
    exit(0);
}
