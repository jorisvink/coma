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
#include <sys/queue.h>

#include <SDL.h>

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "coma.h"

#define WM_MODE_NORMAL		0
#define WM_MODE_PREFIX		1
#define WM_MODE_INPUT		2
#define WM_MODE_CLIENTS		3
#define WM_MODE_LAYOUT		4

#define WM_IO_MAX		512
#define WM_INPUT_MAX		2048
#define WM_CLIENT_LIST_MAX	16

#define WM_GL_RENDERER		0x1f01

#define WM_RENDER_IDLE		4
#define WM_RENDER_MAX		33
#define WM_META_WAIT		100

static void	wm_command(void);
static void	wm_teardown(void);
static void	wm_client_list(void);
static void	wm_layout_swap(void);
static void	wm_unbind(u_int32_t);
static void	wm_renderer_log(void);
static void	wm_command_input(char *);
static void	wm_run_shell_command(const char *);
static void	wm_input(size_t, void (*)(char *));

static void	wm_render(void);
static void	wm_render_input(void);
static void	wm_render_client_list(void);
static int	wm_client_list_build(struct client **);

static void		wm_pointer(int);
static u_int32_t	wm_mods(SDL_Keymod);
static void		wm_meta_flush(void);
static void		wm_event(SDL_Event *);
static void		wm_text(const char *);
static void		wm_pointer_shape(int);
static void		wm_paste(struct term *);
static void		wm_key_input(SDL_Keycode);
static void		wm_key_layout(SDL_Keycode);
static void		wm_key(SDL_KeyboardEvent *);
static void		wm_prefix_action(u_int32_t);
static void		wm_key_client_list(SDL_Keycode);
static int		wm_keypad(SDL_Keycode, u_int32_t);
static int		wm_meta_same(u_int32_t, u_int32_t);
static void		wm_key_client(SDL_Keycode, u_int32_t);
static int		wm_key_is_text(SDL_Keycode, u_int32_t);

static void	wm_window_event(SDL_WindowEvent *);
static void	wm_mouse_wheel(SDL_MouseWheelEvent *);
static void	wm_mouse_motion(SDL_MouseMotionEvent *);
static void	wm_mouse_button(SDL_MouseButtonEvent *);

static void	wm_io(void);
static void	wm_io_ack(void);
static int	wm_io_thread(void *);

static int	wm_primary(void);
static int	wm_quit_confirm(void);
static int	wm_color_parse(const char *, struct color *);

u_int16_t		screen_width = 0;
u_int16_t		screen_height = 0;
int			window_decorations = 0;

char			*font_name = NULL;
u_int32_t		prefix_mod = COMA_MOD_KEY;
u_int32_t		prefix_key = COMA_PREFIX_KEY;

static SDL_Window	*window = NULL;
static SDL_mutex	*io_lock = NULL;
static SDL_Renderer	*renderer = NULL;
static SDL_Thread	*io_thread = NULL;
static SDL_Cursor	*cursor_ibeam = NULL;
static SDL_Cursor	*cursor_arrow = NULL;

static int		dirty = 1;
static int		running = 0;
static u_int32_t	dirty_since = 0;
static u_int32_t	output_last = 0;
static int		text_wait = 0;
static int		text_swallow = 0;
static int		mode = WM_MODE_NORMAL;

static u_int32_t	meta_cp = 0;
static u_int32_t	meta_since = 0;
static int		meta_pending = 0;

static int		pointer_ibeam = 0;
static int		window_focused = 1;
static int		pointer_visible = 1;

static size_t		input_max = 0;
static char		input_buf[WM_INPUT_MAX];
static void		(*input_done)(char *) = NULL;

static int		io_quit = 0;
static int		io_nfds = 0;
static u_int32_t	io_event = 0;
static int		io_fds[WM_IO_MAX];
static short		io_events[WM_IO_MAX];
static int		io_pipe[2] = { -1, -1 };

static struct term	*mouse_term = NULL;
static struct term	*select_term = NULL;

static struct {
	const char	*name;
	const char	*rgb;
	struct color	color;
} colors[] = {
	{ "client-active",		"#000000",	{ 0, 0, 0 }},
	{ "client-inactive",		"#000000",	{ 0, 0, 0 }},
	{ "frame-bar",			"#353535",	{ 0, 0, 0 }},
	{ "frame-bar-inactive",		"#202020",	{ 0, 0, 0 }},
	{ "frame-bar-directory",	"#ffffd7",	{ 0, 0, 0 }},
	{ "frame-bar-client-active",	"#808080",	{ 0, 0, 0 }},
	{ "frame-bar-client-secure",	"#708090",	{ 0, 0, 0 }},
	{ "frame-bar-client-normal",	"#909090",	{ 0, 0, 0 }},
	{ "frame-bar-client-inactive",	"#000000",	{ 0, 0, 0 }},
	{ "command-input",		"#ffffff",	{ 0, 0, 0 }},
	{ "command-bar",		"#080808",	{ 0, 0, 0 }},
	{ "command-border",		"#000000",	{ 0, 0, 0 }},
	{ "background",			"#101010",	{ 0, 0, 0 }},
	{ "terminal-foreground",	"#ffffd7",	{ 0, 0, 0 }},
	{ "terminal-background",	"#151515",	{ 0, 0, 0 }},
	{ NULL,				NULL,		{ 0, 0, 0 }},
};

struct uaction {
	u_int32_t		sym;
	char			*action;
	LIST_ENTRY(uaction)	list;
};

static LIST_HEAD(, uaction)	uactions;

