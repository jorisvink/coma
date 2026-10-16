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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framework.h"

static void	test_term_echo(void);
static void	test_term_exit(void);
static void	test_term_meta(void);
static void	test_term_title(void);
static void	test_term_altgr(void);
static void	test_term_ctrl_c(void);
static void	test_term_resize(void);
static void	test_term_prefix(void);
static void	test_term_autowrap(void);
static void	test_term_back_tab(void);
static void	test_term_vt_reset(void);
static void	test_term_alt_wheel(void);
static void	test_term_combining(void);
static void	test_term_wrap_copy(void);
static void	test_term_sgr_mixed(void);
static void	test_term_vt_cursor(void);
static void	test_term_vt_escape(void);
static void	test_term_vt_replies(void);
static void	test_term_cpr_origin(void);
static void	test_term_ctrl_enter(void);
static void	test_term_ctrl_digit(void);
static void	test_term_ctrl_grave(void);
static void	test_term_soft_reset(void);
static void	test_term_scrollback(void);
static void	test_term_utf8_split(void);
static void	test_term_title_home(void);
static void	test_term_alt_resize(void);
static void	test_term_altgr_late(void);
static void	test_term_not_reading(void);
static void	test_term_wide_delete(void);
static void	test_term_wide_insert(void);
static void	test_term_bad_margins(void);

static int	term_no_clients(void);
static void	term_text(const char *);
static struct vt	*term_vt(const char *);
static void	term_size_expect(char *, size_t);
static void	term_title_set(const char *, int);
static void	term_vt_output(void *, const char *, size_t);
static void	term_title_expect(const char *, const char *);
static void	term_key_text(SDL_Keycode, u_int16_t, const char *);

static size_t	vt_len;
static char	vt_out[512];

void
test_entry(void)
{
	test_framework_register("term-echo", test_term_echo);
	test_framework_register("term-exit", test_term_exit);
	test_framework_register("term-meta", test_term_meta);
	test_framework_register("term-title", test_term_title);
	test_framework_register("term-altgr", test_term_altgr);
	test_framework_register("term-ctrl-c", test_term_ctrl_c);
	test_framework_register("term-resize", test_term_resize);
	test_framework_register("term-prefix", test_term_prefix);
	test_framework_register("term-autowrap", test_term_autowrap);
	test_framework_register("term-back-tab", test_term_back_tab);
	test_framework_register("term-vt-reset", test_term_vt_reset);
	test_framework_register("term-alt-wheel", test_term_alt_wheel);
	test_framework_register("term-combining", test_term_combining);
	test_framework_register("term-wrap-copy", test_term_wrap_copy);
	test_framework_register("term-sgr-mixed", test_term_sgr_mixed);
	test_framework_register("term-vt-cursor", test_term_vt_cursor);
	test_framework_register("term-vt-escape", test_term_vt_escape);
	test_framework_register("term-vt-replies", test_term_vt_replies);
	test_framework_register("term-cpr-origin", test_term_cpr_origin);
	test_framework_register("term-ctrl-enter", test_term_ctrl_enter);
	test_framework_register("term-ctrl-digit", test_term_ctrl_digit);
	test_framework_register("term-ctrl-grave", test_term_ctrl_grave);
	test_framework_register("term-soft-reset", test_term_soft_reset);
	test_framework_register("term-scrollback", test_term_scrollback);
	test_framework_register("term-utf8-split", test_term_utf8_split);
	test_framework_register("term-title-home", test_term_title_home);
	test_framework_register("term-alt-resize", test_term_alt_resize);
	test_framework_register("term-altgr-late", test_term_altgr_late);
	test_framework_register("term-not-reading", test_term_not_reading);
	test_framework_register("term-wide-delete", test_term_wide_delete);
	test_framework_register("term-wide-insert", test_term_wide_insert);
	test_framework_register("term-bad-margins", test_term_bad_margins);

	test_framework_run();
}

static void
test_term_echo(void)
{
	test_coma_start();

	test_type("echo foo$((40 + 2))\n");
	test_wait("foo42");
}

