#! /bin/sh

# local options:  ac_help is the help message that describes them
# and LOCAL_AC_OPTIONS is the script that interprets them.  LOCAL_AC_OPTIONS
# is a script that's processed with eval, so you need to be very careful to
# make certain that what you quote is what you want to quote.

ac_help='
--with-crondir=PATH	Where does the crontab directory go?
--with-mail=PATH	What program to use to send mail (mail)'

# load in the configuration file
#
TARGET=cron
. ./configure.inc

AC_INIT $TARGET

AC_PROG_CC
AC_SCALAR_TYPES
AC_CHECK_ALLOCA || AC_FAIL "$TARGET requires alloca()"

# for basename
if AC_CHECK_FUNCS basename; then
    AC_CHECK_HEADERS libgen.h
fi

if [ "$WITH_MAIL" ]; then
    AC_DEFINE PATH_MAIL \"${WITH_MAIL}\"
else
    MF_PATH_INCLUDE MAIL mail || AC_FAIL "$TARGET requires a mail program"
fi

if [ "$WITH_CRONDIR" ]; then
    CRONDIR=${WITH_CRONDIR}
elif [ -d /var/spool/cron/crontabs ]; then
    CRONDIR=/var/spool/cron/crontabs
else
    CRONDIR=/var/spool/cron
fi
AC_SUB CRONDIR	\"${CRONDIR}\"
AC_DEFINE CRONDIR	\"${CRONDIR}\"

AC_OUTPUT Makefile crontab.5 crontab.1 cron.8