struct {
	const char	*name;
	u_int32_t	sym;
	void		(*cb)(void);
} actions[] = {
	{ "frame-prev",			'h',	coma_frame_prev },
	{ "frame-next",			'l',	coma_frame_next },

	{ "frame-layout-swap",		'w',	wm_layout_swap },
	{ "frame-zoom",			'z',	coma_frame_zoom },
	{ "frame-split",		's',	coma_frame_split },
	{ "frame-merge",		'm',	coma_frame_merge },
	{ "frame-split-next",		'f',	coma_frame_split_next },

	{ "frame-move-client-left",	'i',	coma_frame_client_move_left },
	{ "frame-move-client-right", 	'o',	coma_frame_client_move_right },

	{ "coma-terminal",		'c',	coma_spawn_terminal },
	{ "coma-terminal-ce",		'e',	coma_spawn_terminal_ce },
	{ "coma-terminal-secure",	'v',	coma_spawn_terminal_secure },

	{ "client-kill",		'k',	coma_client_kill_active },
	{ "client-prev",		'p',	coma_frame_client_prev },
	{ "client-next",		'n',	coma_frame_client_next },

	{ "coma-command",		':',	wm_command },
	{ "coma-client-list",		'q',	wm_client_list },

	{ NULL, 0, NULL }
};

static const struct {
	const char	*name;
	u_int32_t	key;
} keynames[] = {
	{ "space",		' ' },
	{ "exclam",		'!' },
	{ "quotedbl",		'"' },
	{ "numbersign",		'#' },
	{ "dollar",		'$' },
	{ "percent",		'%' },
	{ "ampersand",		'&' },
	{ "apostrophe",		'\'' },
	{ "parenleft",		'(' },
	{ "parenright",		')' },
	{ "asterisk",		'*' },
	{ "plus",		'+' },
	{ "comma",		',' },
	{ "minus",		'-' },
	{ "period",		'.' },
	{ "slash",		'/' },
	{ "colon",		':' },
	{ "semicolon",		';' },
	{ "less",		'<' },
	{ "equal",		'=' },
	{ "greater",		'>' },
	{ "question",		'?' },
	{ "at",			'@' },
	{ "bracketleft",	'[' },
	{ "backslash",		'\\' },
	{ "bracketright",	']' },
	{ "asciicircum",	'^' },
	{ "underscore",		'_' },
	{ "grave",		'`' },
	{ "braceleft",		'{' },
	{ "bar",		'|' },
	{ "braceright",		'}' },
	{ "asciitilde",		'~' },
	{ "BackSpace",		SDLK_BACKSPACE },
	{ "Prior",		SDLK_PAGEUP },
	{ "Next",		SDLK_PAGEDOWN },
	{ NULL,			0 }
};

void
coma_wm_init(void)
{
	int		i;

	if ((font_name = strdup(COMA_WM_FONT)) == NULL)
		fatal("strdup");

	LIST_INIT(&uactions);

	for (i = 0; colors[i].name != NULL; i++) {
		if (wm_color_parse(colors[i].rgb, &colors[i].color) == -1)
			fatal("bad default color %s", colors[i].rgb);
	}

	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

	if (getenv("SDL_VIDEODRIVER") == NULL &&
	    getenv("WAYLAND_DISPLAY") != NULL)
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		fatal("SDL_Init: %s", SDL_GetError());
}

void
coma_wm_setup(void)
{
	SDL_Rect	bounds;
	u_int32_t	wflags;
	int		i, w, h, flags;

	w = screen_width;
	h = screen_height;

	if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
		if (w == 0)
			w = bounds.w;
		if (h == 0)
			h = bounds.h;
	} else {
		if (w == 0)
			w = 1280;
		if (h == 0)
			h = 800;
	}

	wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (window_decorations == 0)
		wflags |= SDL_WINDOW_BORDERLESS;

	if ((window = SDL_CreateWindow("coma", SDL_WINDOWPOS_UNDEFINED,
	    SDL_WINDOWPOS_UNDEFINED, w, h, wflags)) == NULL)
		fatal("SDL_CreateWindow: %s", SDL_GetError());

	if ((renderer = SDL_CreateRenderer(window, -1,
	    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)) == NULL) {
		coma_log("no accelerated renderer: %s", SDL_GetError());
		if ((renderer = SDL_CreateRenderer(window, -1,
		    SDL_RENDERER_SOFTWARE)) == NULL)
			fatal("SDL_CreateRenderer: %s", SDL_GetError());
	}

	wm_renderer_log();

	SDL_GetWindowSize(window, &w, &h);
	screen_width = w;
	screen_height = h;

	SDL_SetWindowMinimumSize(window,
	    frame_width + (frame_gap * 2) + (frame_border * 4) + 2,
	    (frame_bar * 2) + (frame_gap * 4) + (frame_border * 4) + 100);

	SDL_StartTextInput();

	cursor_ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);

	coma_draw_init(renderer);
	coma_draw_fonts();

	if (pipe(io_pipe) == -1)
		fatal("pipe: %s", errno_s);

	for (i = 0; i < 2; i++) {
		if ((flags = fcntl(io_pipe[i], F_GETFL)) == -1 ||
		    fcntl(io_pipe[i], F_SETFL, flags | O_NONBLOCK) == -1 ||
		    fcntl(io_pipe[i], F_SETFD, FD_CLOEXEC) == -1)
			fatal("fcntl: %s", errno_s);
	}

	if ((io_event = SDL_RegisterEvents(1)) == 0)
		fatal("SDL_RegisterEvents: %s", SDL_GetError());

	if ((io_lock = SDL_CreateMutex()) == NULL)
		fatal("SDL_CreateMutex: %s", SDL_GetError());

	coma_frame_setup();

	if ((io_thread = SDL_CreateThread(wm_io_thread, "coma-io",
	    NULL)) == NULL)
		fatal("SDL_CreateThread: %s", SDL_GetError());

	if (TAILQ_EMPTY(&clients))
		coma_spawn_terminal();

	running = 1;

	coma_wm_io_update();
	coma_wm_dirty();
}

