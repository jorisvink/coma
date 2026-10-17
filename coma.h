/*
 * Copyright (c) 2019 Joris Vink <joris@coders.se>
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

#ifndef __H_COMA_H
#define __H_COMA_H

#include <sys/types.h>
#include <sys/queue.h>

#include <errno.h>
#include <string.h>
#include <signal.h>

#define errno_s				strerror(errno)

/*
 * Atomic operations used in coma.
 */
#define coma_atomic_read(x)		\
    __atomic_load_n(x, __ATOMIC_SEQ_CST)

#define coma_atomic_write(x, v)		\
    __atomic_store_n(x, v, __ATOMIC_SEQ_CST)

#define COMA_VERSION			"2.0-app"
#define COMA_WM_FONT			"fixed:pixelsize=13:style=bold"
#define COMA_TERM_FONT			"monospace:pixelsize=13"
#define COMA_TERM_NAME			"xterm-256color"
#define COMA_SCROLLBACK			2000

#define COMA_ACTION_SHELL_PREFIX	"cmd_shell:"
#define COMA_ACTION_SHELL_PREFIX_LEN	(sizeof(COMA_ACTION_SHELL_PREFIX) - 1)

#define COMA_LOG_FILE			".coma.log"

#define COMA_MOD_SHIFT			0x0001
#define COMA_MOD_CTRL			0x0002
#define COMA_MOD_ALT			0x0004
#define COMA_MOD_SUPER			0x0008
#define COMA_MOD_NUM			0x0010
#define COMA_MOD_MODE			0x0020

#define COMA_MOD_KEY			COMA_MOD_CTRL
#define COMA_PREFIX_KEY			't'

#define COMA_FRAME_LAYOUT_DEFAULT	1
#define COMA_FRAME_LAYOUT_SMALL_LARGE	2
#define COMA_FRAME_LAYOUT_SMALL_DUAL	3

#define COMA_FRAME_BORDER	5
#define COMA_FRAME_GAP		20
#define COMA_FRAME_BAR		35
#define COMA_FRAME_WIDTH	484

#define COMA_SHELL_ARGV		64
#define COMA_TERM_PAD		2

#define COMA_DRAW_UI		0
#define COMA_DRAW_TERM		1
#define COMA_DRAW_TERM_BOLD	2

struct frame;
struct vt;
struct term;

#define COMA_VT_ATTR_BOLD	0x0001
#define COMA_VT_ATTR_FAINT	0x0002
#define COMA_VT_ATTR_ITALIC	0x0004
#define COMA_VT_ATTR_UNDERLINE	0x0008
#define COMA_VT_ATTR_BLINK	0x0010
#define COMA_VT_ATTR_REVERSE	0x0020
#define COMA_VT_ATTR_INVISIBLE	0x0040
#define COMA_VT_ATTR_STRUCK	0x0080
#define COMA_VT_ATTR_WRAP	0x0100
#define COMA_VT_ATTR_WIDE	0x0200
#define COMA_VT_ATTR_WDUMMY	0x0400

#define COMA_VT_FG_DEFAULT	256
#define COMA_VT_BG_DEFAULT	257
#define COMA_VT_TRUECOLOR	(1 << 24)

#define COMA_VT_COMB_MAX	2

#define COMA_VT_MODE_APPKEYPAD		0x0001
#define COMA_VT_MODE_MOUSEBTN		0x0002
#define COMA_VT_MODE_MOUSEMOTION	0x0004
#define COMA_VT_MODE_REVERSE		0x0008
#define COMA_VT_MODE_HIDE		0x0010
#define COMA_VT_MODE_APPCURSOR		0x0020
#define COMA_VT_MODE_MOUSESGR		0x0040
#define COMA_VT_MODE_FOCUS		0x0080
#define COMA_VT_MODE_MOUSEX10		0x0100
#define COMA_VT_MODE_MOUSEMANY		0x0200
#define COMA_VT_MODE_BRCKTPASTE		0x0400
#define COMA_VT_MODE_ALTSCREEN		0x0800
#define COMA_VT_MODE_MOUSE		(COMA_VT_MODE_MOUSEBTN | \
    COMA_VT_MODE_MOUSEMOTION | COMA_VT_MODE_MOUSEX10 | COMA_VT_MODE_MOUSEMANY)

#define COMA_VT_CURSOR_BLOCK		1
#define COMA_VT_CURSOR_UNDERLINE	2
#define COMA_VT_CURSOR_BAR		3

#define COMA_VT_MOUSE_PRESS		1
#define COMA_VT_MOUSE_RELEASE		2
#define COMA_VT_MOUSE_MOTION		3

