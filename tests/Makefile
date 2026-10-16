# coma tests Makefile

CC?=cc

OBJDIR?=obj
LIB=$(OBJDIR)/libtest.a

CFLAGS+=-std=c99 -pedantic -Wall -Werror -Wstrict-prototypes
CFLAGS+=-Wmissing-prototypes -Wmissing-declarations -Wshadow
CFLAGS+=-Wpointer-arith -Wcast-qual -Wsign-compare
CFLAGS+=-I.. -I. -g -DCOMA_TESTS
CFLAGS+=$(shell pkg-config --cflags sdl2 SDL2_ttf fontconfig)

SRC=	framework.c
COMA=	coma.c client.c config.c draw.c frame.c term.c vt.c wm.c

TESTS=	$(OBJDIR)/test-term \
	$(OBJDIR)/test-wm \
	$(OBJDIR)/test-select
OBJS=	$(SRC:%.c=$(OBJDIR)/%.o) $(COMA:%.c=$(OBJDIR)/coma/%.o)

OSNAME=$(shell uname -s | sed -e 's/[-_].*//g' | tr A-Z a-z)

ifeq ("$(OSNAME)", "linux")
	CFLAGS+=-D_GNU_SOURCE=1
endif

LDFLAGS+=	$(LIB)
LDFLAGS+=	$(shell pkg-config --libs sdl2 SDL2_ttf fontconfig)
LDFLAGS+=	-lutil

all: test

test: $(TESTS)
	@status=0; \
	for t in $(TESTS); do \
		./$$t || status=1; \
	done; \
	exit $$status

$(LIB): $(OBJS) $(OBJDIR)
	$(AR) rcs $(LIB) $(OBJS)

$(OBJDIR)/test-%: $(LIB) tests/%.c
	$(CC) $(CFLAGS) tests/$*.c $(LDFLAGS) -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/coma/%.o: ../%.c ../coma.h
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c framework.h ../coma.h
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(LIB)
	rm -rf $(OBJDIR)

.PHONY: all clean test