void
coma_wm_run(void)
{
	for (;;) {
		if (coma_wm_pump(-1) == 0)
			break;
	}

	wm_teardown();
}

int
coma_wm_pump(int timeout)
{
	SDL_Event		evt;
	u_int32_t		now, idle, busy, delay;

	if (coma_atomic_read(&sig_recv) != -1) {
		running = 0;
		return (0);
	}

	if (coma_atomic_read(&sig_chld)) {
		coma_atomic_write(&sig_chld, 0);
		coma_reap();
	}

	if (meta_pending) {
		now = SDL_GetTicks();
		if (now - meta_since >= WM_META_WAIT) {
			wm_meta_flush();
		} else {
			delay = WM_META_WAIT - (now - meta_since);
			if (timeout == -1 || (u_int32_t)timeout > delay)
				timeout = delay;
		}
	}

	if (dirty) {
		now = SDL_GetTicks();
		idle = now - output_last;
		busy = now - dirty_since;

		if (idle >= WM_RENDER_IDLE || busy >= WM_RENDER_MAX) {
			wm_render();
		} else {
			delay = WM_RENDER_IDLE - idle;
			if (delay > WM_RENDER_MAX - busy)
				delay = WM_RENDER_MAX - busy;

			if (timeout == -1 || (u_int32_t)timeout > delay)
				timeout = delay;
		}
	}

	if (timeout == -1) {
		if (!SDL_WaitEvent(&evt))
			fatal("SDL_WaitEvent: %s", SDL_GetError());
	} else {
		if (!SDL_WaitEventTimeout(&evt, timeout))
			return (running);
	}

	wm_event(&evt);

	while (running && SDL_PollEvent(&evt))
		wm_event(&evt);

	return (running);
}

int
coma_wm_focused(void)
{
	return (window_focused);
}

void
coma_wm_dirty(void)
{
	if (dirty == 0) {
		dirty = 1;
		dirty_since = SDL_GetTicks();
	}
}

void
coma_wm_wakeup(void)
{
	ssize_t		ret;

	if (io_pipe[1] != -1) {
		ret = write(io_pipe[1], "s", 1);
		(void)ret;
	}
}

void
coma_wm_io_update(void)
{
	int		fd;
	struct client	*client;

	if (io_lock == NULL)
		return;

	SDL_LockMutex(io_lock);

	io_nfds = 0;
	TAILQ_FOREACH(client, &clients, glist) {
		if ((fd = coma_term_fd(client->term)) == -1)
			continue;
		if (io_nfds == WM_IO_MAX)
			break;
		io_fds[io_nfds] = fd;
		io_events[io_nfds] = POLLIN;
		if (coma_term_pending(client->term))
			io_events[io_nfds] |= POLLOUT;
		io_nfds++;
	}

	SDL_UnlockMutex(io_lock);

	wm_io_ack();
}

void
coma_wm_term_gone(struct term *term)
{
	if (select_term == term)
		select_term = NULL;

	if (mouse_term == term)
		mouse_term = NULL;
}

void
coma_wm_warp(u_int16_t x, u_int16_t y)
{
	if (window == NULL)
		return;

	if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
		return;

	SDL_WarpMouseInWindow(window, x, y);
}

struct color *
coma_wm_color(const char *name)
{
	int		i;

	for (i = 0; colors[i].name != NULL; i++) {
		if (!strcmp(name, colors[i].name))
			return (&colors[i].color);
	}

	return (&colors[0].color);
}

u_int32_t
coma_wm_key_parse(const char *name)
{
	int		i;
	u_int32_t	cp;
	size_t		len;
	SDL_Keycode	key;

	if ((len = coma_utf8_decode(name, &cp)) != 0 && name[len] == '\0')
		return (cp);

	for (i = 0; keynames[i].name != NULL; i++) {
		if (!strcmp(keynames[i].name, name))
			return (keynames[i].key);
	}

	if ((key = SDL_GetKeyFromName(name)) == SDLK_UNKNOWN)
		return (0);

	return (key);
}

int
coma_wm_register_action(const char *action, u_int32_t sym)
{
	int		i;
	struct uaction	*ua;

	if (!strncmp(COMA_ACTION_SHELL_PREFIX,
	    action, COMA_ACTION_SHELL_PREFIX_LEN)) {
		ua = coma_calloc(1, sizeof(*ua));
		ua->sym = sym;
		ua->action = strdup(action + COMA_ACTION_SHELL_PREFIX_LEN);
		if (ua->action == NULL)
			fatal("strdup");
		wm_unbind(sym);
		LIST_INSERT_HEAD(&uactions, ua, list);
		return (0);
	}

	for (i = 0; actions[i].name != NULL; i++) {
		if (!strcmp(actions[i].name, action)) {
			wm_unbind(sym);
			actions[i].sym = sym;
			return (0);
		}
	}

	return (-1);
}

int
coma_wm_register_color(const char *name, const char *rgb)
{
	int		i;

	for (i = 0; colors[i].name != NULL; i++) {
		if (!strcmp(name, colors[i].name))
			break;
	}

	if (colors[i].name == NULL)
		return (-1);

	return (wm_color_parse(rgb, &colors[i].color));
}

void
coma_wm_selection_set(const char *text)
{
	if (wm_primary() && SDL_SetPrimarySelectionText(text) == 0)
		return;

	(void)SDL_SetClipboardText(text);
}

char *
coma_wm_selection_get(void)
{
	char		*text, *copy;

	text = NULL;

	if (wm_primary())
		text = SDL_GetPrimarySelectionText();

	if (text == NULL || *text == '\0') {
		SDL_free(text);
		text = SDL_GetClipboardText();
	}

	if (text == NULL)
		return (NULL);

	if ((copy = strdup(text)) == NULL)
		fatal("strdup");

	SDL_free(text);

	return (copy);
}

