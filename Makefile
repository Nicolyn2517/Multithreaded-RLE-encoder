CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Werror -Wextra
LDFLAGS=-pthread

.PHONY: all
all: nyuenc

nyuenc: nyuenc.o singleth.o 

nyuenc.o: nyuenc.c singleth.h

singleth.o: singleth.c singleth.h

.PHONY: clean
clean:
	rm -f *.o nyuenc
