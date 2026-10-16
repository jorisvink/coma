# coma Makefile.

CC?=cc
COMA=coma
DESTDIR?=
PREFIX?=/usr/local
INSTALL_DIR=$(PREFIX)/bin
MAN_DIR?=$(PREFIX)/share/man

SRC=	coma.c client.c config.c draw.c frame.c term.c vt.c wm.c
OBJS=	$(SRC:%.c=%.o)

CFLAGS+=-Wall
CFLAGS+=-Werror
CFLAGS+=-Wstrict-prototypes
CFLAGS+=-Wmissing-prototypes
CFLAGS+=-Wmissing-declarations
CFLAGS+=-Wshadow
CFLAGS+=-Wpointer-arith
CFLAGS+=-Wcast-qual
CFLAGS+=-Wsign-compare
CFLAGS+=-std=c99
CFLAGS+=-pedantic

CFLAGS+=`pkg-config --cflags sdl2 SDL2_ttf fontconfig`
LDFLAGS+=`pkg-config --libs sdl2 SDL2_ttf fontconfig`
LDFLAGS+=-lutil

OSNAME=$(shell uname -s | sed -e 's/[-_].*//g' | tr A-Z a-z)
ifeq ("$(OSNAME)", "linux")
	CFLAGS+=-DPLATFORM_LINUX
	CFLAGS+=-D_GNU_SOURCE=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
	SRC+=linux.c
endif

all: $(COMA)

install: $(COMA)
	mkdir -p $(DESTDIR)$(INSTALL_DIR)
	mkdir -p $(DESTDIR)$(MAN_DIR)/man1
	install -m 555 $(COMA) $(DESTDIR)$(INSTALL_DIR)/$(COMA)
	install -m 644 coma.1 $(DESTDIR)$(MAN_DIR)/man1/coma.1

$(COMA): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(COMA)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

tests-run:
	$(MAKE) -C tests

clean:
	$(MAKE) -C tests clean
	rm -rf $(COMA) $(OBJS)
