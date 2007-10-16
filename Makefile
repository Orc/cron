CC=cc -baout -g -DDEBUG
progs=cron crontab

objs=cron.o readcrontab.o runjob.o

all: $(progs)


cron: $(objs)
	$(CC) -o cron.t $(objs)

clean:
	rm -f $(pgm) *.o
