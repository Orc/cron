CC=cc -baout -g -DDEBUG
pgm=cron.t

objs=cron.o readcrontab.o runjob.o

all: $(pgm)


cron.t: $(objs)
	$(CC) -o cron.t $(objs)

clean:
	rm -f $(pgm) *.o