static void
test_term_exit(void)
{
	test_coma_start();

	test_type("exit\n");
	test_until(term_no_clients, "the client to go away");
}

static void
test_term_ctrl_c(void)
{
	test_coma_start();

	test_type("sleep 100\n");
	test_pump(300);

	test_key('c', KMOD_LCTRL);

	test_type("echo back$((1 + 1))\n");
	test_wait("back2");
}

static void
test_term_ctrl_enter(void)
{
	test_coma_start();

	test_type("echo foo$((1 + 2))");
	test_key(SDLK_RETURN, KMOD_LCTRL);
	test_wait("foo3");

	test_type("echo bar$((2 + 2))");
	test_key(SDLK_RETURN, KMOD_LSHIFT);
	test_wait("bar4");
}

static void
test_term_ctrl_digit(void)
{
	test_coma_start();

	test_type("echo x");
	test_key('1', KMOD_LCTRL);
	test_key('9', KMOD_LCTRL);
	test_key('.', KMOD_LCTRL);
	test_key(SDLK_BACKSPACE, KMOD_LSHIFT);
	test_type(" | tr 19 ab\n");
	test_wait("xab");
}

static void
test_term_ctrl_grave(void)
{
	test_coma_start();

	test_type("od -An -c\n");
	test_key('`', KMOD_LCTRL);
	test_key(SDLK_RETURN, 0);
	test_key('d', KMOD_LCTRL);
	test_wait("\\0");
}

static void
test_term_soft_reset(void)
{
	test_coma_start();

	test_type("printf 'abc\\033[1G\\033[4h\\033[!pX\\n'\n");
	test_wait("Xbc");
}

static void
test_term_bad_margins(void)
{
	test_coma_start();

	test_type("printf 'abc\\033[3;2rX\\n'\n");
	test_wait("abcX");
}

static void
test_term_vt_escape(void)
{
	struct vt	*vt;
	struct vt_cell	*row;

	vt = term_vt("a\033 Fb\033-Ac\033&4d");
	row = coma_vt_row(vt, 0);

	if (row[0].u != 'a' || row[1].u != 'b' || row[2].u != 'c' ||
	    row[3].u != 'd')
		fatal("escape sequence intermediates were printed");

	coma_vt_free(vt);
}

static void
test_term_vt_reset(void)
{
	struct vt	*vt;
	struct vt_cell	*row;
	int		col, line, shape;

	vt = term_vt("\033[6 q\033[!p");
	coma_vt_cursor(vt, &col, &line, &shape);

	if (shape != COMA_VT_CURSOR_BLOCK)
		fatal("DECSTR kept cursor shape %d", shape);

	coma_vt_free(vt);

	vt = term_vt("\033[5;10r\033[3;7H\033[31m\033#8X");
	row = coma_vt_row(vt, 0);
	coma_vt_cursor(vt, &col, &line, &shape);

	if (row[0].u != 'X' || row[0].fg != COMA_VT_FG_DEFAULT ||
	    line != 0 || col != 1)
		fatal("DECALN did not reset cursor and attributes");

	coma_vt_free(vt);

	vt = term_vt("\033[5;10r\033#8\033[1;1HX\033[24;1H\n");
	row = coma_vt_row(vt, 0);

	if (row[0].u != 'E')
		fatal("DECALN did not reset the scroll region");

	coma_vt_free(vt);
}

