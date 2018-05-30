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

all:
	$(CC) $(CFLAGS) $(LDFLAGS) cpu.c audio.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fPIC core_au.c -o core_au.$(LIBEXT)
	mkdir -p $(R2P)
	cp -rf core_au.$(LIBEXT)* $(R2P)
