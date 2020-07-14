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
#include <sys/stat.h>
#include <sys/queue.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "coma.h"

static void	wm_run(void);
static void	wm_command(void);
static void	wm_restart(void);
static void	wm_teardown(void);
static void	wm_screen_init(void);
static void	wm_client_list(void);
static void	wm_query_atoms(void);
static Atom	wm_atom(const char *);
static void	wm_run_command(char *, int);
static void	wm_run_shell_command(char *);
static int	wm_input(char *, size_t, void (*autocomplete)(char *, size_t));

static void	wm_client_check(Window);
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
int		client_discovery = 0;

Atom		atom_frame_id = None;
Atom		atom_client_pos = None;
Atom		atom_client_act = None;
Atom		atom_net_wm_pid = None;
Atom		atom_client_visible = None;

char		*font_name = NULL;
unsigned int	prefix_mod = COMA_MOD_KEY;
KeySym		prefix_key = COMA_PREFIX_KEY;

static Window	key_input = None;
static Window	cmd_input = None;
static Window	clients_win = None;

static XftDraw	*cmd_xft = NULL;
static XftDraw	*clients_xft = NULL;

struct {
	const char	*name;
	const char	*rgb;
	int		allocated;
	XftColor	color;
} xft_colors[] = {
	{ "client-active",		"#55007a",	0,	{ 0 }},
	{ "client-inactive",		"#222222",	0,	{ 0 }},
	{ "frame-bar",			"#55007a",	0,	{ 0 }},
	{ "frame-bar-directory",	"#aaaaaa",	0,	{ 0 }},
	{ "frame-bar-client-active",	"#ffffff",	0,	{ 0 }},
	{ "frame-bar-client-inactive",	"#555555",	0,	{ 0 }},
	{ "command-input",		"#ffffff",	0,	{ 0 }},
	{ "command-bar",		"#000000",	0,	{ 0 }},
	{ "command-border",		"#55007a",	0,	{ 0 }},
	{ NULL,				NULL,		0,	{ 0 }},
};

struct uaction {
	KeySym			sym;
	char			*action;
	int			hold;
	int			shell;
	LIST_ENTRY(uaction)	list;
};

static LIST_HEAD(, uaction)	uactions;

struct {
	const char	*name;
	KeySym		sym;
	void		(*cb)(void);
} actions[] = {
	{ "frame-prev",		XK_h,		coma_frame_prev },
	{ "frame-next",		XK_l,		coma_frame_next },
	{ "frame-popup",	XK_space,	coma_frame_popup_toggle },

	{ "frame-zoom",		XK_z,	coma_frame_zoom },
	{ "frame-split",	XK_s,	coma_frame_split },
	{ "frame-merge",	XK_m,	coma_frame_merge },
	{ "frame-split-next",	XK_f,	coma_frame_split_next },

	{ "frame-move-client-left",	XK_i,	coma_frame_client_move_left },
	{ "frame-move-client-right", 	XK_o,	coma_frame_client_move_right },

	{ "coma-restart",		XK_r,	wm_restart },
	{ "coma-terminal",		XK_c,	coma_spawn_terminal },

	{ "client-kill",		XK_k,	coma_client_kill_active },
	{ "client-prev",		XK_p,	coma_frame_client_prev },
	{ "client-next",		XK_n,	coma_frame_client_next },

	{ "coma-run",			XK_e,		wm_run },
	{ "coma-command",		XK_colon,	wm_command },
	{ "coma-client-list",		XK_q,		wm_client_list },

	{ NULL, 0, NULL }
};

void
coma_wm_init(void)
{
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		fatal("failed to open display");

	if ((font_name = strdup(COMA_WM_FONT)) == NULL)
		fatal("strdup");

	LIST_INIT(&uactions);
}

void
coma_wm_setup(void)
{
	XSetErrorHandler(wm_error_active);
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, True);

	XSetErrorHandler(wm_error);

	wm_query_atoms();
	wm_screen_init();
}

