/*
 * Copyright (c) 2019-2026 Joris Vink <joris@sanctorum.se>
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
#include <sys/ioctl.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#if defined(__linux__)
#include <pty.h>
#elif defined(__FreeBSD__)
#include <libutil.h>
#else
#include <util.h>
#endif

#if defined(__APPLE__)
#include <libproc.h>
#endif

#if defined(__OpenBSD__)
#include <sys/sysctl.h>
#endif

#include "coma.h"

#define TERM_WORD_CHARS		"!#%*-./@_~"
#define TERM_PAD		COMA_TERM_PAD

#define TERM_READ_TIME		8
#define TERM_READ_MAX		(256 * 1024)
#define TERM_OUTPUT_MAX		(4 * 1024 * 1024)
#define TERM_CELL_TEXT		(4 * (COMA_VT_COMB_MAX + 1))

#define CELL_BOLD		0x01
#define CELL_UNDERLINE		0x02
#define CELL_ITALIC		0x04
#define CELL_STRIKE		0x08
#define CELL_DEFBG		0x10
#define CELL_DEFFG		0x20
#define CELL_WRAP		0x40
#define CELL_REVERSE		0x80

#define SEL_NONE		0
#define SEL_ACTIVE		1
#define SEL_DONE		2

struct cell {
	u_int32_t		ch;
	struct color		fg;
	struct color		bg;
	u_int8_t		attr;
	u_int8_t		width;
	u_int32_t		comb[COMA_VT_COMB_MAX];
};

struct term_pos {
	int			row;
	int			col;
};

struct term_rect {
	int			start_row;
	int			end_row;
	int			start_col;
	int			end_col;
};

struct sbline {
	int			cols;
	struct cell		*cells;
};

struct term {
	int			fd;
	pid_t			pid;

	struct vt		*vt;

	int			ox;
	int			oy;
	int			rows;
	int			cols;

	size_t			utf8_len;
	char			utf8[4];

	size_t			olen;
	int			paste;
	size_t			osize;
	char			*obuf;

	int			focused;

	struct sbline		*sb;
	int			scroll;
	int			sb_max;
	int			sb_head;
	int			sb_count;

	int			sel_mode;
	int			sel_state;
	struct term_pos		sel_end;
	struct term_pos		sel_start;
	struct term_pos		sel_anchor;

	struct client		*client;
};

static u_int64_t	term_msec(void);
static void		term_colors_default(void);
static int		term_word_char(u_int32_t);
static void		term_winsize(struct term *);
static void		term_child(char **, const char *);
static struct sbline	*term_sb_line(struct term *, int);
static int		term_row_cols(struct term *, int);
static char		*term_selection_text(struct term *);
static int		term_row_wrapped(struct term *, int);
static size_t		term_cell_text(struct cell *, char *);
static int		term_selected(struct term *, int, int);
static void		term_sel_shift(struct term *, int, int);
static struct term	*term_setup(int, pid_t, struct client *);
static void		term_cell(struct term *, int, int, struct cell *);
static void		term_select_update(struct term *, struct term_pos *);
static int		term_sel_in_rect(struct term *, struct term_rect *,
			    int);
static void		term_pos(struct term *, int, int, struct term_pos *,
			    int);
static void		term_cell_convert(const struct vt_cell *,
			    struct cell *);

static void	term_cb_sb_clear(void *);
static void	term_cb_title(void *, const char *);
static void	term_cb_scroll(void *, int, int, int);
static void	term_output(void *, const char *, size_t);
static void	term_cb_damage(void *, int, int, int, int);
static void	term_cb_scrollout(void *, const struct vt_cell *, int);

static struct color	def_fg;
static struct color	def_bg;

static const struct vt_callbacks term_callbacks = {
	.title = term_cb_title,
	.output = term_output,
	.scroll = term_cb_scroll,
	.damage = term_cb_damage,
	.sbclear = term_cb_sb_clear,
	.scrollout = term_cb_scrollout,
};

struct term *
coma_term_spawn(char **argv, const char *cwd, struct client *client, int secure)
{
	struct winsize		ws;
	pid_t			pid;
	int			mfd, sfd;

	memset(&ws, 0, sizeof(ws));
	ws.ws_row = 24;
	ws.ws_col = 80;

	if (openpty(&mfd, &sfd, NULL, NULL, &ws) == -1) {
		coma_log("openpty: %s", errno_s);
		return (NULL);
	}

	if ((pid = fork()) == -1) {
		coma_log("fork: %s", errno_s);
		(void)close(sfd);
		(void)close(mfd);
		return (NULL);
	}

	if (pid == 0) {
		(void)close(mfd);
		if (login_tty(sfd) == -1)
			_exit(1);
		if (secure)
			coma_platform_sandbox();
		term_child(argv, cwd);
		/* NOTREACHED */
	}

	(void)close(sfd);

	coma_log("spawned '%s' as pid %d on fd %d (secure=%d)",
	    argv[0], pid, mfd, secure);

	return (term_setup(mfd, pid, client));
}

