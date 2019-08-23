# coma Makefile.

CC?=cc
COMA=coma
PREFIX?=/usr/local
INSTALL_DIR=$(PREFIX)/bin
MAN_DIR?=$(PREFIX)/share/man

SRC=	coma.c client.c config.c frame.c wm.c
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

CFLAGS+=`pkg-config --cflags x11 xft`
LDFLAGS+=`pkg-config --libs x11 xft`

all: $(COMA)

install: $(COMA)
	install -m 555 $(COMA) $(INSTALL_DIR)/$(COMA)
	install -m 555 scripts/coma-* $(INSTALL_DIR)
	install -m 644 coma.1 $(MAN_DIR)/man1/coma.1

$(COMA): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(COMA)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(COMA) $(OBJS)
