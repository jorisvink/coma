# coma Makefile.

CC?=cc
COMA=coma
DESTDIR?=
PREFIX?=/usr/local
INSTALL_DIR=$(PREFIX)/bin
MAN_DIR?=$(PREFIX)/share/man
SHARE_DIR=$(PREFIX)/share/coma
DARWIN_SB_PATH?=$(SHARE_DIR)/sb

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

INSTALL_TARGETS=	install-bin

OSNAME=$(shell uname -s | sed -e 's/[-_].*//g' | tr A-Z a-z)
ifeq ("$(OSNAME)", "linux")
	CFLAGS+=-DPLATFORM_LINUX
	CFLAGS+=-D_GNU_SOURCE=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
	SRC+=linux.c
else ifeq ("$(OSNAME)", "darwin")
	CFLAGS+=-DPLATFORM_DARWIN
	SRC+=darwin.c
	INSTALL_TARGETS+=install-darwin-sb
endif

all: $(COMA)

install: $(INSTALL_TARGETS)

app: $(COMA)
	./macos/make-app.sh

install-bin: $(COMA)
	mkdir -p $(DESTDIR)$(INSTALL_DIR)
	mkdir -p $(DESTDIR)$(MAN_DIR)/man1
	install -m 555 $(COMA) $(DESTDIR)$(INSTALL_DIR)/$(COMA)
	install -m 644 coma.1 $(DESTDIR)$(MAN_DIR)/man1/coma.1

install-darwin-sb:
	mkdir -p $(DARWIN_SB_PATH)
	install -m 644 coma.sb $(DARWIN_SB_PATH)

$(COMA): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(COMA)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

tests-run:
	$(MAKE) -C tests

clean:
	$(MAKE) -C tests clean
	rm -rf $(COMA) $(OBJS) Coma.app
