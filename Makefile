# coma Makefile.

CC?=cc
COMA=coma
OBJDIR?=obj
PREFIX?=/usr/local
INSTALL_DIR=$(PREFIX)/bin
MAN_DIR=$(PREFIX)/share/man

SRC=	src/coma.c src/client.c src/config.c src/frame.c src/wm.c
OBJS=	$(SRC:src/%.c=$(OBJDIR)/%.o)

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

CFLAGS+=$(shell pkg-config --cflags x11 xft)
LDFLAGS+=$(shell pkg-config --libs x11 xft)

all: $(COMA)

install: $(COMA)
	install -m 555 $(COMA) $(INSTALL_DIR)/$(COMA)
	install -m 644 coma.1 $(MAN_DIR)/man1/coma.1

$(COMA): $(OBJDIR) $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(COMA)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(COMA) $(OBJDIR)