#define COMA_VT_KEY_ENTER		1
#define COMA_VT_KEY_TAB			2
#define COMA_VT_KEY_BACKSPACE		3
#define COMA_VT_KEY_ESCAPE		4
#define COMA_VT_KEY_UP			5
#define COMA_VT_KEY_DOWN		6
#define COMA_VT_KEY_LEFT		7
#define COMA_VT_KEY_RIGHT		8
#define COMA_VT_KEY_INS			9
#define COMA_VT_KEY_DEL			10
#define COMA_VT_KEY_HOME		11
#define COMA_VT_KEY_END			12
#define COMA_VT_KEY_PAGEUP		13
#define COMA_VT_KEY_PAGEDOWN		14
#define COMA_VT_KEY_KP_ENTER		15
#define COMA_VT_KEY_F(n)		(100 + (n))

struct vt_cell {
	u_int32_t		u;
	u_int16_t		mode;
	u_int32_t		fg;
	u_int32_t		bg;
	u_int32_t		comb[COMA_VT_COMB_MAX];
};

struct vt_callbacks {
	void	(*sbclear)(void *);
	void	(*title)(void *, const char *);
	void	(*output)(void *, const char *, size_t);
	void	(*scroll)(void *, int, int, int);
	void	(*damage)(void *, int, int, int, int);
	void	(*scrollout)(void *, const struct vt_cell *, int);
};

struct color {
	u_int8_t		r;
	u_int8_t		g;
	u_int8_t		b;
};

struct client {
	u_int32_t		id;
	int			secure;

	struct term		*term;
	struct frame		*frame;

	char			*tag;
	char			*cmd;
	char			*pwd;
	char			*host;
	char			*title;
	char			*status;

	u_int16_t		w;
	u_int16_t		h;
	u_int16_t		x;
	u_int16_t		y;
	u_int16_t		bw;

	u_int16_t		fbo;
	u_int16_t		fbw;

	TAILQ_ENTRY(client)	list;
	TAILQ_ENTRY(client)	glist;
};

TAILQ_HEAD(client_list, client);

#define COMA_FRAME_INLIST	0x0001
#define COMA_FRAME_ZOOMED	0x0002

struct frame {
	u_int32_t		id;
	int			flags;

	u_int16_t		w;
	u_int16_t		h;
	u_int16_t		x;
	u_int16_t		y;

	u_int16_t		orig_w;
	u_int16_t		orig_h;
	u_int16_t		orig_x;
	u_int16_t		orig_y;

	struct client		*focus;
	struct client_list	clients;
	struct frame		*split;

	TAILQ_ENTRY(frame)	list;
};

TAILQ_HEAD(frame_list, frame);

extern struct client_list	clients;
extern char			*homedir;
extern u_int32_t		prefix_mod;
extern u_int32_t		prefix_key;
extern char			*shell;
extern char			*font_name;
extern char			*term_name;
extern char			*term_font_name;
extern int			scrollback_lines;
extern int			window_decorations;
extern char			*term_font_bold_name;
extern int			frame_count;
extern int			frame_layout;
extern u_int16_t		frame_gap;
extern u_int16_t		frame_bar;
extern u_int16_t		frame_width;
extern u_int16_t		frame_height;
extern int			frame_offset;
extern u_int16_t		frame_border;
extern u_int16_t		screen_width;
extern u_int16_t		screen_height;
extern struct frame		*frame_active;
extern struct client		*client_active;
extern volatile sig_atomic_t	sig_recv;
extern volatile sig_atomic_t	sig_chld;

void		fatal(const char *, ...);
void		coma_log(const char *, ...);

void		coma_reap(void);
void		coma_command(char *);
void		coma_execute(char **);
void		coma_init(int, char **);
void		coma_spawn_terminal(void);
void		coma_spawn_terminal_ce(void);
void		coma_config_parse(const char *);
void		coma_spawn_terminal_secure(void);
size_t		coma_utf8_encode(u_int32_t, char *);
size_t		coma_utf8_decode(const char *, u_int32_t *);
int		coma_split_arguments(char *, char **, size_t);
int		coma_split_string(char *, const char *, char **, size_t);

void		*coma_malloc(size_t);
void		*coma_calloc(size_t, size_t);

void		coma_wm_run(void);
int		coma_wm_pump(int);
void		coma_wm_init(void);
void		coma_wm_dirty(void);
void		coma_wm_setup(void);
void		coma_wm_wakeup(void);
int		coma_wm_focused(void);
void		coma_wm_io_update(void);
struct color	*coma_wm_color(const char *);
char		*coma_wm_selection_get(void);
u_int32_t	coma_wm_key_parse(const char *);
void		coma_wm_term_gone(struct term *);
void		coma_wm_warp(u_int16_t, u_int16_t);
void		coma_wm_selection_set(const char *);
int		coma_wm_register_action(const char *, u_int32_t);
int		coma_wm_register_color(const char *, const char *);

