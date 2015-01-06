# Makefile
CC = gcc
CFLAGS = -g


CFLAGS += $(shell pkg-config --cflags --libs json-c)
CFLAGS += $(shell pkg-config --cflags --libs libcurl)

all: libnss_consul.so.2

libnss_consul.so.2: nss.c
	$(CC) $(CFLAGS) -shared -fPIC -o libnss_consul.so.2 -Wl,-soname,libnss_consul.so.2 nss.c

test: test-nss.c
	$(CC) -o test test-nss.c