void
coma_wm_run(void)
{
	XEvent			evt;
	struct pollfd		pfd[1];
	int			running, ret;

	running = 1;
	restart = 0;

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

		ret = poll(pfd, 1, 500);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll: %s", errno_s);
		}

		coma_frame_update_titles();

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

			XSync(dpy, False);
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
	struct uaction	*ua;

	if (!strncmp(COMA_ACTION_PREFIX, action, COMA_ACTION_PREFIX_LEN)) {
		ua = coma_calloc(1, sizeof(*ua));
		ua->sym = sym;
		ua->hold = 1;
		ua->action = strdup(action + COMA_ACTION_PREFIX_LEN);
		if (ua->action == NULL)
			fatal("strdup");
		LIST_INSERT_HEAD(&uactions, ua, list);
		return (0);
	}

	if (!strncmp(COMA_ACTION_NOHOLD_PREFIX,
	    action, COMA_ACTION_NOHOLD_PREFIX_LEN)) {
		ua = coma_calloc(1, sizeof(*ua));
		ua->sym = sym;
		ua->action = strdup(action + COMA_ACTION_NOHOLD_PREFIX_LEN);
		if (ua->action == NULL)
			fatal("strdup");
		LIST_INSERT_HEAD(&uactions, ua, list);
		return (0);
	}

	if (!strncmp(COMA_ACTION_SHELL_PREFIX,
	    action, COMA_ACTION_SHELL_PREFIX_LEN)) {
		ua = coma_calloc(1, sizeof(*ua));
		ua->sym = sym;
		ua->shell = 1;
		ua->action = strdup(action + COMA_ACTION_SHELL_PREFIX_LEN);
		if (ua->action == NULL)
			fatal("strdup");
		LIST_INSERT_HEAD(&uactions, ua, list);
		return (0);
	}

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

void
coma_wm_property_write(Window win, Atom prop, u_int32_t value)
{
	(void)XChangeProperty(dpy, win, prop, XA_INTEGER, 32,
	    PropModeReplace, (unsigned char *)&value, 1);

	coma_log("win 0x%08x prop 0x%08x = %u", win, prop, value);
}

int
coma_wm_property_read(Window win, Atom prop, u_int32_t *val)
{
	int		ret;
	Atom		type;
	unsigned char	*data;
	int		format;
	unsigned long	nitems, bytes;

	ret = XGetWindowProperty(dpy, win, prop, 0, 32, False, AnyPropertyType,
	    &type, &format, &nitems, &bytes, &data);

	if (ret != Success) {
		coma_log("prop=0x%08x win=0x%08x bad prop", prop, win);
		return (-1);
	}

	if (type != XA_INTEGER && type != XA_CARDINAL) {
		coma_log("prop=0x%08x win=0x%08x type=0x%08x bad type",
		    prop, win, type);
		return (-1);
	}

	if (nitems != 1) {
		coma_log("prop=0x%08x win=0x%08x bad nitems %d",
		    prop, win, nitems);
		return (-1);
	}

	memcpy(val, data, sizeof(*val));
	XFree(data);

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
	struct uaction	*ua;

	while ((ua = LIST_FIRST(&uactions)) != NULL) {
		LIST_REMOVE(ua, list);
		free(ua->action);
		free(ua);
	}

	coma_frame_cleanup();

	XftFontClose(dpy, font);
	XftDrawDestroy(cmd_xft);
	XftDrawDestroy(clients_xft);

	XDestroyWindow(dpy, key_input);
	XDestroyWindow(dpy, cmd_input);
	XDestroyWindow(dpy, clients_win);

	XUngrabKeyboard(dpy, CurrentTime);
	XSync(dpy, True);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XCloseDisplay(dpy);
}

