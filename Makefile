CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

all: proxy

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c csapp.h
	$(CC) $(CFLAGS) -c proxy.c

cache.o: cache.c
	$(CC) $(CFLAGS) -c cache.c

cache2.o: cache2.c
	$(CC) $(CFLAGS) -c cache2.c

cache: cache.o csapp.o

cache2: cache2.o csapp.o

proxy: proxy.o csapp.o

submit:
	(make clean; cd ..; tar cvf proxylab.tar proxylab-handout)

clean:
	rm -f *~ *.o proxy core