static void
wm_renderer_log(void)
{
	const char		*gl;
	SDL_RendererInfo	info;
	void			*sym;
	const u_int8_t		*(*get)(u_int32_t);

	if (SDL_GetRendererInfo(renderer, &info) != 0) {
		coma_log("SDL_GetRendererInfo: %s", SDL_GetError());
		return;
	}

	gl = NULL;

	if (!strncmp(info.name, "opengl", 6) &&
	    (sym = SDL_GL_GetProcAddress("glGetString")) != NULL) {
		memcpy(&get, &sym, sizeof(get));
		gl = (const char *)get(WM_GL_RENDERER);
	}

	coma_log("renderer %s (%s%s) on %s",
	    info.name, (info.flags & SDL_RENDERER_ACCELERATED) ?
	    "accelerated" : "software",
	    (info.flags & SDL_RENDERER_PRESENTVSYNC) ? ", vsync" : "",
	    gl != NULL ? gl : "unknown");
}

static void
wm_teardown(void)
{
	struct uaction	*ua;

	coma_atomic_write(&io_quit, 1);
	wm_io_ack();
	SDL_WaitThread(io_thread, NULL);

	coma_client_cleanup();

	while ((ua = LIST_FIRST(&uactions)) != NULL) {
		LIST_REMOVE(ua, list);
		free(ua->action);
		free(ua);
	}

	coma_frame_cleanup();
	coma_draw_cleanup();

	SDL_DestroyMutex(io_lock);
	io_lock = NULL;

	(void)close(io_pipe[0]);
	(void)close(io_pipe[1]);
	io_pipe[0] = -1;
	io_pipe[1] = -1;

	if (cursor_ibeam != NULL)
		SDL_FreeCursor(cursor_ibeam);
	if (cursor_arrow != NULL)
		SDL_FreeCursor(cursor_arrow);

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

static void
wm_render(void)
{
	struct color	*bg;

	dirty = 0;
	bg = coma_wm_color("background");

	SDL_SetRenderDrawColor(renderer, bg->r, bg->g, bg->b, 0xff);
	SDL_RenderClear(renderer);

	coma_frame_draw_all();

	switch (mode) {
	case WM_MODE_INPUT:
		wm_render_input();
		break;
	case WM_MODE_CLIENTS:
		wm_render_client_list();
		break;
	}

	SDL_RenderPresent(renderer);
}

static void
wm_render_input(void)
{
	int		x, y;

	x = (screen_width / 2) - 200;
	y = (screen_height / 2) - 50;

	coma_draw_box(x, y, 404, COMA_FRAME_BAR + 4, 2,
	    coma_wm_color("command-border"));
	coma_draw_fill(x + 2, y + 2, 400, COMA_FRAME_BAR,
	    coma_wm_color("command-bar"));

	coma_draw_clip(x + 2, y + 2, 400, COMA_FRAME_BAR);
	coma_draw_text(x + 2 + 5, y + 2 + 15, coma_wm_color("command-input"),
	    input_buf, strlen(input_buf));
	coma_draw_unclip();
}

static void
wm_render_client_list(void)
{
	const char	*host;
	struct color	*color;
	char		c, buf[128];
	int		x, y, idx, len, limit;
	struct client	*list[WM_CLIENT_LIST_MAX];

	x = (screen_width - 404) / 2;
	y = (screen_height - 404) / 2;

	coma_draw_box(x, y, 404, 404, 2, coma_wm_color("command-border"));
	coma_draw_fill(x + 2, y + 2, 400, 400, coma_wm_color("command-bar"));

	limit = wm_client_list_build(list);
	color = coma_wm_color("command-input");

	coma_draw_clip(x + 2, y + 2, 400, 400);

	for (idx = 0; idx < limit; idx++) {
		if (idx < 10)
			c = '0' + idx;
		else
			c = 'a' + (idx - 10);

		if (list[idx]->host != NULL)
			host = list[idx]->host;
		else
			host = "?";

		if (list[idx]->tag) {
			len = snprintf(buf, sizeof(buf),
			    "#%c [%s] [%s]", c, list[idx]->tag, host);
		} else if (list[idx]->cmd) {
			len = snprintf(buf, sizeof(buf),
			    "#%c [%s] [%s]", c, list[idx]->cmd, host);
		} else {
			len = snprintf(buf, sizeof(buf), "#%c [%s]", c, host);
		}

		if (len == -1) {
			len = snprintf(buf, sizeof(buf), "#%c [unknown]", c);
		} else if ((size_t)len >= sizeof(buf)) {
			len = sizeof(buf) - 1;
			while (len > 0 && (buf[len - 1] & 0xc0) == 0x80)
				len--;
			if (len > 0 && (buf[len - 1] & 0x80))
				len--;
		}

		if (len == -1 || (size_t)len >= sizeof(buf))
			fatal("failed to construct client list buffer");

		coma_draw_text(x + 2 + 5, y + 2 + 20 + (idx * 15),
		    color, buf, len);
	}

	coma_draw_unclip();
}

static int
wm_client_list_build(struct client **list)
{
	int		idx;
	struct client	*client;

	idx = 0;

	TAILQ_FOREACH(client, &clients, glist) {
		if (idx >= WM_CLIENT_LIST_MAX)
			break;
		list[idx++] = client;
	}

	return (idx);
}

static void
wm_event(SDL_Event *evt)
{
	switch (evt->type) {
	case SDL_QUIT:
		if (wm_quit_confirm())
			running = 0;
		break;
	case SDL_WINDOWEVENT:
		wm_window_event(&evt->window);
		break;
	case SDL_KEYDOWN:
		wm_key(&evt->key);
		break;
	case SDL_TEXTINPUT:
		wm_text(evt->text.text);
		break;
	case SDL_MOUSEMOTION:
		wm_mouse_motion(&evt->motion);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		wm_mouse_button(&evt->button);
		break;
	case SDL_MOUSEWHEEL:
		wm_mouse_wheel(&evt->wheel);
		break;
	default:
		if (evt->type == io_event)
			wm_io();
		break;
	}
}

static void
wm_window_event(SDL_WindowEvent *evt)
{
	switch (evt->event) {
	case SDL_WINDOWEVENT_DISPLAY_CHANGED:
		if (coma_draw_rescale())
			coma_frame_relayout();
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		if (coma_draw_rescale() == 0 &&
		    evt->data1 == screen_width && evt->data2 == screen_height)
			break;
		screen_width = evt->data1;
		screen_height = evt->data2;
		coma_log("window resized to %ux%u",
		    screen_width, screen_height);
		coma_frame_relayout();
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		window_focused = 1;
		if (client_active != NULL)
			coma_term_focus(client_active->term, 1);
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		window_focused = 0;
		if (client_active != NULL)
			coma_term_focus(client_active->term, 0);
		break;
	default:
		break;
	}

	coma_wm_dirty();
}

static void
wm_key(SDL_KeyboardEvent *evt)
{
	SDL_Keycode	sym;
	u_int32_t	mods;

	sym = evt->keysym.sym;
	mods = wm_mods(evt->keysym.mod);

	text_swallow = 0;

	switch (sym) {
	case SDLK_LCTRL:
	case SDLK_RCTRL:
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:
	case SDLK_LALT:
	case SDLK_RALT:
	case SDLK_LGUI:
	case SDLK_RGUI:
	case SDLK_MODE:
	case SDLK_CAPSLOCK:
	case SDLK_NUMLOCKCLEAR:
		return;
	}

	if (mode != WM_MODE_NORMAL || wm_key_is_text(sym, mods) == 0)
		wm_meta_flush();

	switch (mode) {
	case WM_MODE_PREFIX:
		if ((u_int32_t)sym == prefix_key &&
		    (mods & prefix_mod) == prefix_mod) {
			mode = WM_MODE_NORMAL;
			wm_key_client(sym, mods);
			return;
		}
		if (wm_key_is_text(sym, mods)) {
			text_wait = 1;
			return;
		}
		text_swallow = 1;
		mode = WM_MODE_NORMAL;
		wm_prefix_action(sym);
		return;
	case WM_MODE_INPUT:
		wm_key_input(sym);
		return;
	case WM_MODE_CLIENTS:
		text_swallow = 1;
		wm_key_client_list(sym);
		return;
	case WM_MODE_LAYOUT:
		text_swallow = 1;
		wm_key_layout(sym);
		return;
	}

	if (prefix_key != 0 && (u_int32_t)sym == prefix_key &&
	    (mods & prefix_mod) == prefix_mod) {
		text_wait = 0;
		text_swallow = 1;
		mode = WM_MODE_PREFIX;
		return;
	}

	wm_key_client(sym, mods);
}

static void
wm_text(const char *text)
{
	u_int32_t	cp;
	size_t		len;

	if (text_swallow) {
		text_swallow = 0;
		return;
	}

	switch (mode) {
	case WM_MODE_PREFIX:
		if (text_wait == 0 || coma_utf8_decode(text, &cp) == 0)
			return;
		text_wait = 0;
		mode = WM_MODE_NORMAL;
		wm_prefix_action(cp);
		return;
	case WM_MODE_INPUT:
		len = strlen(input_buf);
		if (len + strlen(text) < input_max) {
			(void)strlcat(input_buf, text, sizeof(input_buf));
			coma_wm_dirty();
		}
		return;
	case WM_MODE_CLIENTS:
	case WM_MODE_LAYOUT:
		return;
	}

	if (client_active == NULL)
		return;

	wm_pointer(0);

	if (meta_pending) {
		meta_pending = 0;
		len = coma_utf8_decode(text, &cp);
		if (len != 0 && text[len] == '\0' &&
		    wm_meta_same(cp, meta_cp)) {
			coma_term_unichar(client_active->term,
			    cp, COMA_MOD_ALT);
			return;
		}
	}

	while ((len = coma_utf8_decode(text, &cp)) != 0) {
		coma_term_unichar(client_active->term, cp, 0);
		text += len;
	}
}

static void
wm_key_client(SDL_Keycode sym, u_int32_t mods)
{
	u_int32_t	cp;
	int		key;
	struct term	*term;

	if (client_active == NULL) {
		text_swallow = 1;
		return;
	}

	term = client_active->term;
	wm_pointer(0);

	if (mods & COMA_MOD_SHIFT) {
		switch (sym) {
		case SDLK_PAGEUP:
			coma_term_scroll_page(term, 1);
			text_swallow = 1;
			return;
		case SDLK_PAGEDOWN:
			coma_term_scroll_page(term, 0);
			text_swallow = 1;
			return;
		case SDLK_INSERT:
			wm_paste(term);
			text_swallow = 1;
			return;
		}
	}

	switch (sym) {
	case SDLK_RETURN:
		key = COMA_VT_KEY_ENTER;
		break;
	case SDLK_TAB:
		key = COMA_VT_KEY_TAB;
		break;
	case SDLK_BACKSPACE:
		key = COMA_VT_KEY_BACKSPACE;
		break;
	case SDLK_ESCAPE:
		key = COMA_VT_KEY_ESCAPE;
		break;
	case SDLK_UP:
		key = COMA_VT_KEY_UP;
		break;
	case SDLK_DOWN:
		key = COMA_VT_KEY_DOWN;
		break;
	case SDLK_LEFT:
		key = COMA_VT_KEY_LEFT;
		break;
	case SDLK_RIGHT:
		key = COMA_VT_KEY_RIGHT;
		break;
	case SDLK_INSERT:
		key = COMA_VT_KEY_INS;
		break;
	case SDLK_DELETE:
		key = COMA_VT_KEY_DEL;
		break;
	case SDLK_HOME:
		key = COMA_VT_KEY_HOME;
		break;
	case SDLK_END:
		key = COMA_VT_KEY_END;
		break;
	case SDLK_PAGEUP:
		key = COMA_VT_KEY_PAGEUP;
		break;
	case SDLK_PAGEDOWN:
		key = COMA_VT_KEY_PAGEDOWN;
		break;
	case SDLK_KP_ENTER:
		key = COMA_VT_KEY_KP_ENTER;
		break;
	default:
		if (sym >= SDLK_F1 && sym <= SDLK_F12)
			key = COMA_VT_KEY_F(sym - SDLK_F1 + 1);
		else
			key = wm_keypad(sym, mods);
		break;
	}

	if (key != 0) {
		coma_term_key(term, key, mods);
		text_swallow = 1;
		return;
	}

	if (!(mods & (COMA_MOD_CTRL | COMA_MOD_ALT)))
		return;

	if (sym < 0x20 || sym >= 0x7f)
		return;

	cp = sym;

	if ((mods & COMA_MOD_SHIFT) && cp >= 'a' && cp <= 'z')
		cp -= 'a' - 'A';

	if (mods & COMA_MOD_CTRL) {
		if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
			cp &= 0x1f;
		} else {
			switch (cp) {
			case ' ':
			case '2':
			case '@':
			case '`':
				cp = 0x00;
				break;
			case '[':
			case '3':
				cp = 0x1b;
				break;
			case '\\':
			case '4':
				cp = 0x1c;
				break;
			case ']':
			case '5':
				cp = 0x1d;
				break;
			case '^':
			case '6':
				cp = 0x1e;
				break;
			case '_':
			case '/':
			case '7':
				cp = 0x1f;
				break;
			case '8':
			case '?':
				cp = 0x7f;
				break;
			default:
				break;
			}
		}
		mods &= ~(COMA_MOD_CTRL | COMA_MOD_SHIFT);
		coma_term_unichar(term, cp, mods);
		text_swallow = 1;
		return;
	}

	wm_meta_flush();

	meta_cp = cp;
	meta_pending = 1;
	meta_since = SDL_GetTicks();
}