void
coma_term_free(struct term *term)
{
	int		i;

	(void)close(term->fd);
	coma_vt_free(term->vt);

	for (i = 0; i < term->sb_max; i++)
		free(term->sb[i].cells);

	free(term->sb);
	free(term->obuf);
	free(term);
}

int
coma_term_fd(struct term *term)
{
	return (term->fd);
}

pid_t
coma_term_pid(struct term *term)
{
	return (term->pid);
}

int
coma_term_cwd(struct term *term, char *buf, size_t len)
{
#if defined(__linux__)
	ssize_t		ret;
	char		path[64];

	(void)snprintf(path, sizeof(path), "/proc/%d/cwd", term->pid);

	ret = readlink(path, buf, len - 1);
	if (ret == -1 || (size_t)ret >= len - 1)
		return (-1);

	buf[ret] = '\0';

	return (0);
#elif defined(__APPLE__)
	struct proc_vnodepathinfo	vpi;

	if (proc_pidinfo(term->pid, PROC_PIDVNODEPATHINFO, 0,
	    &vpi, sizeof(vpi)) != sizeof(vpi))
		return (-1);

	if (strlcpy(buf, vpi.pvi_cdir.vip_path, len) >= len)
		return (-1);

	return (0);
#elif defined(__OpenBSD__)
	size_t		size;
	int		mib[3];

	size = len;
	mib[0] = CTL_KERN;
	mib[2] = term->pid;
	mib[1] = KERN_PROC_CWD;

	if (sysctl(mib, 3, buf, &size, NULL, 0) == -1)
		return (-1);

	return (0);
#else
	return (-1);
#endif
}

int
coma_term_pending(struct term *term)
{
	return (term->olen > 0);
}

void
coma_term_flush(struct term *term)
{
	ssize_t		ret;
	size_t		off;

	off = 0;

	while (off < term->olen) {
		ret = write(term->fd, term->obuf + off, term->olen - off);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			coma_log("write to pid %d: %s", term->pid, errno_s);
			off = term->olen;
			break;
		}

		off += ret;
	}

	if (off > 0) {
		memmove(term->obuf, term->obuf + off, term->olen - off);
		term->olen -= off;
	}
}

int
coma_term_read(struct term *term)
{
	ssize_t		ret;
	u_int64_t	start;
	size_t		total, len, used;
	char		buf[8192 + sizeof(term->utf8)];

	total = 0;
	start = term_msec();

	while (total < TERM_READ_MAX) {
		if (term_msec() - start >= TERM_READ_TIME)
			break;

		memcpy(buf, term->utf8, term->utf8_len);
		ret = read(term->fd, buf + term->utf8_len,
		    sizeof(buf) - term->utf8_len);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return (-1);
		}

		if (ret == 0)
			return (-1);

		len = term->utf8_len + ret;
		used = coma_vt_write(term->vt, buf, len);

		if ((term->utf8_len = len - used) > sizeof(term->utf8))
			term->utf8_len = 0;

		memcpy(term->utf8, buf + used, term->utf8_len);

		total += ret;
	}

	if (total > 0) {
		term->scroll = 0;
		coma_wm_dirty();
	}

	return (0);
}

void
coma_term_resize(struct term *term, u_int16_t w, u_int16_t h)
{
	u_int16_t	cw, ch;
	int		rows, cols;

	coma_draw_cell_size(&cw, &ch);

	if (w > TERM_PAD * 2)
		cols = (w - TERM_PAD * 2) / cw;
	else
		cols = 1;

	if (h > TERM_PAD * 2)
		rows = (h - TERM_PAD * 2) / ch;
	else
		rows = 1;

	if (cols < 1)
		cols = 1;
	if (rows < 1)
		rows = 1;

	if (rows == term->rows && cols == term->cols)
		return;

	term->scroll = 0;
	term->rows = rows;
	term->cols = cols;
	term->sel_state = SEL_NONE;

	coma_vt_resize(term->vt, cols, rows);
	term_winsize(term);

	coma_wm_dirty();
}

