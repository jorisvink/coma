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

#include <sys/types.h>
#include <sys/queue.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#include <poll.h>
#include <stdlib.h>
#include <stdio.h>

#include "coma.h"

static void	wm_restart(void);
static void	wm_teardown(void);
static void	wm_screen_init(void);

static void	wm_handle_prefix(XKeyEvent *);
static void	wm_mouse_click(XButtonEvent *);
static void	wm_mouse_motion(XMotionEvent *);

static void	wm_window_map(XMapRequestEvent *);
static void	wm_window_destroy(XDestroyWindowEvent *);
static void	wm_window_configure(XConfigureRequestEvent *);

static int	wm_error(Display *, XErrorEvent *);
static int	wm_error_active(Display *, XErrorEvent *);

Display		*dpy = NULL;
XftFont		*font = NULL;
u_int16_t	screen_width = 0;
u_int16_t	screen_height = 0;

unsigned int	prefix_mod = COMA_MOD_KEY;
KeySym		prefix_key = COMA_PREFIX_KEY;

static Window	key_input = None;

struct {
	const char	*name;
	const char	*rgb;
	int		allocated;
	XftColor	color;
} xft_colors[] = {
	{ "client-active",		"#55007a",	0,	{ 0 }},
	{ "client-inactive",		"#222222",	0,	{ 0 }},
	{ "frame-bar",			"#55007a",	0,	{ 0 }},
	{ "frame-bar-client-active",	"#ffffff",	0,	{ 0 }},
	{ "frame-bar-client-inactive",	"#555555",	0,	{ 0 }},
	{ NULL,				NULL,		0,	{ 0 }},
};

struct {
	const char	*name;
	KeySym		sym;
	void		(*cb)(void);
} actions[] = {
	{ "frame-prev",			XK_h,		coma_frame_prev },
	{ "frame-next",			XK_l,		coma_frame_next },
	{ "frame-popup",		XK_space,	coma_frame_popup },

	{ "frame-zoom",			XK_z,	coma_frame_zoom },
	{ "frame-split",		XK_s,	coma_frame_split },
	{ "frame-merge",		XK_m,	coma_frame_merge },
	{ "frame-split-next",		XK_f,	coma_frame_split_next },

	{ "frame-move-client-left",	XK_i,	coma_frame_client_move_left },
	{ "frame-move-client-right", 	XK_o,	coma_frame_client_move_right },

	{ "coma-restart",		XK_r,	wm_restart },
	{ "coma-terminal",		XK_c,	coma_spawn_terminal },

	{ "client-kill",		XK_k,	coma_client_kill_active },
	{ "client-prev",		XK_p,	coma_frame_client_prev },
	{ "client-next",		XK_n,	coma_frame_client_next },

	{ NULL, 0, NULL }
};

void
coma_wm_init(void)
{
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		fatal("failed to open display");
}

void
coma_wm_setup(void)
{
	XSetErrorHandler(wm_error_active);
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);

	XSetErrorHandler(wm_error);

	wm_screen_init();
}

void
coma_wm_run(void)
{
	XEvent			evt;
	struct pollfd		pfd[1];
	int			running, ret;

	running = 1;

	while (running) {
		if (sig_recv != -1) {
			switch (sig_recv) {
			case SIGQUIT:
			case SIGINT:
				running = 0;
				continue;
			case SIGHUP:
				running = 0;
				restart = 1;
				continue;
			case SIGCHLD:
				coma_reap();
				break;
			default:
				break;
			}
			sig_recv = -1;
		}

		pfd[0].fd = ConnectionNumber(dpy);
		pfd[0].events = POLLIN;

		ret = poll(pfd, 1, 1000);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll: %s", errno_s);
		}

		if (ret == 0 || !(pfd[0].revents & POLLIN))
			continue;

		while (XPending(dpy)) {
			XNextEvent(dpy, &evt);

			switch (evt.type) {
			case ButtonRelease:
				wm_mouse_click(&evt.xbutton);
				break;
			case MotionNotify:
				wm_mouse_motion(&evt.xmotion);
				break;
			case DestroyNotify:
				wm_window_destroy(&evt.xdestroywindow);
				break;
			case ConfigureRequest:
				wm_window_configure(&evt.xconfigurerequest);
				break;
			case MapRequest:
				wm_window_map(&evt.xmaprequest);
				break;
			case KeyPress:
				wm_handle_prefix(&evt.xkey);
				break;
			}
		}
	}

	wm_teardown();
}

