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

static void	wm_teardown(void);
static void	wm_screen_init(void);

static void	wm_handle_prefix(XKeyEvent *);
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

static Window	key_input = None;
static XftColor	xft_colors[COMA_WM_COLOR_MAX];

/* Must be in sync with the defines COMA_WM_COLOR_* */
static const char *colors[] = {
	"#55007a",
	"#222222",
	"#55007a",
	"#ffffff",
	"#555555",
	NULL
};

void
coma_wm_setup(void)
{
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		fatal("failed to open display");

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
	Time			motion;
	int			running, ret;

	motion = 0;
	running = 1;

	while (running) {
		if (sig_recv != -1) {
			switch (sig_recv) {
			case SIGQUIT:
			case SIGINT:
			case SIGHUP:
				running = 0;
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
coma_wm_xftcolor(u_int32_t which)
{
	if (which >= COMA_WM_COLOR_MAX)
		fatal("bad color index: %u", which);

	return (&xft_colors[which]);
}

void
coma_wm_register_prefix(Window win)
{
	KeyCode		c;

	XUngrabKey(dpy, AnyKey, AnyModifier, win);

	c = XKeysymToKeycode(dpy, COMA_PREFIX_KEY);
	XGrabKey(dpy, c, ControlMask, win, True, GrabModeAsync, GrabModeAsync);
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
	XftColor	xc;
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

	for (idx = 0; idx < COMA_WM_COLOR_MAX; idx++) {
		XftColorAllocName(dpy, visual, colormap, colors[idx], &xc);
		xft_colors[idx] = xc;
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
	int			revert;
	struct client		*client;

	client = client_active;
	XGetInputFocus(dpy, &focus, &revert);

	sym = XkbKeycodeToKeysym(dpy, prefix->keycode, 0, 0);

	if (sym != COMA_PREFIX_KEY)
		return;

	XSetInputFocus(dpy, key_input, RevertToNone, CurrentTime);
	XMaskEvent(dpy, KeyPressMask, &evt);

	if (evt.type != KeyPress)
		return;

	sym = XkbKeycodeToKeysym(dpy, evt.xkey.keycode, 0, 0);
	switch (sym) {
	case XK_i:
		coma_frame_client_move(COMA_CLIENT_MOVE_LEFT);
		break;
	case XK_o:
		coma_frame_client_move(COMA_CLIENT_MOVE_RIGHT);
		break;
	case XK_space:
		coma_frame_popup();
		break;
	case XK_c:
		coma_spawn_terminal();
		break;
	case XK_p:
		coma_frame_client_prev();
		break;
	case XK_n:
		coma_frame_client_next();
		break;
	case XK_h:
	case XK_Left:
		coma_frame_prev();
		break;
	case XK_l:
	case XK_Right:
		coma_frame_next();
		break;
	case XK_Up:
	case XK_Down:
		coma_frame_split_next();
		break;
	case XK_m:
		coma_frame_merge();
		break;
	case XK_s:
		coma_frame_split();
		break;
	case XK_k:
		coma_client_kill_active();
		break;
	case XK_r:
		restart = 1;
		sig_recv = SIGQUIT;
		break;
	}

	if (client == client_active)
		XSetInputFocus(dpy, focus, RevertToPointerRoot, CurrentTime);
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
