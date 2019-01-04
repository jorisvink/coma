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

#include <sys/queue.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#include <errno.h>
#include <string.h>
#include <signal.h>

#define errno_s				strerror(errno)

#define COMA_VERSION			"0.1"
#define COMA_PREFIX_KEY			XK_t

#define COMA_WM_COLOR_WIN_ACTIVE	0
#define COMA_WM_COLOR_WIN_INACTIVE	1
#define COMA_WM_COLOR_MAX		2

/* xterm with fixed font is 484 pixels wide (80 columns). */
#define COMA_FRAME_WIDTH_DEFAULT	484

/* xterm with fixed font 161 columns for tmux sessions. */
#define COMA_FRAME_WIDTH_LARGE		970

#define COMA_FRAME_GAP			10

struct frame;

struct client {
	u_int32_t		id;

	Window			window;
	struct frame		*frame;

	u_int16_t		w;
	u_int16_t		h;
	u_int16_t		x;
	u_int16_t		y;
	u_int16_t		bw;

	TAILQ_ENTRY(client)	list;
};

TAILQ_HEAD(client_list, client);

struct frame {
	u_int16_t		id;
	u_int16_t		width;
	u_int16_t		offset;
	struct client		*focus;
	struct client_list	clients;

	TAILQ_ENTRY(frame)	list;
};

TAILQ_HEAD(frame_list, frame);

extern Display			*dpy;
extern int			restart;
extern u_int16_t		frame_width;
extern u_int16_t		screen_width;
extern u_int16_t		screen_height;
extern struct frame		*frame_active;
extern struct client		*client_active;
extern volatile sig_atomic_t	sig_recv;

void		fatal(const char *, ...);

void		coma_reap(void);
void		coma_spawn_terminal(void);

void		*coma_malloc(size_t);
void		*coma_calloc(size_t, size_t);

void		coma_wm_run(void);
void		coma_wm_setup(void);
XftColor	*coma_wm_xftcolor(u_int32_t);
void		coma_wm_register_prefix(Window);

void		coma_frame_prev(void);
void		coma_frame_next(void);
void		coma_frame_setup(void);
void		coma_frame_popup(void);
void		coma_frame_select_any(void);
void		coma_frame_client_prev(void);
void		coma_frame_client_next(void);

void		coma_client_init(void);
void		coma_client_create(Window);
void		coma_client_kill_active(void);
void		coma_client_map(struct client *);
void		coma_client_hide(struct client *);
void		coma_client_focus(struct client *);
void		coma_client_unhide(struct client *);
void		coma_client_adjust(struct client *);
void		coma_client_destroy(struct client *);
void		coma_client_send_configure(struct client *);

struct client	*coma_client_find(Window);
struct client	*coma_frame_find_client(Window);

#endif
