# Makefile
CC = gcc
CFLAGS = -ggdb #-Werror

CFLAGS += $(shell pkg-config --cflags --libs json-c)
CFLAGS += $(shell pkg-config --cflags --libs libcurl)

.PHONY: test clean

all: libnss_consul.so.2 tests

clean:
	rm -f libnss_consul.so.2
	rm -f test-nss

libnss_consul.so.2: nss.c
	$(CC) $(CFLAGS) -shared -fPIC -o libnss_consul.so.2 -Wl,-soname,libnss_consul.so.2 nss.c

tests: test-nss.c
	$(CC) -o test-nss test-nss.c

test:
	./test-nss web.service.consul