static int
wm_keypad(SDL_Keycode sym, u_int32_t mods)
{
	if ((mods & COMA_MOD_NUM) || wm_primary() == 0)
		return (0);

	switch (sym) {
	case SDLK_KP_0:
		return (COMA_VT_KEY_INS);
	case SDLK_KP_1:
		return (COMA_VT_KEY_END);
	case SDLK_KP_2:
		return (COMA_VT_KEY_DOWN);
	case SDLK_KP_3:
		return (COMA_VT_KEY_PAGEDOWN);
	case SDLK_KP_4:
		return (COMA_VT_KEY_LEFT);
	case SDLK_KP_6:
		return (COMA_VT_KEY_RIGHT);
	case SDLK_KP_7:
		return (COMA_VT_KEY_HOME);
	case SDLK_KP_8:
		return (COMA_VT_KEY_UP);
	case SDLK_KP_9:
		return (COMA_VT_KEY_PAGEUP);
	case SDLK_KP_PERIOD:
		return (COMA_VT_KEY_DEL);
	}

	return (0);
}

static void
wm_key_input(SDL_Keycode sym)
{
	size_t		len;
	char		cmd[WM_INPUT_MAX];

	switch (sym) {
	case SDLK_ESCAPE:
		mode = WM_MODE_NORMAL;
		break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		mode = WM_MODE_NORMAL;
		if (input_buf[0] != '\0') {
			(void)strlcpy(cmd, input_buf, sizeof(cmd));
			input_done(cmd);
		}
		break;
	case SDLK_BACKSPACE:
		len = strlen(input_buf);
		while (len > 0) {
			len--;
			if ((input_buf[len] & 0xc0) != 0x80)
				break;
		}
		input_buf[len] = '\0';
		break;
	default:
		return;
	}

	coma_wm_dirty();
}

