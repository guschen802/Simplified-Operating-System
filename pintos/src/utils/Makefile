all: setitimer-helper squish-pty squish-unix

CC = gcc
CFLAGS = -Wall -W -m32
LDLIBS = -lm -m32
setitimer-helper: setitimer-helper.o
squish-pty: squish-pty.o
squish-unix: squish-unix.o

clean: 
	rm -f *.o setitimer-helper squish-pty squish-unix
