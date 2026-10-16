/*
 * Copyright (c) 2026 Joris Vink <joris@coders.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "framework.h"

#define SELECT_TEXT	"alpha beta gamma"

static void	test_select_drag(void);
static void	test_select_word(void);
static void	test_select_line(void);
static void	test_select_paste(void);
static void	test_select_blank(void);
static void	test_select_hidden(void);
static void	test_select_sb_wide(void);
static void	test_select_wide_word(void);
static void	test_select_wide_wrap(void);
static void	test_select_word_wrap(void);

static int	select_setup(void);
static void	select_expect(const char *);

void
test_entry(void)
{
	test_framework_register("select-drag", test_select_drag);
	test_framework_register("select-word", test_select_word);
	test_framework_register("select-line", test_select_line);
	test_framework_register("select-paste", test_select_paste);
	test_framework_register("select-blank", test_select_blank);
	test_framework_register("select-hidden", test_select_hidden);
	test_framework_register("select-sb-wide", test_select_sb_wide);
	test_framework_register("select-wide-word", test_select_wide_word);
	test_framework_register("select-wide-wrap", test_select_wide_wrap);
	test_framework_register("select-word-wrap", test_select_word_wrap);

	test_framework_run();
}

static void
test_select_drag(void)
{
	int		row, x, y;

	row = select_setup();

	test_cell_pos(row, 0, &x, &y);
	test_mouse(SDL_MOUSEBUTTONDOWN, x, y, SDL_BUTTON_LEFT, 1);

	test_cell_pos(row, 4, &x, &y);
	test_mouse(SDL_MOUSEMOTION, x, y, 0, 0);
	test_mouse(SDL_MOUSEBUTTONUP, x, y, SDL_BUTTON_LEFT, 1);

	select_expect("alpha");
}

static void
test_select_word(void)
{
	int		row, x, y;

	row = select_setup();

	test_cell_pos(row, 7, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	select_expect("beta");
}

static void
test_select_line(void)
{
	int		row, x, y;

	row = select_setup();

	test_cell_pos(row, 2, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 3);

	select_expect(SELECT_TEXT);
}

static void
test_select_paste(void)
{
	int		x, y;

	test_coma_start();

	coma_wm_selection_set("echo pasted$((2 + 3))");

	test_cell_pos(0, 0, &x, &y);
	test_click(x, y, SDL_BUTTON_MIDDLE, 1);

	test_type("\n");
	test_wait("pasted5");
}

static void
test_select_blank(void)
{
	int		row, x, y;

	row = select_setup();

	test_cell_pos(row, 7, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	test_cell_pos(row, 40, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	select_expect("beta");
}

static void
test_select_hidden(void)
{
	int		row, x, y;

	test_coma_start();

	test_type("printf 'left\\033[8mX\\033[0mright\\n'\n");
	test_wait("left right");

	row = test_find("left right");

	test_cell_pos(row, 0, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 3);

	select_expect("left right");
}

static void
test_select_sb_wide(void)
{
	int		row, x, y;
	char		line[512];

	test_coma_start();

	test_type("echo wide-$(printf 'x%.0s' $(seq 1 50))-end; seq 1 80; "
	    "echo do$((1 + 1))ne\n");
	test_wait("do2ne");

	test_resize(400, 768);
	test_pump(50);

	for (row = -1; row > -200; row--) {
		if (coma_term_line(client_active->term, row, line,
		    sizeof(line)) == 0 && strncmp(line, "wide-", 5) == 0)
			break;
	}

	coma_term_scroll(client_active->term, -row);

	test_cell_pos(0, 1, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 3);

	select_expect("wide-xxxxxxxxxxxxxxxxxxxxxxxxx"
	    "xxxxxxxxxxxxxxxxxxxxxxxxx-end");
}

static void
test_select_wide_word(void)
{
	int		row, x, y;

	test_coma_start();

	test_type("printf 'ab \\344\\270\\200\\344\\272\\214 cd\\n'\n");
	test_wait("\344\270\200\344\272\214 cd");

	row = test_find("\344\270\200\344\272\214 cd");

	test_cell_pos(row, 5, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	select_expect("\344\270\200\344\272\214");
}

static void
test_select_wide_wrap(void)
{
	int		row, x, y;
	char		*text;

	test_coma_start();

	test_type("clear; printf 'x%.0s' $(seq 2 $(tput cols)); "
	    "printf '\\344\\270\\200end\\n'\n");
	test_wait("end");

	row = test_find("end");

	test_cell_pos(row, 0, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 3);
	test_pump(50);

	if ((text = coma_wm_selection_get()) == NULL)
		fatal("nothing was selected");

	if (strchr(text, '\n') != NULL ||
	    strstr(text, "x\344\270\200end") == NULL)
		fatal("selected '%s'", text);

	free(text);
}

static void
test_select_word_wrap(void)
{
	int		row, x, y;
	char		line[512];

	test_coma_start();

	test_type("clear; printf ' %.0s' $(seq 4 $(tput cols)); "
	    "printf 'abc\\144ef\\n'\n");
	test_wait("def");

	row = test_find("def");

	test_cell_pos(row, 1, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	select_expect("abcdef");

	if (coma_term_line(client_active->term,
	    row - 1, line, sizeof(line)) == -1)
		fatal("failed to read line %d", row - 1);

	test_cell_pos(row - 1, strlen(line) - 2, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 2);

	select_expect("abcdef");
}

static int
select_setup(void)
{
	test_coma_start();

	test_type("echo alpha be''ta gamma\n");
	test_wait(SELECT_TEXT);

	return (test_find(SELECT_TEXT));
}

static void
select_expect(const char *expect)
{
	char	*text;

	test_pump(50);

	if ((text = coma_wm_selection_get()) == NULL)
		fatal("nothing was selected");

	if (strcmp(text, expect))
		fatal("selected '%s', expected '%s'", text, expect);

	free(text);
}