static void
wm_key_client_list(SDL_Keycode sym)
{
	struct frame	*frame;
	int		idx, limit;
	struct client	*list[WM_CLIENT_LIST_MAX];

	if (sym == SDLK_ESCAPE) {
		mode = WM_MODE_NORMAL;
		coma_wm_dirty();
		return;
	}

	if (sym >= '0' && sym <= '9')
		idx = sym - '0';
	else if (sym >= 'a' && sym <= 'f')
		idx = (sym - 'a') + 10;
	else
		return;

	limit = wm_client_list_build(list);
	if (idx >= limit)
		return;

	mode = WM_MODE_NORMAL;
	frame = list[idx]->frame;

	frame->focus = list[idx];
	coma_frame_focus(frame, 1);
}

static void
wm_key_layout(SDL_Keycode sym)
{
	const char	*layout;

	mode = WM_MODE_NORMAL;

	switch (sym) {
	case '1':
		layout = "default";
		break;
	case '2':
		layout = "small-large";
		break;
	case '3':
		layout = "small-dual";
		break;
	default:
		return;
	}

	coma_log("swapping to layout '%s'", layout);
	coma_frame_layout(layout);
	coma_frame_relayout();
}

static int
wm_key_is_text(SDL_Keycode sym, u_int32_t mods)
{
	if (mods & (COMA_MOD_CTRL | COMA_MOD_SUPER))
		return (0);

	if (sym & SDLK_SCANCODE_MASK)
		return (0);

	return (sym >= 0x20 && sym != SDLK_DELETE);
}

static void
wm_prefix_action(u_int32_t sym)
{
	int			i;
	struct uaction		*ua;
	struct frame		*frame;

	if (sym == 0)
		return;

	if (sym >= '0' && sym <= '9') {
		if ((frame = coma_frame_lookup(sym - '0')) != NULL) {
			coma_frame_focus(frame, 1);
			return;
		}
	}

	for (i = 0; actions[i].name != NULL; i++) {
		if (actions[i].sym == sym) {
			actions[i].cb();
			return;
		}
	}

	LIST_FOREACH(ua, &uactions, list) {
		if (ua->sym == sym) {
			wm_run_shell_command(ua->action);
			break;
		}
	}
}

