CC = gcc
CFLAGS = -Wall -Wextra -O3 -I./src
LDFLAGS = -pthread

all: gnetworkd gnetwork

gnetworkd: src/gnetworkd.c src/common.h
	$(CC) $(CFLAGS) -o gnetworkd src/gnetworkd.c $(LDFLAGS)

gnetwork: src/gnetwork.c src/common.h
	$(CC) $(CFLAGS) -o gnetwork src/gnetwork.c $(LDFLAGS)

clean:
	rm -f gnetworkd gnetwork

install: all
	mkdir -p /usr/local/bin
	cp -f gnetworkd /usr/local/bin/gnetworkd
	cp -f gnetwork /usr/local/bin/gnetwork
	chmod 755 /usr/local/bin/gnetworkd /usr/local/bin/gnetwork

.PHONY: all clean install
