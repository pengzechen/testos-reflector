# Makefile for user test programs

# Use musl cross compiler for static linking
CC = aarch64-linux-musl-gcc
CFLAGS = -static -O2 -Wall

PROGRAMS = hello

all: $(PROGRAMS)

hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c

clean:
	rm -f $(PROGRAMS) *.o

.PHONY: all clean