static void
wm_meta_flush(void)
{
	if (meta_pending == 0)
		return;

	meta_pending = 0;

	if (client_active != NULL)
		coma_term_unichar(client_active->term, meta_cp, COMA_MOD_ALT);
}

static int
wm_meta_same(u_int32_t a, u_int32_t b)
{
	if (a >= 'A' && a <= 'Z')
		a += 'a' - 'A';

	if (b >= 'A' && b <= 'Z')
		b += 'a' - 'A';

	return (a == b);
}

static u_int32_t
wm_mods(SDL_Keymod mod)
{
	u_int32_t	mods;

	mods = 0;

	if (mod & KMOD_SHIFT)
		mods |= COMA_MOD_SHIFT;
	if (mod & KMOD_CTRL)
		mods |= COMA_MOD_CTRL;
	if (mod & KMOD_ALT)
		mods |= COMA_MOD_ALT;
	if (mod & KMOD_GUI)
		mods |= COMA_MOD_SUPER;
	if (mod & KMOD_NUM)
		mods |= COMA_MOD_NUM;
	if (mod & KMOD_MODE)
		mods |= COMA_MOD_MODE;

	return (mods);
}

static void
wm_pointer(int show)
{
	if (pointer_visible == show)
		return;

	pointer_visible = show;

	if (show)
		(void)SDL_ShowCursor(SDL_ENABLE);
	else
		(void)SDL_ShowCursor(SDL_DISABLE);
}

static void
wm_pointer_shape(int ibeam)
{
	SDL_Cursor	*cursor;

	if (pointer_ibeam == ibeam)
		return;

	if (ibeam)
		cursor = cursor_ibeam;
	else
		cursor = cursor_arrow;

	if (cursor == NULL)
		return;

	pointer_ibeam = ibeam;
	SDL_SetCursor(cursor);
}

static void
wm_paste(struct term *term)
{
	char		*text;

	if ((text = coma_wm_selection_get()) == NULL)
		return;

	coma_term_paste(term, text);
	free(text);
}

static void
wm_mouse_button(SDL_MouseButtonEvent *evt)
{
	u_int32_t	mods;
	struct term	*term;
	int		button;
	struct client	*client;

	wm_pointer(1);
	mods = wm_mods(SDL_GetModState());

	switch (evt->button) {
	case SDL_BUTTON_LEFT:
		button = 1;
		break;
	case SDL_BUTTON_MIDDLE:
		button = 2;
		break;
	case SDL_BUTTON_RIGHT:
		button = 3;
		break;
	default:
		return;
	}

	/* XXX - macos is dumb */
	if (button == 1 && (mods & COMA_MOD_ALT))
		button = 2;

	if (evt->type == SDL_MOUSEBUTTONUP) {
		if (mouse_term != NULL) {
			coma_term_mouse_button(mouse_term,
			    evt->x, evt->y, button, 0, mods);
			if (!(SDL_GetMouseState(NULL, NULL) &
			    (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK |
			    SDL_BUTTON_RMASK)))
				mouse_term = NULL;
			return;
		}

		if (select_term != NULL && button == 1) {
			coma_term_select_end(select_term);
			select_term = NULL;
			return;
		}

		if (mode == WM_MODE_NORMAL)
			(void)coma_frame_bar_click(evt->x, evt->y);
		return;
	}

	if (mode != WM_MODE_NORMAL)
		return;

	if ((client = coma_frame_client_at(evt->x, evt->y)) == NULL)
		return;

	if (client != client_active &&
	    !(frame_active->flags & COMA_FRAME_ZOOMED)) {
		client->frame->focus = client;
		coma_frame_focus(client->frame, 0);
	}

	term = client->term;

	if (coma_term_mouse_grabbed(term, mods)) {
		mouse_term = term;
		coma_term_mouse_button(term, evt->x, evt->y, button, 1, mods);
		return;
	}

	switch (button) {
	case 1:
		select_term = term;
		coma_term_select_start(term, evt->x, evt->y, evt->clicks);
		break;
	case 2:
		wm_paste(term);
		break;
	}
}

static void
wm_mouse_motion(SDL_MouseMotionEvent *evt)
{
	u_int32_t	mods;
	struct client	*client;

	wm_pointer(1);
	mods = wm_mods(SDL_GetModState());

	if (select_term != NULL) {
		coma_term_select_extend(select_term, evt->x, evt->y);
		return;
	}

	if (mouse_term != NULL) {
		coma_term_mouse_move(mouse_term, evt->x, evt->y, mods);
		return;
	}

	if (mode == WM_MODE_NORMAL)
		coma_frame_mouseover(evt->x, evt->y);

	client = coma_frame_client_at(evt->x, evt->y);
	wm_pointer_shape(client != NULL);

	if (client != NULL && coma_term_mouse_grabbed(client->term, mods))
		coma_term_mouse_move(client->term, evt->x, evt->y, mods);
}

static void
wm_mouse_wheel(SDL_MouseWheelEvent *evt)
{
	u_int32_t	mods;
	struct client	*client;
	int		x, y, button;

	wm_pointer(1);

	if (evt->y == 0)
		return;

	mods = wm_mods(SDL_GetModState());
	(void)SDL_GetMouseState(&x, &y);

	if ((client = coma_frame_client_at(x, y)) == NULL)
		return;

	if (coma_term_mouse_grabbed(client->term, mods)) {
		if (evt->y > 0)
			button = 4;
		else
			button = 5;
		coma_term_mouse_button(client->term, x, y, button, 1, mods);
		return;
	}

	coma_term_wheel(client->term, evt->y * 3);
}