static void
wm_screen_init(void)
{
	u_int32_t	id;
	int		screen;
	struct client	*client;
	Visual		*visual;
	Colormap	colormap;
	XftColor	*bg, *border;
	unsigned int	windows, idx;
	Window		root, wr, wp, *childwin;

	screen = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);

	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	screen_width = DisplayWidth(dpy, screen);
	screen_height = DisplayHeight(dpy, screen);

	if ((font = XftFontOpenName(dpy, screen, font_name)) == NULL)
		fatal("failed to open %s", font_name);

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
	coma_frame_bars_create();

	client_discovery = 1;

	if (XQueryTree(dpy, root, &wr, &wp, &childwin, &windows)) {
		for (idx = 0; idx < windows; idx++)
			wm_client_check(childwin[idx]);
		XFree(childwin);
	}

	coma_frame_bar_sort();

	key_input = XCreateSimpleWindow(dpy, root,
	    0, 0, 1, 1, 0, WhitePixel(dpy, screen), BlackPixel(dpy, screen));
	XSelectInput(dpy, key_input, KeyPressMask);
	XMapWindow(dpy, key_input);

	bg = coma_wm_color("command-bar");
	border = coma_wm_color("command-border");

	cmd_input = XCreateSimpleWindow(dpy, root,
	    (screen_width / 2) - 200, (screen_height / 2) - 50, 400,
	    COMA_FRAME_BAR, 2, border->pixel, bg->pixel);

	if ((cmd_xft = XftDrawCreate(dpy, cmd_input, visual, colormap)) == NULL)
		fatal("XftDrawCreate failed");

	clients_win = XCreateSimpleWindow(dpy, root,
	    (screen_width / 2) - 220, (screen_height / 2) - 205, 400, 400, 2,
	    border->pixel, bg->pixel);

	if ((clients_xft = XftDrawCreate(dpy,
	    clients_win, visual, colormap)) == NULL)
		fatal("XftDrawCreate failed");

	if (coma_wm_property_read(root, atom_client_act, &id) == 0) {
		coma_log("client 0x%08x was active", id);
		if ((client = coma_client_find(id)) != NULL) {
			coma_client_focus(client);
			coma_frame_focus(client->frame, 1);
			if (client->frame == frame_popup)
				coma_frame_popup_show();
			coma_frame_bar_update(client->frame);
		}
	}

	client_discovery = 0;
	XSync(dpy, True);
}

static void
wm_query_atoms(void)
{
	atom_net_wm_pid = wm_atom("_NET_WM_PID");
	atom_frame_id = wm_atom("_COMA_WM_FRAME_ID");
	atom_client_pos = wm_atom("_COMA_WM_CLIENT_POS");
	atom_client_act = wm_atom("_COMA_WM_CLIENT_ACT");
	atom_client_visible = wm_atom("_COMA_WM_CLIENT_VISIBLE");

	coma_log("_NET_WM_PID Atom = 0x%08x", atom_net_wm_pid);
	coma_log("_COMA_WM_FRAME_ID Atom = 0x%08x", atom_frame_id);
	coma_log("_COMA_WM_CLIENT_POS Atom = 0x%08x", atom_client_pos);
	coma_log("_COMA_WM_CLIENT_ACT Atom = 0x%08x", atom_client_act);
	coma_log("_COMA_WM_CLIENT_VISIBLE Atom = 0x%08x", atom_client_visible);
}

static Atom
wm_atom(const char *name)
{
	Atom	prop;

	if ((prop = XInternAtom(dpy, name, False)) == None)
		fatal("failed to query Atom '%s'", name);

	return (prop);
}

static void
wm_run(void)
{
	char	cmd[2048];

	if (wm_input(cmd, sizeof(cmd), NULL) == -1)
		return;

	wm_run_command(cmd, 1);
}

static void
wm_command(void)
{
	char	cmd[32], *argv[32];

	if (wm_input(cmd, sizeof(cmd), NULL) == -1)
		return;

	if (coma_split_arguments(cmd, argv, 32)) {
		if (!strcmp(argv[0], "tag") && argv[1] != NULL) {
			if (client_active == NULL)
				return;

			free(client_active->tag);
			if ((client_active->tag = strdup(argv[1])) == NULL)
				fatal("strdup");

			coma_frame_bar_update(frame_active);
		} else if (!strcmp(argv[0], "untag")) {
			free(client_active->tag);
			client_active->tag = NULL;
		}
	}
}