static void
test_term_vt_cursor(void)
{
	struct vt	*vt;
	struct vt_cell	*row;
	int		col, line, shape;

	vt = term_vt("\033[5;10r\033[7;1H\033[20A");
	coma_vt_cursor(vt, &col, &line, &shape);
	if (line != 4)
		fatal("CUU went past the top margin to row %d", line);
	coma_vt_free(vt);

	vt = term_vt("\033[5;10r\033[2;1H\033[20B");
	coma_vt_cursor(vt, &col, &line, &shape);
	if (line != 9)
		fatal("CUD went past the bottom margin to row %d", line);
	coma_vt_free(vt);

	vt = term_vt("\033[3;5H\033[L");
	coma_vt_cursor(vt, &col, &line, &shape);
	if (col != 0)
		fatal("IL left the cursor in column %d", col);
	coma_vt_free(vt);

	vt = term_vt("\033[1;80HZ\0337\033[5;5H\0338Q");
	row = coma_vt_row(vt, 1);
	if (row[0].u != 'Q')
		fatal("DECRC did not restore the pending wrap");
	coma_vt_free(vt);

	vt = term_vt("\033(0\0337\033(B\0338q");
	row = coma_vt_row(vt, 0);
	if (row[0].u != 0x2500)
		fatal("DECRC did not restore the charset");
	coma_vt_free(vt);
}

static void
test_term_vt_replies(void)
{
	struct vt	*vt;

	vt = term_vt("\033]4;1;?;2;?\007");
	if (strstr(vt_out, "\033]4;1;rgb:") == NULL ||
	    strstr(vt_out, "\033]4;2;rgb:") == NULL)
		fatal("OSC 4 did not answer every color");
	coma_vt_free(vt);

	vt = term_vt("\033]10;?;?\033\\");
	if (strstr(vt_out, "\033]10;rgb:") == NULL ||
	    strstr(vt_out, "\033]11;rgb:") == NULL ||
	    strchr(vt_out, '\007') != NULL)
		fatal("OSC 10 reply was wrong");
	coma_vt_free(vt);

	vt = term_vt("\033[3;5H\033[?6n");
	if (strcmp(vt_out, "\033[?3;5R") != 0)
		fatal("DECXCPR reply was wrong");
	coma_vt_free(vt);
}

static void
test_term_sgr_mixed(void)
{
	struct vt_callbacks	cb;
	struct vt		*vt;
	const char		*seq;
	struct vt_cell		*cell;

	memset(&cb, 0, sizeof(cb));

	seq = "\033[4:3;31mX";
	vt = coma_vt_create(10, 2, &cb, NULL);

	(void)coma_vt_write(vt, seq, strlen(seq));
	cell = coma_vt_row(vt, 0);

	if (cell->fg != 1 || (cell->mode & COMA_VT_ATTR_UNDERLINE) == 0)
		fatal("got fg %u mode 0x%x", cell->fg, cell->mode);

	coma_vt_free(vt);
}

static void
test_term_resize(void)
{
	char	before[32], after[32];

	test_coma_start();

	term_size_expect(before, sizeof(before));
	test_type("stty size\n");
	test_wait(before);

	test_resize(800, 600);
	term_size_expect(after, sizeof(after));

	if (strcmp(before, after) == 0)
		fatal("resize did not change the size (%s)", after);

	test_type("stty size\n");
	test_wait(after);
}

static void
test_term_alt_resize(void)
{
	test_coma_start();

	test_type("printf 'keep%d\\n\\033[?1049h\\033[999;1Halt%d' 1 2\n");
	test_wait("alt2");

	test_resize(screen_width, screen_height / 3);

	test_type("printf '\\033[?1049lback%d\\n' 3\n");
	test_wait("back3");

	if (test_find("keep1") == -1)
		fatal("primary screen lost its content on resize");
}

static void
test_term_scrollback(void)
{
	int		row;
	char		line[64];

	test_coma_start();

	test_type("seq 1 500\n");
	test_wait("500");

	for (row = -1; ; row--) {
		if (coma_term_line(client_active->term,
		    row, line, sizeof(line)) == -1)
			fatal("line '1' not found in the scrollback");

		if (strcmp(line, "1") == 0)
			break;
	}
}

static void
test_term_utf8_split(void)
{
	test_coma_start();

	test_type("printf 'x\\342\\224'; sleep 0.5; printf '\\200 split\\n'\n");
	test_wait("x\342\224\200 split");
}

static void
test_term_not_reading(void)
{
	test_coma_start();

	test_type("stty raw -echo; printf '\\033[c%.0s' $(seq 8000); "
	    "echo; echo done$((4 + 4)); sleep 2; stty sane\n");
	test_wait("done8");
}

