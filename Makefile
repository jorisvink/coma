# coma Makefile.

CC?=cc
COMA=coma
OBJDIR?=obj
PREFIX?=/usr/local

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

$(COMA): $(OBJDIR) $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(COMA)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(COMA) $(OBJDIR)