void
coma_term_draw(struct term *term, int x, int y, int w, int h, int focused)
{
	struct cell		cell;
	u_int16_t		cw, ch;
	struct color		fg, bg, tmp, cc;
	int			defbg, cursor, style;
	int			i, row, col, r, px, py, width;
	int			mode, crow, ccol, shape, visible, reverse;

	term_colors_default();
	coma_draw_cell_size(&cw, &ch);

	mode = coma_vt_mode(term->vt);
	coma_vt_cursor(term->vt, &ccol, &crow, &shape);

	visible = (mode & COMA_VT_MODE_HIDE) == 0;
	reverse = (mode & COMA_VT_MODE_REVERSE) != 0;

	if (reverse) {
		cc = def_bg;
		coma_draw_fill(x, y, w, h, &def_fg);
	} else {
		cc = def_fg;
		coma_draw_fill(x, y, w, h, &def_bg);
	}

	term->ox = x + TERM_PAD;
	term->oy = y + TERM_PAD;

	coma_draw_clip(x, y, w, h);

	for (row = 0; row < term->rows; row++) {
		r = row - term->scroll;
		py = term->oy + (row * ch);

		for (col = 0; col < term->cols; col++) {
			term_cell(term, r, col, &cell);

			fg = cell.fg;
			bg = cell.bg;
			px = term->ox + (col * cw);
			defbg = (cell.attr & (CELL_DEFBG | CELL_REVERSE)) ==
			    CELL_DEFBG;

			if (reverse && (cell.attr & CELL_DEFFG)) {
				if (cell.attr & CELL_REVERSE)
					fg = def_fg;
				else
					fg = def_bg;
			}

			if (reverse && (cell.attr & CELL_DEFBG)) {
				if (cell.attr & CELL_REVERSE)
					bg = def_bg;
				else
					bg = def_fg;
			}

			cursor = focused && visible && term->scroll == 0 &&
			    r == crow && col == ccol &&
			    shape == COMA_VT_CURSOR_BLOCK;

			if (term_selected(term, r, col) ^ cursor) {
				tmp = fg;
				fg = bg;
				bg = tmp;
				defbg = 0;
			}

			if (cell.width > 1 && col + 1 < term->cols)
				width = 2;
			else
				width = 1;

			if (defbg == 0)
				coma_draw_fill(px, py, cw * width, ch, &bg);

			if (cell.attr & CELL_BOLD)
				style = COMA_DRAW_TERM_BOLD;
			else
				style = COMA_DRAW_TERM;

			if (cell.ch != 0 && cell.ch != ' ' &&
			    cell.ch != (u_int32_t)-1)
				coma_draw_glyph(style, px, py, cell.ch, &fg);

			for (i = 0; i < COMA_VT_COMB_MAX &&
			    cell.comb[i] != 0; i++) {
				coma_draw_mark(style, px, py, cw * width,
				    cell.comb[i], &fg);
			}

			if (cell.attr & CELL_UNDERLINE) {
				coma_draw_fill(px, py + ch - 1,
				    cw * width, 1, &fg);
			}

			if (cell.attr & CELL_STRIKE) {
				coma_draw_fill(px, py + ch / 2,
				    cw * width, 1, &fg);
			}

			col += width - 1;
		}
	}

	if (visible && term->scroll == 0 &&
	    crow < term->rows && ccol < term->cols) {
		px = term->ox + (ccol * cw);
		py = term->oy + (crow * ch);

		width = 1;
		term_cell(term, crow, ccol, &cell);

		if (cell.width > 1 && ccol + 1 < term->cols)
			width = 2;

		if (focused == 0) {
			coma_draw_box(px, py, cw * width, ch, 1, &cc);
		} else if (shape == COMA_VT_CURSOR_UNDERLINE) {
			coma_draw_fill(px, py + ch - 2, cw * width, 2, &cc);
		} else if (shape == COMA_VT_CURSOR_BAR) {
			coma_draw_fill(px, py, 2, ch, &cc);
		}
	}

	coma_draw_unclip();
}

void
coma_term_focus(struct term *term, int focus)
{
	if (term->focused == focus)
		return;

	term->focused = focus;
	coma_vt_focus(term->vt, focus);
}