static void
test_term_title(void)
{
	test_coma_start();

	test_type("printf '\\033]0;myhost;/tmp/some dir;vim\\007'; "
	    "echo done$((5 + 5))\n");
	test_wait("done10");

	term_title_expect(client_active->cmd, "vim");
	term_title_expect(client_active->host, "myhost");
	term_title_expect(client_active->pwd, "/tmp/some dir");
}

static void
test_term_meta(void)
{
	test_coma_start();

	test_type("cat -v\n");
	test_pump(100);

	term_key_text('b', KMOD_LALT, "b");
	term_key_text('f', KMOD_LALT, NULL);
	test_type("\n");

	test_wait("^[b^[f");
}

static void
test_term_altgr(void)
{
	test_coma_start();

	test_type("echo x");
	term_key_text('7', KMOD_RALT, "{");
	term_key_text('0', KMOD_RALT, "}");
	test_type("y | tr x z\n");

	test_wait("z{}y");
}

static void
test_term_altgr_late(void)
{
	test_coma_start();

	test_type("cat -v\n");
	test_pump(100);

	term_key_text('<', KMOD_RALT, NULL);
	term_key_text(' ', KMOD_NONE, NULL);
	term_text("|");
	term_text(" ");
	test_type("x\n");

	test_wait("| x");

	if (test_find("^[") != -1)
		fatal("altgr text was sent as a meta key");
}

static void
test_term_prefix(void)
{
	test_coma_start();

	test_type("cat -v\n");
	test_pump(100);

	test_key('t', KMOD_LCTRL);
	test_key('t', KMOD_LCTRL);
	test_type("\n");

	test_wait("^T");
}

static void
test_term_alt_wheel(void)
{
	test_coma_start();

	test_type("printf '\\033[?1049h'; cat -v\n");
	test_pump(100);

	coma_term_wheel(client_active->term, 1);
	test_type("\n");

	test_wait("^[[A");
}

static void
test_term_combining(void)
{
	test_coma_start();

	test_type("printf 'cafe\\314\\201 ok\\n'\n");
	test_wait("cafe\314\201 ok");
}

static void
test_term_wrap_copy(void)
{
	char		*text;
	int		row, x, y;

	test_coma_start();

	test_type("clear; printf 'A%.0s' $(seq 1 120); "
	    "printf 'B%.0s' $(seq 1 30); echo\n");
	test_wait("BBBBBBBBBB");

	row = test_find("AAAAAAAAAA");
	test_cell_pos(row, 0, &x, &y);
	test_click(x, y, SDL_BUTTON_LEFT, 3);

	if ((text = coma_wm_selection_get()) == NULL)
		fatal("nothing was selected");

	if (strlen(text) != 150 || strchr(text, '\n') != NULL)
		fatal("wrapped line was copied as '%s'", text);

	free(text);
}

static void
test_term_title_home(void)
{
	char	path[PATH_MAX];

	test_coma_start();

	term_title_set("X/src", 1);
	(void)snprintf(path, sizeof(path), "%sX/src", homedir);
	term_title_expect(client_active->pwd, path);

	term_title_set("/src", 2);
	term_title_expect(client_active->pwd, "~/src");
}

static void
test_term_autowrap(void)
{
	int		row;
	size_t		len;
	char		line[512];

	test_coma_start();

	test_type("printf 'mark%d\\033[?7l\\033[999GXY\\033[?7hZ\\n' 42\n");
	test_wait("mark42");

	row = test_find("mark42");

	if (coma_term_line(client_active->term, row, line, sizeof(line)) == -1)
		fatal("failed to read line %d", row);

	if ((len = strlen(line)) == 0 || line[len - 1] != 'Z')
		fatal("line is '%s'", line);
}

