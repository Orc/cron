CC=cc -baout -g -DDEBUG
progs=cron crontab

objs=cron.o readcrontab.o runjob.o lib.o

all: $(progs)


cron: $(objs)
	$(CC) -o cron $(objs)

clean:
	rm -f $(pgm) *.o