void		coma_draw_fonts(void);
void		coma_draw_init(void *);
void		coma_draw_unclip(void);
int		coma_draw_rescale(void);
void		coma_draw_cleanup(void);
void		coma_draw_clip(int, int, int, int);
int		coma_draw_text_width(const char *, size_t);
void		coma_draw_cell_size(u_int16_t *, u_int16_t *);
void		coma_draw_fill(int, int, int, int, const struct color *);
void		coma_draw_box(int, int, int, int, int,
		    const struct color *);
void		coma_draw_glyph(int, int, int, u_int32_t,
		    const struct color *);
void		coma_draw_mark(int, int, int, int, u_int32_t,
		    const struct color *);
int		coma_draw_text(int, int, const struct color *,
		    const char *, size_t);

struct frame	*coma_frame_lookup(u_int32_t);

void		coma_frame_init(void);
void		coma_frame_prev(void);
void		coma_frame_next(void);
void		coma_frame_zoom(void);
void		coma_frame_setup(void);
void		coma_frame_split(void);
void		coma_frame_merge(void);
void		coma_frame_cleanup(void);
void		coma_frame_relayout(void);
void		coma_frame_draw_all(void);
void		coma_frame_split_next(void);
void		coma_frame_select_any(void);
void		coma_frame_client_prev(void);
void		coma_frame_client_next(void);
void		coma_frame_bars_update(void);
void		coma_frame_layout(const char *);
void		coma_frame_select_id(u_int32_t);
void		coma_frame_client_move_left(void);
void		coma_frame_client_move_right(void);
u_int32_t	coma_frame_base_id(struct frame *);
void		coma_frame_register(struct frame *);
void		coma_frame_focus(struct frame *, int);
void		coma_frame_bar_update(struct frame *);
int		coma_frame_bar_click(u_int16_t, u_int16_t);
void		coma_frame_mouseover(u_int16_t, u_int16_t);
struct client	*coma_frame_client_at(u_int16_t, u_int16_t);
struct frame	*coma_frame_create(u_int16_t, u_int16_t, u_int16_t, u_int16_t);
void		coma_frame_config_create(u_int16_t, u_int16_t,
		    u_int16_t, u_int16_t);

void		coma_client_init(void);
void		coma_client_reap(pid_t);
void		coma_client_cleanup(void);
void		coma_client_kill_active(void);
void		coma_client_spawn(char **, int);
void		coma_client_focus(struct client *);
void		coma_client_exited(struct client *);
void		coma_client_adjust(struct client *);
void		coma_client_destroy(struct client *);
void		coma_client_warp_pointer(struct client *);
void		coma_client_update_title(struct client *, const char *);

void		coma_vt_free(struct vt *);
int		coma_vt_mode(struct vt *);
struct vt_cell	*coma_vt_row(struct vt *, int);
void		coma_vt_focus(struct vt *, int);
void		coma_vt_resize(struct vt *, int, int);
void		coma_vt_paste(struct vt *, const char *);
void		coma_vt_color(u_int32_t, struct color *);
void		coma_vt_key(struct vt *, int, u_int32_t);
void		coma_vt_cursor(struct vt *, int *, int *, int *);
size_t		coma_vt_write(struct vt *, const char *, size_t);
void		coma_vt_unichar(struct vt *, u_int32_t, u_int32_t);
void		coma_vt_mouse(struct vt *, int, int, int, int, u_int32_t);
struct vt	*coma_vt_create(int, int, const struct vt_callbacks *, void *);

int		coma_term_fd(struct term *);
pid_t		coma_term_pid(struct term *);
int		coma_term_read(struct term *);
void		coma_term_free(struct term *);
void		coma_term_flush(struct term *);
int		coma_term_pending(struct term *);
void		coma_term_focus(struct term *, int);
void		coma_term_select_end(struct term *);
void		coma_term_wheel(struct term *, int);
void		coma_term_scroll(struct term *, int);
void		coma_term_scroll_page(struct term *, int);
void		coma_term_paste(struct term *, const char *);
void		coma_term_key(struct term *, int, u_int32_t);
int		coma_term_cwd(struct term *, char *, size_t);
void		coma_term_select_extend(struct term *, int, int);
int		coma_term_mouse_grabbed(struct term *, u_int32_t);
int		coma_term_line(struct term *, int, char *, size_t);
void		coma_term_select_start(struct term *, int, int, int);
void		coma_term_resize(struct term *, u_int16_t, u_int16_t);
void		coma_term_draw(struct term *, int, int, int, int, int);
void		coma_term_unichar(struct term *, u_int32_t, u_int32_t);
void		coma_term_mouse_move(struct term *, int, int, u_int32_t);
void		coma_term_mouse_button(struct term *, int, int, int, int,
		    u_int32_t);

struct term	*coma_term_spawn(char **, const char *, struct client *, int);

void		coma_platform_sandbox(void);

#endif
