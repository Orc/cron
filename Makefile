CC=cc -baout
pgm=cron.t

objs=cron.o readcrontab.o

all: $(pgm)


cron.t: $(objs)
	$(CC) -o cron.t $(objs)

clean:
	rm -f $(pgm) *.o