void
coma_term_key(struct term *term, int key, u_int32_t mods)
{
	if (term->scroll != 0) {
		term->scroll = 0;
		coma_wm_dirty();
	}

	coma_vt_key(term->vt, key, mods);
}

void
coma_term_unichar(struct term *term, u_int32_t cp, u_int32_t mods)
{
	if (term->scroll != 0) {
		term->scroll = 0;
		coma_wm_dirty();
	}

	coma_vt_unichar(term->vt, cp, mods);
}

void
coma_term_paste(struct term *term, const char *text)
{
	if (term->sel_state != SEL_NONE) {
		term->sel_state = SEL_NONE;
		coma_wm_dirty();
	}

	term->paste = 1;
	coma_vt_paste(term->vt, text);
	term->paste = 0;
}

int
coma_term_line(struct term *term, int row, char *buf, size_t len)
{
	size_t		off;
	int		col;
	struct cell	cell;

	if (len <= TERM_CELL_TEXT || row >= term->rows || -row > term->sb_count)
		return (-1);

	off = 0;

	for (col = 0; col < term->cols && len - off > TERM_CELL_TEXT; col++) {
		term_cell(term, row, col, &cell);

		if (cell.ch != (u_int32_t)-1)
			off += term_cell_text(&cell, &buf[off]);
	}

	while (off > 0 && buf[off - 1] == ' ')
		off--;

	buf[off] = '\0';

	return (0);
}

void
coma_term_scroll(struct term *term, int lines)
{
	int		scroll;

	if (coma_vt_mode(term->vt) & COMA_VT_MODE_ALTSCREEN)
		return;

	scroll = term->scroll + lines;

	if (scroll < 0)
		scroll = 0;

	if (scroll > term->sb_count)
		scroll = term->sb_count;

	if (scroll != term->scroll) {
		term->scroll = scroll;
		coma_wm_dirty();
	}
}

void
coma_term_wheel(struct term *term, int lines)
{
	int		key;

	if (!(coma_vt_mode(term->vt) & COMA_VT_MODE_ALTSCREEN)) {
		coma_term_scroll(term, lines);
		return;
	}

	if (lines > 0) {
		key = COMA_VT_KEY_UP;
	} else {
		key = COMA_VT_KEY_DOWN;
		lines = -lines;
	}

	while (lines-- > 0)
		coma_vt_key(term->vt, key, 0);
}

void
coma_term_scroll_page(struct term *term, int up)
{
	int		lines;

	if ((lines = term->rows / 2) < 1)
		lines = 1;

	if (up == 0)
		lines = -lines;

	coma_term_scroll(term, lines);
}

int
coma_term_mouse_grabbed(struct term *term, u_int32_t mods)
{
	if (mods & COMA_MOD_SHIFT)
		return (0);

	return ((coma_vt_mode(term->vt) & COMA_VT_MODE_MOUSE) != 0);
}

void
coma_term_mouse_button(struct term *term, int px, int py, int button,
    int pressed, u_int32_t mods)
{
	struct term_pos		pos;

	term_pos(term, px, py, &pos, 0);

	if (pressed) {
		coma_vt_mouse(term->vt, COMA_VT_MOUSE_PRESS,
		    button, pos.col, pos.row, mods);
	} else {
		coma_vt_mouse(term->vt, COMA_VT_MOUSE_RELEASE,
		    button, pos.col, pos.row, mods);
	}
}

void
coma_term_mouse_move(struct term *term, int px, int py, u_int32_t mods)
{
	struct term_pos		pos;

	term_pos(term, px, py, &pos, 0);
	coma_vt_mouse(term->vt, COMA_VT_MOUSE_MOTION,
	    0, pos.col, pos.row, mods);
}

void
coma_term_select_start(struct term *term, int px, int py, int clicks)
{
	term_pos(term, px, py, &term->sel_anchor, 1);

	if (clicks > 3)
		clicks = 3;

	term->sel_mode = clicks;
	term->sel_state = SEL_ACTIVE;

	term_select_update(term, &term->sel_anchor);
	coma_wm_dirty();
}

void
coma_term_select_extend(struct term *term, int px, int py)
{
	struct term_pos		pos;

	if (term->sel_state != SEL_ACTIVE)
		return;

	term_pos(term, px, py, &pos, 1);
	term_select_update(term, &pos);

	coma_wm_dirty();
}

