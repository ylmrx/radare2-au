CFLAGS+=$(shell pkg-config --cflags r_util)
#CFLAGS+=-fsanitize=address
CFLAGS+=-g
LDFLAGS+=$(shell pkg-config --libs r_util r_io r_cons r_core)
LDFLAGS+=-lao
ifeq ($(shell uname),Linux)
LDFLAGS+=-lm
endif
LIBEXT=$(shell r2 -H LIBEXT)
R2P=$(shell r2 -H USER_PLUGINS)

all: a.out core_au.$(LIBEXT) asm_au.$(LIBEXT) anal_au.$(LIBEXT)

asm_au.$(LIBEXT): asm_au.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fPIC asm_au.c -o asm_au.$(LIBEXT)

anal_au.$(LIBEXT): anal_au.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fPIC anal_au.c -o anal_au.$(LIBEXT)

core_au.$(LIBEXT): core_au.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fPIC core_au.c -o core_au.$(LIBEXT)

a.out:
	$(CC) $(CFLAGS) $(LDFLAGS) cpu.c audio.c

install:
	mkdir -p $(R2P)
	cp -rf asm_au.$(LIBEXT)* $(R2P)
	cp -rf anal_au.$(LIBEXT)* $(R2P)
	cp -rf core_au.$(LIBEXT)* $(R2P)

test:
	r2 -a au -b 32 -i test.r2  malloc://1M

test2:
	r2 -c 'b 4K;aui;aup' -c auv malloc://64K

uninstall:
	rm $(R2P)/*au.$(LIBEXT)*

m:
	$(MAKE) all
	$(MAKE) install
	r2 -a au -b 32 malloc://1M
