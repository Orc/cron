/*
 * crontab: read/write/delete/edit crontabs
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <utime.h>

#include "cron.h"

char *pgm;

void
usage()
{
    fprintf(stderr, "usage: %s [-u user] -{l,r,e}\n"
                    "       %s [-u user] file\n", pgm, pgm);
    exit(1);
}


/* change directory or die
 */
void
xchdir(char *path)
{
    if (chdir(path) == -1) {
	perror(path);
	exit(1);
    }
}


/* pick up our superpowers (if any)
 */
void
superpowers()
{
    if (getuid() != geteuid()) {
	setuid(geteuid());
	setgid(getegid());
    }
}


/* become super, then cat a file out of CRONDIR
 */
int
cat(char *file)
{
    register c;
    FILE *f;

    superpowers();
    xchdir(CRONDIR);

    if ( f = fopen(file, "r") ) {
	while ( (c = fgetc(f)) != EOF ) {
	    putchar(c);
	}
	fclose(f);
	return 1;
    }
    return 0;
}


/* put a new crontab into CRONDIR
 */
int
newcrontab(char *file, char *name, int zap)
{
    static crontab tab = { 0 };
    int tempfile = 0;
    register c;
    FILE *in, *out;
    struct utimbuf touch;

    umask(077);

    if ( strcmp(file, "-") == 0 ) {
	long size = 0;
	if ( (in = tmpfile()) == 0 ) {
	    error("can't create tempfile: %s", strerror(errno));
	    return 0;
	}
	while ( ((c = getchar()) != EOF) && !ferror(in) ) {
	    ++size;
	    fputc(c, in);
	}
	if (ferror(in) || (fflush(in) == EOF) || fseek(in, 0L, SEEK_SET) == -1 ) {
	    fclose(in);
	    error("can't store crontab in tempfile: %s", strerror(errno));
	    return 0;
	}
	if (size == 0)
	    fatal("no input; use -r to delete a crontab");
	tempfile = 1;
    }
    else if ( (in = fopen(file, "r")) == 0 ) {
	error("can't open %s: %s", file, strerror(errno));
	return 0;
    }

    zerocrontab(&tab);
    if ( !readcrontab(&tab, in) ) {
	fclose(in);
	return 0;
    }
    if ( tempfile && (tab.nrl == 0) && (tab.nre == 0) ) {
	error("no jobs or environment variables defined; use -r to delete a crontab");
	return 0;
    }
    if ( fseek(in, 0L, SEEK_SET) == -1 ) {
	fclose(in);
	error("file error: %s", strerror(errno));
	return 0;
    }

    superpowers();
    xchdir(CRONDIR);

    if ( out = fopen(name, "w") ) {
	while ( ((c = fgetc(in)) != EOF) && (fputc(c,out) != EOF) )
	    ;
	if (zap) unlink(file);
	if ( ferror(in) || ferror(out) || (fclose(out) == EOF) ) {
	    unlink(name);
	    fatal("can't write to crontab: %s", strerror(errno));
	}
	fclose(in);
	time(&touch.modtime);
	time(&touch.actime);
	utime(".", &touch);
	exit(0);
    }
    if (zap) unlink(file);
    fatal("can't write to crontab: %s", strerror(errno));
}


/* Use a visual editor ($EDITOR,$VISUAL, or vi) to edit a crontab,
 * then copy the new one into CRONDIR
 */
void
visual(struct passwd *pwd)
{
    char tempfile[20];
    char ans[20];
    char commandline[80];
    char *editor;
    char *yn;
    struct stat st;
    int save_euid = geteuid();

    xchdir(pwd->pw_dir ? pwd->pw_dir : "/tmp");
    strcpy(tempfile, ".crontab.XXXXXX");
    seteuid(getuid());
    mktemp(tempfile);

    if ( ((editor = getenv("EDITOR")) == 0) && ((editor = getenv("VISUAL")) == 0) )
	editor="vi";

    if ( access(editor, X_OK) == 0 ) {
	unlink(tempfile);
	fatal("%s: %s", editor, strerror(errno));
    }

    sprintf(commandline, "crontab -l > %s", tempfile);
    system(commandline);

    if ( (stat(tempfile, &st) != -1) && (st.st_size == 0) )
	unlink(tempfile);

    sprintf(commandline, "%s %s", editor, tempfile);
    while (1) {
	if ( system(commandline) == -1 )
	    fatal("running %s: %s", editor, strerror(errno));
	seteuid(save_euid);
	if ( (stat(tempfile, &st) == -1) || (st.st_size == 0)
	                                 || newcrontab(tempfile, pwd->pw_name, 1)) {
	    unlink(tempfile);
	    exit(0);
	}
	seteuid(getuid());
	do {
	    fprintf(stderr, "Do you want to retry the same edit? ");
	    fgets(ans, sizeof ans, stdin);
	    yn = firstnonblank(ans);
	    *yn = toupper(*yn);
	    if (*yn == 'N') {
		unlink(tempfile);
		exit(1);
	    }
	    if (*yn != 'Y')
		fprintf(stderr, "(Y)es or (N)o; ");
	} while (*yn != 'Y');
    }
}


/* crontab
 */
main(int argc, char **argv)
{
    int i;
    int opt;
    int edit=0,remove=0,list=0;
    char *user = 0;
    char whoami[20];
    char *what = "update";
    struct passwd *pwd;

    opterr = 1;

    pgm = basename(argv[0]);

    if ( (pwd = getpwuid(getuid())) == 0 )
	fatal("you don't seem to have a password entry?\n");
    strncpy(whoami, pwd->pw_name, sizeof whoami);

    while ( (opt=getopt(argc,argv, "elru:")) != EOF ) {
	switch (opt) {
	case 'e': edit = 1; what = "edit"; break;
	case 'l': list = 1; what = "list"; break;
	case 'r': remove = 1; what = "remove"; break;
	case 'u': user = optarg; break;
	default:  usage();
	}
    }
    if ( edit + list + remove > 1 )
	usage();

    if (user) {
	if ( (pwd = getpwnam(user)) == 0 )
	    fatal("user %s does not have a password entry.\n", user);
    }
    else if ( (pwd = getpwuid(getuid())) == 0 )
	fatal("you don't seem to have a password entry?\n");

    if ( (getuid() != 0) && (pwd->pw_uid != getuid()) )
	fatal("you may not %s %s's crontab.\n", what, pwd->pw_name);

    argc -= optind;
    argv += optind;

    openlog("crontab", LOG_PID, LOG_CRON);
    syslog(LOG_INFO, (strcmp(whoami,pwd->pw_name) == 0) ? "(%s) %s"
							: "(%s) %s (%s)",
				whoami, what, pwd->pw_name);
    if (list) {
	if ( !cat(pwd->pw_name) )
	    fatal("no crontab for %s", pwd->pw_name);
    }
    else if (remove) {
	superpowers();
	xchdir(CRONDIR);
	if (unlink(pwd->pw_name) == -1) {
	    perror(pwd->pw_name);
	    exit(1);
	}
    }
    else if (edit)
	visual(pwd);
    else if ( !newcrontab( argc ? argv[0] : "-", pwd->pw_name, 0) )
	fatal("errors in crontab file, cannot install!");
    exit(0);
}
