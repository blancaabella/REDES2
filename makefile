CC = gcc
CFLAGS = -g -Wall

all: server

server: server.c socket.o picohttpparser.o
	$(CC) $(CFLAGS) $^ -pthread -lconfuse -o $@

clean:
	rm -vf *.o libs/*.o server
