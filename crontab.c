/*
 * crontab: read/write/delete/edit crontabs
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/types.h>

char *pgm;

void
usage()
{
    fprintf(stderr, "usage: %s [-u user] -{l,r,e}\n"
                    "       %s [-u user] file\n", pgm, pgm);
    exit(1);
}


void
xchdir(char *path)
{
    if (chdir(path) == -1) {
	perror(path);
	exit(1);
    }
}


void
superpowers()
{
    if (getuid() != geteuid()) {
	/* pick up our superpowers (if any) */
	setuid(geteuid());
	setgid(getegid());
    }
}


void
cat(char *file)
{
    register c;
    FILE *f = fopen(file, "r");

    if (f) {
	while ( (c = fgetc(f)) != EOF ) {
	    putchar(c);
	}
	fclose(f);
	exit(0);
    }
    perror(file);
    exit(1);
}


main(int argc, char **argv)
{
    int i;
    int opt;
    int edit=0,remove=0,list=0;
    char *user = 0;
    char *what = "update";
    struct passwd *pwd;

    opterr = 1;

    pgm = basename(argv[0]);

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
	if ( (pwd = getpwnam(user)) == 0 ) {
	    fprintf(stderr, "user %s does not have a password entry.\n", user);
	    exit(1);
	}
    }
    else if ( (pwd = getpwuid(getuid())) == 0 ) {
	fprintf(stderr, "you don't seem to have a password entry?\n");
	exit(1);
    }

    if ( (getuid() != 0) && (pwd->pw_uid != getuid()) ) {
	fprintf(stderr, "you may not %s %s's crontab.\n", what, pwd->pw_name);
	exit(1);
    }

    argc -= optind;
    argv += optind;

    if (list) {
	superpowers();
	xchdir(CRONDIR);
	cat(pwd->pw_name);
    }
    else if (remove) {
	superpowers();
	xchdir(CRONDIR);
	if (unlink(pwd->pw_name) == -1) {
	    perror(pwd->pw_name);
	    exit(1);
	}
    }
#if 0
    else if (edit) {
    }
    else {
	FILE *f;
	struct stat st;

	if (argc > 0 && strcmp(argv[0], "-") != 0) {
	    if ( (f = fopen(argv[0], "r")) == 0 ) {
		perror(argv[0]);
		exit(1);
	    }
	}
	else
	    f = stdin;

	slurp(f);
	superpowers();
	xchdir(CRONDIR);
	unlink(pwd->pw_name);
	if ( (f = fopen(pwd->pw_name, "w")) == 0 ) {
	    perror(pwd->pw_name);
	    exit(1);
	}
	paint(f);
	fclose(f);
    }
#endif
    exit(0);
}