static void
wm_command(void)
{
	wm_input(32, wm_command_input);
}

static void
wm_command_input(char *cmd)
{
	char	*argv[32];

	if (coma_split_arguments(cmd, argv, 32)) {
		if (client_active == NULL)
			return;

		if (!strcmp(argv[0], "tag") && argv[1] != NULL) {
			free(client_active->tag);
			if ((client_active->tag = strdup(argv[1])) == NULL)
				fatal("strdup");
		} else if (!strcmp(argv[0], "untag")) {
			free(client_active->tag);
			client_active->tag = NULL;
		}

		coma_frame_bar_update(client_active->frame);
	}
}

static void
wm_unbind(u_int32_t sym)
{
	int		i;
	struct uaction	*ua, *next;

	for (i = 0; actions[i].name != NULL; i++) {
		if (actions[i].sym == sym)
			actions[i].sym = 0;
	}

	for (ua = LIST_FIRST(&uactions); ua != NULL; ua = next) {
		next = LIST_NEXT(ua, list);
		if (ua->sym == sym) {
			LIST_REMOVE(ua, list);
			free(ua->action);
			free(ua);
		}
	}
}

static void
wm_input(size_t max, void (*cb)(char *))
{
	if (max > sizeof(input_buf))
		max = sizeof(input_buf);

	input_max = max;
	input_done = cb;
	input_buf[0] = '\0';

	mode = WM_MODE_INPUT;
	coma_wm_dirty();
}

static void
wm_run_shell_command(const char *cmd)
{
	char	*copy, *argv[COMA_SHELL_ARGV];

	if ((copy = strdup(cmd)) == NULL)
		fatal("strdup");

	if (coma_split_arguments(copy, argv, COMA_SHELL_ARGV))
		coma_execute(argv);

	free(copy);
}

static void
wm_client_list(void)
{
	mode = WM_MODE_CLIENTS;
	coma_wm_dirty();
}

static void
wm_layout_swap(void)
{
	mode = WM_MODE_LAYOUT;
}

static void
wm_io(void)
{
	struct client	*client, *next;

	output_last = SDL_GetTicks();

	for (client = TAILQ_FIRST(&clients); client != NULL; client = next) {
		next = TAILQ_NEXT(client, glist);

		coma_term_flush(client->term);

		if (coma_term_read(client->term) == -1)
			coma_client_exited(client);
	}

	coma_wm_io_update();
}

static void
wm_io_ack(void)
{
	ssize_t		ret;

	if (io_pipe[1] != -1) {
		ret = write(io_pipe[1], "a", 1);
		(void)ret;
	}
}

static int
wm_io_thread(void *arg)
{
	SDL_Event		evt;
	char			buf[64];
	struct pollfd		pfd[WM_IO_MAX + 1];
	int			n, i, ready, waiting;

	waiting = 0;

	while (coma_atomic_read(&io_quit) == 0) {
		n = 0;
		pfd[n].fd = io_pipe[0];
		pfd[n].events = POLLIN;
		n++;

		if (waiting == 0) {
			SDL_LockMutex(io_lock);
			for (i = 0; i < io_nfds; i++) {
				pfd[n].fd = io_fds[i];
				pfd[n].events = io_events[i];
				n++;
			}
			SDL_UnlockMutex(io_lock);
		}

		if (poll(pfd, n, -1) == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll: %s", errno_s);
		}

		if (coma_atomic_read(&io_quit))
			break;

		if (pfd[0].revents & POLLIN) {
			while (read(io_pipe[0], buf, sizeof(buf)) > 0)
				;
			waiting = 0;
		}

		if (coma_atomic_read(&sig_recv) != -1 ||
		    coma_atomic_read(&sig_chld))
			ready = 1;
		else
			ready = 0;

		for (i = 1; i < n; i++) {
			if (pfd[i].revents)
				ready = 1;
		}

		if (ready) {
			waiting = 1;
			memset(&evt, 0, sizeof(evt));
			evt.type = io_event;
			if (SDL_PushEvent(&evt) < 0)
				coma_log("SDL_PushEvent: %s", SDL_GetError());
		}
	}

	return (0);
}

static int
wm_quit_confirm(void)
{
	SDL_MessageBoxData		data;
	int				button;
	const char			*driver;
	SDL_MessageBoxButtonData	buttons[2];

	if (TAILQ_EMPTY(&clients))
		return (1);

	driver = SDL_GetCurrentVideoDriver();

	if (driver == NULL || !strcmp(driver, "offscreen") ||
	    !strcmp(driver, "dummy"))
		return (1);

	memset(&data, 0, sizeof(data));
	memset(buttons, 0, sizeof(buttons));

	buttons[0].buttonid = 0;
	buttons[0].text = "Cancel";
	buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
	    SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

	buttons[1].buttonid = 1;
	buttons[1].text = "Quit";

	data.title = "coma";
	data.numbuttons = 2;
	data.window = window;
	data.buttons = buttons;
	data.flags = SDL_MESSAGEBOX_WARNING;
	data.message = "Quit coma and close all terminals?";

	if (SDL_ShowMessageBox(&data, &button) != 0)
		return (1);

	return (button == 1);
}

static int
wm_primary(void)
{
	const char	*driver;

	if ((driver = SDL_GetCurrentVideoDriver()) == NULL)
		return (0);

	return (!strcmp(driver, "x11") || !strcmp(driver, "wayland"));
}

static int
wm_color_parse(const char *rgb, struct color *color)
{
	unsigned int	r, g, b;

	if (strlen(rgb) != 7 || rgb[0] != '#')
		return (-1);

	if (sscanf(rgb + 1, "%02x%02x%02x", &r, &g, &b) != 3)
		return (-1);

	color->r = r;
	color->g = g;
	color->b = b;

	return (0);
}