XftColor *
coma_wm_color(const char *name)
{
	int		i;

	for (i = 0; xft_colors[i].name != NULL; i++) {
		if (!strcmp(name, xft_colors[i].name))
			return (&xft_colors[i].color);
	}

	return (&xft_colors[0].color);
}

void
coma_wm_register_prefix(Window win)
{
	KeyCode		c;

	XUngrabKey(dpy, AnyKey, AnyModifier, win);

	c = XKeysymToKeycode(dpy, prefix_key);
	XGrabKey(dpy, c, prefix_mod, win, True, GrabModeAsync, GrabModeAsync);
}

int
coma_wm_register_action(const char *action, KeySym sym)
{
	int		i;

	for (i = 0; actions[i].name != NULL; i++) {
		if (!strcmp(actions[i].name, action)) {
			actions[i].sym = sym;
			return (0);
		}
	}

	return (-1);
}

int
coma_wm_register_color(const char *name, const char *rgb)
{
	Visual		*visual;
	Colormap	colormap;
	int		screen, i;

	for (i = 0; xft_colors[i].name != NULL; i++) {
		if (!strcmp(name, xft_colors[i].name))
			break;
	}

	if (xft_colors[i].name == NULL)
		return (-1);

	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);

	if (xft_colors[i].allocated)
		XftColorFree(dpy, visual, colormap, &xft_colors[i].color);

	XftColorAllocName(dpy, visual, colormap, rgb, &xft_colors[i].color);
	xft_colors[i].allocated = 1;

	return (0);
}

static void
wm_restart(void)
{
	restart = 1;
	sig_recv = SIGQUIT;
}

static void
wm_teardown(void)
{
	coma_frame_cleanup();

	XftFontClose(dpy, font);
	XDestroyWindow(dpy, key_input);

	XUngrabKeyboard(dpy, CurrentTime);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XCloseDisplay(dpy);
}

static void
wm_screen_init(void)
{
	int		screen;
	Visual		*visual;
	Colormap	colormap;
	unsigned int	windows, idx;
	Window		root, wr, wp, *childwin;

	screen = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);

	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	screen_width = DisplayWidth(dpy, screen);
	screen_height = DisplayHeight(dpy, screen);

	if ((font = XftFontOpenName(dpy, screen, COMA_WM_FONT)) == NULL)
		fatal("failed to open %s", COMA_WM_FONT);

	for (idx = 0; xft_colors[idx].name != NULL; idx++) {
		if (xft_colors[idx].allocated == 0) {
			XftColorAllocName(dpy, visual, colormap,
			    xft_colors[idx].rgb, &xft_colors[idx].color);
		}
	}

	XSelectInput(dpy, root,
	    SubstructureRedirectMask | SubstructureNotifyMask |
	    EnterWindowMask | LeaveWindowMask | KeyPressMask |
	    PointerMotionMask);

	coma_frame_setup();
	coma_wm_register_prefix(root);

	if (XQueryTree(dpy, root, &wr, &wp, &childwin, &windows)) {
		for (idx = 0; idx < windows; idx++)
			coma_client_create(childwin[idx]);
	}

	coma_frame_bars_create();

	key_input = XCreateSimpleWindow(dpy, root,
	    0, 0, 1, 1, 0, WhitePixel(dpy, screen), BlackPixel(dpy, screen));
	XSelectInput(dpy, key_input, KeyPressMask);
	XMapWindow(dpy, key_input);

	XSync(dpy, False);
}