static void
test_term_back_tab(void)
{
	int		row;
	size_t		len;
	char		line[512];

	test_coma_start();

	test_type("printf 'mark%d\\033[999GQ\\033[ZXY\\n' 42\n");
	test_wait("mark42");

	row = test_find("mark42");

	if (coma_term_line(client_active->term, row, line, sizeof(line)) == -1)
		fatal("failed to read line %d", row);

	if (strstr(line, "XY") == NULL ||
	    (len = strlen(line)) == 0 || line[len - 1] != 'Q')
		fatal("line is '%s'", line);
}

static void
test_term_cpr_origin(void)
{
	test_coma_start();

	test_type("stty -echo -icanon; "
	    "printf '\\033[5;10r\\033[?6h\\033[3;4H\\033[6n'; "
	    "dd bs=1 count=6 2>/dev/null | tr '\\033' E; "
	    "printf '\\033[?6l\\033[r\\n'; stty sane\n");
	test_wait("E[3;4R");
}

static void
test_term_wide_delete(void)
{
	test_coma_start();

	test_type("printf '\\344\\270\\200xyz\\033[2G\\033[P\\n'\n");
	test_wait(" xyz");
}

static void
test_term_wide_insert(void)
{
	test_coma_start();

	test_type("printf '\\033[999G\\033[D\\344\\270\\200"
	    "\\033[1G\\033[@done\\n'\n");
	test_wait("done");

	if (test_find("\344\270\200") != -1)
		fatal("wide character left in the last column");
}

static int
term_no_clients(void)
{
	return (test_clients() == 0);
}

static void
term_size_expect(char *buf, size_t len)
{
	int		n;
	u_int16_t	cw, ch;

	coma_draw_cell_size(&cw, &ch);

	n = snprintf(buf, len, "%d %d",
	    (client_active->h - (COMA_TERM_PAD * 2)) / ch,
	    (client_active->w - (COMA_TERM_PAD * 2)) / cw);
	if (n == -1 || (size_t)n >= len)
		fatal("failed to construct the expected size");
}

static void
term_title_expect(const char *have, const char *want)
{
	if (have == NULL || strcmp(have, want) != 0) {
		fatal("expected '%s' from the title, got '%s'", want,
		    have != NULL ? have : "(null)");
	}
}

static void
term_text(const char *text)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = SDL_TEXTINPUT;
	(void)strlcpy(evt.text.text, text, sizeof(evt.text.text));

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	test_pump(10);
}

static struct vt *
term_vt(const char *input)
{
	struct vt				*vt;
	static const struct vt_callbacks	cb = {
		.output = term_vt_output
	};

	vt_len = 0;
	vt_out[0] = '\0';

	vt = coma_vt_create(80, 24, &cb, NULL);
	(void)coma_vt_write(vt, input, strlen(input));

	return (vt);
}

static void
term_vt_output(void *arg, const char *data, size_t len)
{
	if (len >= sizeof(vt_out) - vt_len)
		fatal("vt output too large");

	memcpy(&vt_out[vt_len], data, len);
	vt_len += len;
	vt_out[vt_len] = '\0';
}

static void
term_key_text(SDL_Keycode sym, u_int16_t mod, const char *text)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = SDL_KEYDOWN;
	evt.key.state = SDL_PRESSED;
	evt.key.keysym.sym = sym;
	evt.key.keysym.mod = mod;

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	if (text != NULL) {
		memset(&evt, 0, sizeof(evt));

		evt.type = SDL_TEXTINPUT;
		(void)strlcpy(evt.text.text, text, sizeof(evt.text.text));

		if (SDL_PushEvent(&evt) < 0)
			fatal("SDL_PushEvent: %s", SDL_GetError());
	}

	test_pump(10);
}

static void
term_title_set(const char *suffix, int mark)
{
	int	len;
	char	cmd[512], wait[32];

	len = snprintf(cmd, sizeof(cmd),
	    "printf '\\033]0;h;%s%s;vi\\007'; echo mark$((%d + 1))\n",
	    homedir, suffix, mark);
	if (len < 0 || (size_t)len >= sizeof(cmd))
		fatal("title command is too long");

	(void)snprintf(wait, sizeof(wait), "mark%d", mark + 1);

	test_type(cmd);
	test_wait(wait);
}
