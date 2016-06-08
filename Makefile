LUA_INCLUDE = /usr/include/lua5.2
LIB_INSTALL = /usr/local/lib/lua/5.2
CFLAGS = -pedantic -Wall -Wextra -fPIC -O2 -D_REENTRANT
LDFLAGS = -shared -fPIC

all: 
	@echo "Please run make <linux|macosx>."

linux:
	$(MAKE) tz.so

macosx:
	$(MAKE) LDFLAGS="$(LDFLAGS) -undefined dynamic_lookup" tz.so

tz.o: tz.h tz.c
	gcc -c ${CFLAGS} -I${LUA_INCLUDE} tz.c

tz.so: tz.o
	gcc ${LDFLAGS} -o tz.so tz.o

test:
	lua test.lua

install:
	cp tz.so ${LIB_INSTALL}

clean:
	-rm tz.o tz.so
