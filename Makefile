CC = gcc
CFLAGS = -Wall

all: index peer

index: index.c
	$(CC) $(CFLAGS) -o index index.c

peer: peer.c
	$(CC) $(CFLAGS) -pthread -o peer peer.c

clean:
	rm -f index peer

.PHONY: all clean
