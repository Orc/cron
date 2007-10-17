CC=cc -baout -g -DDEBUG
progs=cron crontab

common=readcrontab.o lib.o

all: $(progs)

install: $(progs)
	install -c -m 700 cron /usr/sbin/cron
	install -c -m 4711 crontab /usr/bin/crontab
	install -c cron.8 /usr/man/man8
	install -c crontab.5 /usr/man/man5
	install -c crontab.1 /usr/man/man1



cron: cron.o runjob.o $(common)
	$(CC) -o cron cron.o runjob.o $(common)

crontab: crontab.o $(common)
	$(CC) -o crontab crontab.o $(common)

clean:
	rm -f $(pgm) *.o
