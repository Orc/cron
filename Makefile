CC=cc -baout -g -DDEBUG
progs=cron crontab

common=readcrontab.o lib.o

all: $(progs)


cron: cron.o $(common)
	$(CC) -o cron cron.o runjob.o $(common)

crontab: crontab.o $(common)
	$(CC) -o crontab crontab.o $(common)

clean:
	rm -f $(pgm) *.o
