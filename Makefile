LUA_INCDIR=/usr/include/lua5.3
LUA_BIN=/usr/bin/lua5.3
LIBDIR=/usr/local/lib/lua/5.3
CFLAGS=-Wall -Wextra -Wpointer-arith -Werror -fPIC -O3 -D_REENTRANT -D_GNU_SOURCE
LDFLAGS=-shared -fPIC

export LUA_CPATH=$(PWD)/?.so

default: all

all: tz.so

tz.so: tz.o
	gcc $(LDFLAGS) -o tz.so tz.o

tz.o: src/tz.h src/tz.c
	gcc -c -o tz.o $(CFLAGS) -I$(LUA_INCDIR) src/tz.c

.PHONY: test
test:
	$(LUA_BIN) test/test.lua

install:
	cp tz.so $(LIBDIR)

clean:
	-rm -f tz.o tz.so