static void
wm_run_command(char *cmd, int hold)
{
	int	off, title, local;
	char	*argv[COMA_SHELL_ARGV];

	off = 0;
	local = 1;
	title = -1;

	argv[off++] = terminal;

	if (hold)
		argv[off++] = "-hold";
	else
		argv[off++] = "+hold";

	if (client_active != NULL && client_active->host != NULL) {
		if (strcmp(myhost, client_active->host)) {
			argv[off++] = "-T";
			title = off;
			argv[off++] = client_active->host;
		}
	}

	argv[off++] = "-e";

	if (client_active != NULL && client_active->host != NULL) {
		if (strcmp(myhost, client_active->host)) {
			argv[off++] = "coma-remote";
			argv[off++] = client_active->host;
			if (client_active->pwd)
				argv[off++] = client_active->pwd;
			local = 0;
		}
	}

	if (local)
		argv[off++] = "coma-cmd";

	if (coma_split_arguments(cmd, argv + off, COMA_SHELL_ARGV - off)) {
		if (!strcmp(argv[off], "vi") || !strcmp(argv[off], "vim"))
			argv[1] = "+hold";

		if (title != -1)
			argv[title] = argv[off];

		coma_execute(argv);
	}
}

static void
wm_run_shell_command(char *cmd)
{
	char	*argv[COMA_SHELL_ARGV];

	if (coma_split_arguments(cmd, argv, COMA_SHELL_ARGV))
		coma_execute(argv);
}

static int
wm_input(char *cmd, size_t len, void (*autocomplete)(char *, size_t))
{
	XEvent			evt;
	KeySym			sym;
	char			c[2];
	size_t			clen;
	Window			focus;
	XftColor		*color;
	int			revert;
	struct client		*client;

	memset(cmd, 0, len);

	XSelectInput(dpy, cmd_input, KeyPressMask);
	XMapWindow(dpy, cmd_input);
	XRaiseWindow(dpy, cmd_input);

	client = client_active;
	XGetInputFocus(dpy, &focus, &revert);
	XSetInputFocus(dpy, cmd_input, RevertToNone, CurrentTime);

	color = coma_wm_color("command-input");

	for (;;) {
		clen = strlen(cmd);

		XClearWindow(dpy, cmd_input);

		if (clen > 0) {
			XftDrawStringUtf8(cmd_xft, color, font,
			    5, 15, (const FcChar8 *)cmd, clen);
		}

		XMaskEvent(dpy, KeyPressMask, &evt);
		sym = XkbKeycodeToKeysym(dpy, evt.xkey.keycode, 0,
		    (evt.xkey.state & ShiftMask));

		if (sym == XK_Shift_L || sym == XK_Shift_R)
			continue;

		if (sym == XK_BackSpace) {
			if (clen > 0)
				cmd[clen - 1] = '\0';
			continue;
		}

		if (sym == XK_Tab) {
			if (autocomplete != NULL)
				autocomplete(cmd, len);
			continue;
		}

		if (sym == XK_Escape || sym == XK_Return)
			break;

		c[0] = sym;
		c[1] = '\0';

		if (strlcat(cmd, c, len) >= len)
			continue;
	}

	XUnmapWindow(dpy, cmd_input);

	if (client == client_active)
		XSetInputFocus(dpy, focus, RevertToPointerRoot, CurrentTime);

	if (clen > 1 && sym == XK_Return)
		return (0);

	return (-1);
}