static void
wm_window_destroy(XDestroyWindowEvent *evt)
{
	struct client	*client;

	if (evt->window == key_input)
		return;

	if ((client = coma_client_find(evt->window)) == NULL)
		return;

	coma_client_destroy(client);
}

static void
wm_handle_prefix(XKeyEvent *prefix)
{
	XEvent			evt;
	KeySym			sym;
	Window			focus;
	struct client		*client;
	int			revert, i;

	client = client_active;
	XGetInputFocus(dpy, &focus, &revert);

	sym = XkbKeycodeToKeysym(dpy, prefix->keycode, 0, 0);

	if (sym != prefix_key)
		return;

	XSetInputFocus(dpy, key_input, RevertToNone, CurrentTime);
	XMaskEvent(dpy, KeyPressMask, &evt);

	if (evt.type != KeyPress) {
		XSetInputFocus(dpy, focus, RevertToPointerRoot, CurrentTime);
		return;
	}

	sym = XkbKeycodeToKeysym(dpy, evt.xkey.keycode, 0, 0);

	for (i = 0; actions[i].name != NULL; i++) {
		if (actions[i].sym == sym) {
			actions[i].cb();
			break;
		}
	}

	if (client == client_active)
		XSetInputFocus(dpy, focus, RevertToPointerRoot, CurrentTime);
}

static void
wm_mouse_click(XButtonEvent *evt)
{
	coma_frame_bar_click(evt->window, evt->x);
}

static void
wm_mouse_motion(XMotionEvent *evt)
{
	static Time		last = 0;

	if ((evt->time - last) <= (1000 / 60))
		return;

	last = evt->time;
	coma_frame_mouseover(evt->x, evt->y);
}

static void
wm_window_map(XMapRequestEvent *evt)
{
	struct client		*client;

	if ((client = coma_client_find(evt->window)) == NULL)
		coma_client_create(evt->window);
}

static void
wm_window_configure(XConfigureRequestEvent *evt)
{
	XWindowChanges		cfg;
	struct client		*client;

	memset(&cfg, 0, sizeof(cfg));

	if ((client = coma_client_find(evt->window)) != NULL) {
		if (evt->value_mask & CWBorderWidth)
			client->bw = evt->border_width;

		if (evt->value_mask & CWWidth)
			client->w = evt->width;

		if (evt->value_mask & CWHeight)
			client->h = evt->height;

		if (evt->value_mask & CWX)
			client->x = evt->x;

		if (evt->value_mask & CWY)
			client->y = evt->y;

		cfg.x = client->x;
		cfg.y = client->y;
		cfg.width = client->w;
		cfg.height = client->h;
		cfg.border_width = client->bw;

		XConfigureWindow(dpy, evt->window, evt->value_mask, &cfg);
		coma_client_send_configure(client);
	} else {
		cfg.x = evt->x;
		cfg.y = evt->y;
		cfg.stack_mode = Above;
		cfg.width = evt->width;
		cfg.height = evt->height;
		evt->value_mask &= ~CWStackMode;
		cfg.border_width = evt->border_width;
		XConfigureWindow(dpy, evt->window, evt->value_mask, &cfg);
	}
}

static int
wm_error(Display *edpy, XErrorEvent *error)
{
	int		len;
	char		msg[128], req[128], num[32];

	len = snprintf(num, sizeof(num), "%d", error->request_code);
	if (len == -1 || (size_t)len >= sizeof(num))
		fatal("overflow creating num '%d' buf", error->request_code);

	XGetErrorText(dpy, error->error_code, msg, sizeof(msg));
	XGetErrorDatabaseText(dpy, "XRequest", num, "<unknown>",
	    req, sizeof(req));

	printf("%s: %s\n", req, msg);

	return (0);
}

static int
wm_error_active(Display *edpy, XErrorEvent *error)
{
	fatal("another wm is already running");
	return (0);
}