void
coma_term_select_end(struct term *term)
{
	char		*text;

	if (term->sel_state != SEL_ACTIVE)
		return;

	if (term->sel_mode == 1 && term->sel_start.row == term->sel_end.row &&
	    term->sel_start.col == term->sel_end.col) {
		term->sel_state = SEL_NONE;
		coma_wm_dirty();
		return;
	}

	term->sel_state = SEL_DONE;

	if ((text = term_selection_text(term)) != NULL) {
		if (text[0] != '\0') {
			coma_wm_selection_set(text);
		} else {
			term->sel_state = SEL_NONE;
			coma_wm_dirty();
		}
		free(text);
	}
}

static u_int64_t
term_msec(void)
{
	struct timespec		ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);

	return (((u_int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

static struct term *
term_setup(int fd, pid_t pid, struct client *client)
{
	int		flags;
	struct term	*term;

	term = coma_calloc(1, sizeof(*term));

	term->fd = fd;
	term->pid = pid;
	term->rows = 24;
	term->cols = 80;
	term->client = client;

	if ((flags = fcntl(fd, F_GETFL)) == -1 ||
	    fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		fatal("fcntl(O_NONBLOCK): %s", errno_s);

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		fatal("fcntl(FD_CLOEXEC): %s", errno_s);

	term->vt = coma_vt_create(term->cols, term->rows,
	    &term_callbacks, term);

	if ((term->sb_max = scrollback_lines) > 0)
		term->sb = coma_calloc(term->sb_max, sizeof(*term->sb));

	return (term);
}

static void
term_child(char **argv, const char *cwd)
{
	sigset_t	set;

	(void)signal(SIGINT, SIG_DFL);
	(void)signal(SIGHUP, SIG_DFL);
	(void)signal(SIGPIPE, SIG_DFL);
	(void)signal(SIGCHLD, SIG_DFL);
	(void)signal(SIGQUIT, SIG_DFL);
	(void)signal(SIGTERM, SIG_DFL);

	(void)sigemptyset(&set);
	(void)sigprocmask(SIG_SETMASK, &set, NULL);

	if (cwd != NULL && chdir(cwd) == -1)
		fprintf(stderr, "chdir(%s): %s\n", cwd, errno_s);

	(void)unsetenv("LINES");
	(void)unsetenv("COLUMNS");
	(void)setenv("TERM", term_name, 1);
	(void)setenv("COLORTERM", "truecolor", 1);

	execvp(argv[0], argv);
	fprintf(stderr, "failed to start '%s': %s\n", argv[0], errno_s);
	_exit(1);
}

static void
term_winsize(struct term *term)
{
	struct winsize		ws;
	u_int16_t		cw, ch;

	coma_draw_cell_size(&cw, &ch);

	memset(&ws, 0, sizeof(ws));
	ws.ws_row = term->rows;
	ws.ws_col = term->cols;
	ws.ws_xpixel = term->cols * cw;
	ws.ws_ypixel = term->rows * ch;

	if (ioctl(term->fd, TIOCSWINSZ, &ws) == -1)
		coma_log("TIOCSWINSZ: %s", errno_s);
}

static void
term_output(void *arg, const char *s, size_t len)
{
	struct term	*term;
	size_t		queued;

	term = arg;

	if (term->paste == 0 && len > TERM_OUTPUT_MAX - term->olen) {
		coma_log("pid %d is not reading, dropped %zu bytes",
		    term->pid, len);
		return;
	}

	if (term->olen + len > term->osize) {
		if ((term->osize *= 2) < term->olen + len)
			term->osize = term->olen + len;
		if ((term->obuf = realloc(term->obuf, term->osize)) == NULL)
			fatal("realloc: %s", errno_s);
	}

	queued = term->olen;

	memcpy(term->obuf + term->olen, s, len);
	term->olen += len;

	coma_term_flush(term);

	if (queued == 0 && term->olen > 0)
		coma_wm_io_update();
}

static void
term_colors_default(void)
{
	def_fg = *coma_wm_color("terminal-foreground");
	def_bg = *coma_wm_color("terminal-background");
}

static void
term_cell(struct term *term, int row, int col, struct cell *cell)
{
	struct sbline		*line;
	const struct vt_cell	*cells;

	if (row >= 0) {
		if ((cells = coma_vt_row(term->vt, row)) != NULL &&
		    col < term->cols) {
			term_cell_convert(&cells[col], cell);
			return;
		}
	} else if (-row <= term->sb_count) {
		line = term_sb_line(term, -row - 1);
		if (col < line->cols) {
			*cell = line->cells[col];
			return;
		}
	}

	memset(cell, 0, sizeof(*cell));
	cell->width = 1;
	cell->fg = def_fg;
	cell->bg = def_bg;
	cell->attr = CELL_DEFBG | CELL_DEFFG;
}

static void
term_cell_convert(const struct vt_cell *vc, struct cell *cell)
{
	struct color		tmp;
	u_int32_t		fg, bg;

	fg = vc->fg;
	bg = vc->bg;

	cell->attr = 0;
	cell->ch = vc->u;
	cell->width = 1;

	memcpy(cell->comb, vc->comb, sizeof(cell->comb));

	if (vc->mode & COMA_VT_ATTR_WDUMMY)
		cell->ch = (u_int32_t)-1;
	if (vc->mode & COMA_VT_ATTR_WIDE)
		cell->width = 2;

	if (vc->mode & COMA_VT_ATTR_BOLD) {
		cell->attr |= CELL_BOLD;
		if (fg < 8)
			fg += 8;
	}

	if (vc->mode & COMA_VT_ATTR_UNDERLINE)
		cell->attr |= CELL_UNDERLINE;
	if (vc->mode & COMA_VT_ATTR_ITALIC)
		cell->attr |= CELL_ITALIC;
	if (vc->mode & COMA_VT_ATTR_STRUCK)
		cell->attr |= CELL_STRIKE;
	if (vc->mode & COMA_VT_ATTR_WRAP)
		cell->attr |= CELL_WRAP;

	if (vc->mode & COMA_VT_ATTR_INVISIBLE) {
		cell->ch = ' ';
		memset(cell->comb, 0, sizeof(cell->comb));
	}

	if (vc->mode & COMA_VT_ATTR_REVERSE) {
		cell->attr |= CELL_REVERSE;
		if (fg == COMA_VT_FG_DEFAULT)
			cell->attr |= CELL_DEFBG;
		if (bg == COMA_VT_BG_DEFAULT)
			cell->attr |= CELL_DEFFG;
	} else {
		if (bg == COMA_VT_BG_DEFAULT)
			cell->attr |= CELL_DEFBG;
		if (fg == COMA_VT_FG_DEFAULT)
			cell->attr |= CELL_DEFFG;
	}

	coma_vt_color(fg, &cell->fg);
	coma_vt_color(bg, &cell->bg);

	if (vc->mode & COMA_VT_ATTR_FAINT) {
		cell->fg.r /= 2;
		cell->fg.g /= 2;
		cell->fg.b /= 2;
	}

	if (vc->mode & COMA_VT_ATTR_REVERSE) {
		tmp = cell->fg;
		cell->fg = cell->bg;
		cell->bg = tmp;
	}
}

static void
term_pos(struct term *term, int px, int py, struct term_pos *pos,
    int scrolled)
{
	u_int16_t	cw, ch;
	int		row, col;

	coma_draw_cell_size(&cw, &ch);

	col = (px - term->ox) / (int)cw;
	row = (py - term->oy) / (int)ch;

	if (px < term->ox || col < 0)
		col = 0;
	if (py < term->oy || row < 0)
		row = 0;
	if (col >= term->cols)
		col = term->cols - 1;
	if (row >= term->rows)
		row = term->rows - 1;

	if (scrolled)
		row -= term->scroll;

	pos->col = col;
	pos->row = row;
}

static int
term_selected(struct term *term, int row, int col)
{
	if (term->sel_state == SEL_NONE)
		return (0);

	if (row < term->sel_start.row || row > term->sel_end.row)
		return (0);

	if (row == term->sel_start.row && col < term->sel_start.col)
		return (0);

	if (row == term->sel_end.row && col > term->sel_end.col)
		return (0);

	return (1);
}

static void
term_select_update(struct term *term, struct term_pos *pos)
{
	struct cell	cell;
	int		col, row, cols;

	if (pos->row < term->sel_anchor.row ||
	    (pos->row == term->sel_anchor.row &&
	    pos->col < term->sel_anchor.col)) {
		term->sel_start = *pos;
		term->sel_end = term->sel_anchor;
	} else {
		term->sel_start = term->sel_anchor;
		term->sel_end = *pos;
	}

	switch (term->sel_mode) {
	case 2:
		for (;;) {
			while (term->sel_start.col > 0) {
				term_cell(term, term->sel_start.row,
				    term->sel_start.col - 1, &cell);
				if (term_word_char(cell.ch) == 0)
					break;
				term->sel_start.col--;
			}
			row = term->sel_start.row - 1;
			if (term->sel_start.col != 0 ||
			    term_row_wrapped(term, row) == 0)
				break;
			term_cell(term, term->sel_start.row, 0, &cell);
			if (term_word_char(cell.ch) == 0)
				break;
			col = term_row_cols(term, row) - 1;
			term_cell(term, row, col, &cell);
			if (term_word_char(cell.ch) == 0)
				break;
			term->sel_start.row = row;
			term->sel_start.col = col;
		}
		for (;;) {
			cols = term_row_cols(term, term->sel_end.row);
			while (term->sel_end.col < cols - 1) {
				term_cell(term, term->sel_end.row,
				    term->sel_end.col + 1, &cell);
				if (term_word_char(cell.ch) == 0)
					break;
				term->sel_end.col++;
			}
			if (term->sel_end.col != cols - 1 ||
			    term->sel_end.row >= term->rows - 1 ||
			    term_row_wrapped(term, term->sel_end.row) == 0)
				break;
			term_cell(term, term->sel_end.row,
			    term->sel_end.col, &cell);
			if (term_word_char(cell.ch) == 0)
				break;
			term_cell(term, term->sel_end.row + 1, 0, &cell);
			if (term_word_char(cell.ch) == 0)
				break;
			term->sel_end.row++;
			term->sel_end.col = 0;
		}
		break;
	case 3:
		while (term_row_wrapped(term, term->sel_start.row - 1))
			term->sel_start.row--;
		while (term->sel_end.row < term->rows - 1 &&
		    term_row_wrapped(term, term->sel_end.row))
			term->sel_end.row++;
		term->sel_start.col = 0;
		term->sel_end.col = term->cols - 1;
		break;
	}
}

static int
term_word_char(u_int32_t cp)
{
	if (cp == 0)
		return (0);

	if (cp >= 0x80)
		return (1);

	if (isalnum((unsigned char)cp))
		return (1);

	return (strchr(TERM_WORD_CHARS, (int)cp) != NULL);
}

static char *
term_selection_text(struct term *term)
{
	struct cell	cell;
	char		*buf;
	size_t		len, cap, line;
	int		row, col, cols, first, last;

	len = 0;
	cap = 256;
	buf = coma_malloc(cap);

	for (row = term->sel_start.row; row <= term->sel_end.row; row++) {
		line = len;
		cols = term_row_cols(term, row);

		if (row == term->sel_start.row)
			first = term->sel_start.col;
		else
			first = 0;

		if (row == term->sel_end.row &&
		    term->sel_end.col < term->cols - 1)
			last = term->sel_end.col;
		else
			last = cols - 1;

		for (col = first; col <= last; col++) {
			if (cap - len < TERM_CELL_TEXT + 2) {
				cap *= 2;
				if ((buf = realloc(buf, cap)) == NULL)
					fatal("realloc: %s", errno_s);
			}

			term_cell(term, row, col, &cell);

			if (cell.ch != 0 && cell.ch != (u_int32_t)-1)
				len += term_cell_text(&cell, &buf[len]);
		}

		if (row != term->sel_end.row && last == cols - 1 &&
		    term_row_wrapped(term, row))
			continue;

		while (len > line && buf[len - 1] == ' ')
			len--;

		if (row != term->sel_end.row)
			buf[len++] = '\n';
	}

	buf[len] = '\0';

	return (buf);
}

static size_t
term_cell_text(struct cell *cell, char *out)
{
	int		i;
	size_t		len;

	if (cell->ch == 0) {
		out[0] = ' ';
		len = 1;
	} else {
		len = coma_utf8_encode(cell->ch, out);
	}

	for (i = 0; i < COMA_VT_COMB_MAX && cell->comb[i] != 0; i++)
		len += coma_utf8_encode(cell->comb[i], &out[len]);

	return (len);
}

static int
term_row_wrapped(struct term *term, int row)
{
	struct cell	cell;

	term_cell(term, row, term_row_cols(term, row) - 1, &cell);

	return ((cell.attr & CELL_WRAP) != 0);
}

static int
term_row_cols(struct term *term, int row)
{
	struct sbline	*line;

	if (row < 0 && -row <= term->sb_count) {
		line = term_sb_line(term, -row - 1);
		return (line->cols);
	}

	return (term->cols);
}

static struct sbline *
term_sb_line(struct term *term, int idx)
{
	int		slot;

	slot = (term->sb_head - 1 - idx + (term->sb_max * 2)) % term->sb_max;

	return (&term->sb[slot]);
}

static int
term_sel_in_rect(struct term *term, struct term_rect *rect, int all)
{
	int		row, first, last, inside;

	for (row = term->sel_start.row; row <= term->sel_end.row; row++) {
		if (row == term->sel_start.row)
			first = term->sel_start.col;
		else
			first = 0;

		if (row == term->sel_end.row)
			last = term->sel_end.col;
		else
			last = term->cols - 1;

		inside = row >= rect->start_row && row < rect->end_row &&
		    first >= rect->start_col && last < rect->end_col;

		if (all && inside == 0)
			return (0);

		if (all == 0 && row >= rect->start_row && row < rect->end_row &&
		    last >= rect->start_col && first < rect->end_col)
			return (1);
	}

	return (all);
}

static void
term_sel_shift(struct term *term, int rows, int cols)
{
	term->sel_end.row += rows;
	term->sel_start.row += rows;
	term->sel_anchor.row += rows;

	term->sel_end.col += cols;
	term->sel_start.col += cols;
	term->sel_anchor.col += cols;

	if (-term->sel_start.row > term->sb_count)
		term->sel_state = SEL_NONE;
}

static void
term_cb_damage(void *arg, int x1, int y1, int x2, int y2)
{
	struct term_rect	rect;
	struct term		*term;

	term = arg;

	if (term->sel_state == SEL_NONE)
		return;

	rect.start_row = y1;
	rect.end_row = y2 + 1;
	rect.start_col = x1;
	rect.end_col = x2 + 1;

	if (term_sel_in_rect(term, &rect, 0))
		term->sel_state = SEL_NONE;
}

static void
term_cb_scroll(void *arg, int top, int bot, int n)
{
	struct term		*term;
	struct term_rect	src, dst;

	term = arg;

	if (term->sel_state == SEL_NONE || n == 0)
		return;

	src.start_col = 0;
	src.end_col = term->cols;
	dst.start_col = 0;
	dst.end_col = term->cols;

	if (n < 0) {
		src.start_row = top - n;
		src.end_row = bot + 1;
		dst.start_row = top;
		dst.end_row = bot + 1 + n;
	} else {
		src.start_row = top;
		src.end_row = bot + 1 - n;
		dst.start_row = top + n;
		dst.end_row = bot + 1;
	}

	if (n < 0 && top == 0 && term->sb_max > 0 &&
	    !(coma_vt_mode(term->vt) & COMA_VT_MODE_ALTSCREEN))
		src.start_row = -term->sb_count;

	if (term_sel_in_rect(term, &src, 1)) {
		term_sel_shift(term, n, 0);
	} else if (term_sel_in_rect(term, &src, 0) ||
	    term_sel_in_rect(term, &dst, 0)) {
		term->sel_state = SEL_NONE;
	}
}

static void
term_cb_title(void *arg, const char *title)
{
	struct term	*term;

	term = arg;
	coma_client_update_title(term->client, title);
}

static void
term_cb_scrollout(void *arg, const struct vt_cell *cells, int cols)
{
	int		i;
	struct term	*term;
	struct sbline	*line;

	term = arg;

	if (term->sb_max == 0)
		return;

	line = &term->sb[term->sb_head];

	if (line->cells != NULL && line->cols != cols) {
		free(line->cells);
		line->cells = NULL;
	}

	if (line->cells == NULL)
		line->cells = coma_calloc(cols, sizeof(*line->cells));

	line->cols = cols;

	for (i = 0; i < cols; i++)
		term_cell_convert(&cells[i], &line->cells[i]);

	term->sb_head = (term->sb_head + 1) % term->sb_max;

	if (term->sb_count < term->sb_max)
		term->sb_count++;
}

static void
term_cb_sb_clear(void *arg)
{
	struct term	*term;

	term = arg;
	term->scroll = 0;
	term->sb_count = 0;

	if (term->sel_state != SEL_NONE && term->sel_start.row < 0)
		term->sel_state = SEL_NONE;

	coma_wm_dirty();
}