static void
wm_client_list(void)
{
	XEvent			evt;
	KeySym			sym;
	Window			focus;
	struct frame		*prev;
	XftColor		*color;
	char			c, buf[128];
	struct client		*client, *cl, *list[16];
	int			revert, y, idx, len, limit;

	color = coma_wm_color("command-input");

	XSelectInput(dpy, clients_win, KeyPressMask);
	XMapWindow(dpy, clients_win);
	XRaiseWindow(dpy, clients_win);

	client = client_active;
	XGetInputFocus(dpy, &focus, &revert);
	XSetInputFocus(dpy, clients_win, RevertToNone, CurrentTime);

	XClearWindow(dpy, clients_win);

	y = 20;
	idx = 0;

	TAILQ_FOREACH(cl, &clients, glist) {
		if (idx > 15)
			break;

		if (idx < 10)
			c = '0' + idx;
		else
			c = 'a' + (idx - 10);

		if (cl->tag) {
			len = snprintf(buf, sizeof(buf),
			    "#%c [%s] [%s]", c, cl->tag, cl->host);
		} else if (cl->cmd) {
			len = snprintf(buf, sizeof(buf),
			    "#%c [%s] [%s]", c, cl->cmd, cl->host);
		} else {
			len = snprintf(buf, sizeof(buf),
			    "#%c [%s]", c, cl->host);
		}

		if (len == -1 || (size_t)len >= sizeof(buf))
			len = snprintf(buf, sizeof(buf), "#%c [unknown]", c);

		if (len == -1 || (size_t)len >= sizeof(buf))
			fatal("failed to construct client list buffer");

		XftDrawStringUtf8(clients_xft, color, font,
		    5, y, (const FcChar8 *)buf, len);

		y += 15;

		list[idx++] = cl;
	}

	limit = idx;
	idx = -1;

	for (;;) {
		XMaskEvent(dpy, KeyPressMask, &evt);

		if (evt.type != KeyPress)
			continue;

		sym = XkbKeycodeToKeysym(dpy, evt.xkey.keycode, 0,
		    (evt.xkey.state & ShiftMask));

		if (sym == XK_Escape)
			break;

		if (sym >= XK_0 && sym <= XK_9) {
			idx = sym - XK_0;
			if (idx < limit)
				break;
		}

		if (sym >= XK_a && sym <= XK_f) {
			idx = (sym - XK_a) + 10;
			if (idx < limit)
				break;
		}
	}

	XUnmapWindow(dpy, clients_win);

	if (idx != -1 && idx < limit) {
		prev = frame_active;
		frame_active = list[idx]->frame;

		if (frame_active == frame_popup && prev != frame_popup)
			coma_frame_popup_show();

		if (frame_active != frame_popup && prev == frame_popup) {
			coma_frame_popup_hide();
			frame_active = list[idx]->frame;
		}

		coma_client_focus(list[idx]);
		coma_client_warp_pointer(list[idx]);
	}

	if (client == client_active)
		XSetInputFocus(dpy, focus, RevertToPointerRoot, CurrentTime);
}

static void
wm_client_check(Window window)
{
	u_int32_t	pid;

	if (coma_wm_property_read(window, atom_net_wm_pid, &pid) == -1) {
		coma_log("ignoring window 0x%08x", window);
		return;
	}

	coma_log("discovered window 0x%08x with pid %u", window, pid);
	coma_client_create(window);
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
	struct uaction		*ua;
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

	for (;;) {
		XMaskEvent(dpy, KeyPressMask, &evt);

		if (evt.type != KeyPress)
			goto out;

		sym = XkbKeycodeToKeysym(dpy, evt.xkey.keycode, 0,
		    (evt.xkey.state & ShiftMask));

		if (sym == XK_Shift_L || sym == XK_Shift_R)
			continue;

		break;
	}

	for (i = 0; actions[i].name != NULL; i++) {
		if (actions[i].sym == sym) {
			actions[i].cb();
			break;
		}
	}

	if (actions[i].name == NULL) {
		LIST_FOREACH(ua, &uactions, list) {
			if (ua->sym == sym) {
				if (ua->shell)
					wm_run_shell_command(ua->action);
				else
					wm_run_command(ua->action, ua->hold);
				break;
			}
		}
	}

out:
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

	coma_log("%s: %s", req, msg);

	return (0);
}

static int
wm_error_active(Display *edpy, XErrorEvent *error)
{
	fatal("another wm is already running");
	return (0);
}
