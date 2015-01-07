# Makefile
CC = gcc
CFLAGS = -g


CFLAGS += $(shell pkg-config --cflags --libs json-c)
CFLAGS += $(shell pkg-config --cflags --libs libcurl)

.PHONY: test clean

all: libnss_consul.so.2 tests

clean:
	rm -f libnss_consul.so.2
	rm -f test-nss
	rm -f test

libnss_consul.so.2: nss.c
	$(CC) $(CFLAGS) -shared -fPIC -o libnss_consul.so.2 -Wl,-soname,libnss_consul.so.2 nss.c

tests: test-nss.c test.c
	$(CC) -o test-nss test-nss.c
	$(CC) $(CFLAGS) -o test test.c

test: tests
	./test-nss
