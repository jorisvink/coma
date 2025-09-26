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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "coma.h"

#define CLIENT_MOVE_LEFT		1
#define CLIENT_MOVE_RIGHT		2

#define LARGE_SINGLE_WINDOW		0
#define LARGE_DUAL_WINDOWS		1

static void	frame_layout_default(void);
static void	frame_layout_small_large(int);
static void	frame_bar_sort(struct frame *);
static void	frame_bar_create(struct frame *);

static void		frame_client_move(int);
static struct frame	*frame_find_left(void);
static struct frame	*frame_find_right(void);

static struct frame_list	frames;
static u_int32_t		frame_id = 1;
static u_int16_t		zoom_width = 0;
static struct frame		*popup_restore = NULL;

int				frame_count = -1;
int				frame_offset = -1;
u_int16_t			frame_height = 0;
u_int16_t			frame_y_offset = 0;
struct frame			*frame_popup = NULL;
struct frame			*frame_active = NULL;
u_int16_t			frame_gap = COMA_FRAME_GAP;
u_int16_t			frame_bar = COMA_FRAME_BAR;
u_int16_t			frame_width = COMA_FRAME_WIDTH;
u_int16_t			frame_border = COMA_FRAME_BORDER;
int				frame_layout = COMA_FRAME_LAYOUT_DEFAULT;

void
coma_frame_init(void)
{
	TAILQ_INIT(&frames);
}

void
coma_frame_setup(void)
{
	switch (frame_layout) {
	case COMA_FRAME_LAYOUT_DEFAULT:
		frame_layout_default();
		break;
	case COMA_FRAME_LAYOUT_SMALL_LARGE:
		frame_layout_small_large(LARGE_SINGLE_WINDOW);
		break;
	case COMA_FRAME_LAYOUT_SMALL_DUAL:
		frame_layout_small_large(LARGE_DUAL_WINDOWS);
		break;
	default:
		fatal("unknown frame layout %d", frame_layout);
	}

	frame_popup->id = UINT_MAX;
	frame_active = TAILQ_FIRST(&frames);

	coma_log("frame active is %u", frame_active->id);
}

void
coma_frame_layout(const char *mode)
{
	if (!strcmp(mode, "default")) {
		frame_layout = COMA_FRAME_LAYOUT_DEFAULT;
	} else if (!strcmp(mode, "small-large")) {
		frame_layout = COMA_FRAME_LAYOUT_SMALL_LARGE;
	} else if (!strcmp(mode, "small-dual")) {
		frame_layout = COMA_FRAME_LAYOUT_SMALL_DUAL;
	} else {
		fatal("unknown frame-layout '%s'", mode);
	}
}

void
coma_frame_cleanup(void)
{
	struct frame	*frame, *next;

	for (frame = TAILQ_FIRST(&frames); frame != NULL; frame = next) {
		next = TAILQ_NEXT(frame, list);
		TAILQ_REMOVE(&frames, frame, list);
		XDestroyWindow(dpy, frame->bar);
		XftDrawDestroy(frame->xft_draw);
		free(frame);
	}

	XDestroyWindow(dpy, frame_popup->bar);
	XftDrawDestroy(frame_popup->xft_draw);
	free(frame_popup);
}

void
coma_frame_popup_toggle(void)
{
	if (frame_active == frame_popup)
		coma_frame_popup_hide();
	else
		coma_frame_popup_show();
}

void
coma_frame_popup_hide(void)
{
	struct client	*client;

	TAILQ_FOREACH(client, &frame_popup->clients, list)
		coma_client_hide(client);

	if (frame_popup->split != NULL) {
		TAILQ_FOREACH(client,
		    &frame_popup->split->clients, list) {
			coma_client_hide(client);
		}
	}

	coma_frame_select_any();

	XUnmapWindow(dpy, frame_popup->bar);
	if (frame_popup->split != NULL)
		XUnmapWindow(dpy, frame_popup->split->bar);

	if (popup_restore != NULL) {
		coma_frame_focus(popup_restore, 1);
		popup_restore = NULL;
	}
}

void
coma_frame_popup_show(void)
{
	struct client	*client, *focus;

	if (frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	popup_restore = frame_active;

	focus = frame_popup->focus;
	frame_active = frame_popup;

	TAILQ_FOREACH(client, &frame_popup->clients, list)
		coma_client_unhide(client);

	if (frame_popup->split != NULL) {
		TAILQ_FOREACH(client,
		    &frame_popup->split->clients, list) {
			coma_client_unhide(client);
		}
	}

	XMapRaised(dpy, frame_popup->bar);
	coma_frame_bar_update(frame_popup);

	if (frame_popup->split != NULL) {
		XMapRaised(dpy, frame_popup->split->bar);
		coma_frame_bar_update(frame_popup->split);
	}

	if (focus != NULL)
		coma_client_focus(focus);
}

void
coma_frame_next(void)
{
	struct frame	*next;

	if (!(frame_active->flags & COMA_FRAME_INLIST) ||
	    frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	if ((next = frame_find_right()) != NULL)
		coma_frame_focus(next, 1);
}

void
coma_frame_prev(void)
{
	struct frame	*prev;

	if (!(frame_active->flags & COMA_FRAME_INLIST) ||
	    frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	if ((prev = frame_find_left()) != NULL)
		coma_frame_focus(prev, 1);
}

void
coma_frame_client_next(void)
{
	struct client	*next;

	if (frame_active->focus == NULL)
		return;

	next = TAILQ_PREV(frame_active->focus, client_list, list);
	if (next == NULL)
		next = TAILQ_LAST(&frame_active->clients, client_list);

	if (next != NULL) {
		coma_client_focus(next);
		coma_client_warp_pointer(next);
		coma_frame_bar_update(frame_active);
	}
}

void
coma_frame_client_prev(void)
{
	struct client	*prev;

	if (frame_active->focus == NULL)
		return;

	if ((prev = TAILQ_NEXT(frame_active->focus, list)) == NULL)
		prev = TAILQ_FIRST(&frame_active->clients);

	if (prev != NULL) {
		coma_client_focus(prev);
		coma_client_warp_pointer(prev);
		coma_frame_bar_update(frame_active);
	}
}

void
coma_frame_client_move_left(void)
{
	frame_client_move(CLIENT_MOVE_LEFT);
}

void
coma_frame_client_move_right(void)
{
	frame_client_move(CLIENT_MOVE_RIGHT);
}

void
coma_frame_split(void)
{
	struct frame	*frame;
	struct client	*client;
	u_int16_t	height, used, y;

	if (frame_active == frame_popup)
		return;

	if (frame_active->split != NULL)
		return;

	height = frame_border + frame_active->h + frame_border + frame_bar;
	used = (frame_border * 4) + (frame_bar * 2) + frame_gap;
	height = (height - used) / 2;

	y = frame_active->y + frame_border + height + frame_border +
	    frame_bar + frame_gap;

	frame = coma_frame_create(frame_active->w, height, frame_active->x, y);

	if (frame_active->flags & COMA_FRAME_INLIST)
		TAILQ_INSERT_TAIL(&frames, frame, list);

	frame->split = frame_active;
	frame->flags = frame_active->flags;

	frame_active->split = frame;
	frame_active->h = height;

	frame_active->orig_h = frame_active->h;

	frame_bar_create(frame_active);
	frame_bar_create(frame);

	TAILQ_FOREACH(client, &frame_active->clients, list)
		coma_client_adjust(client);

	coma_frame_bar_update(frame);
	coma_frame_bar_update(frame_active);

	frame_active = frame;
	coma_spawn_terminal();
}

void
coma_frame_merge(void)
{
	struct frame	*survives, *dies;
	struct client	*client, *focus, *next;

	if (frame_active->split == NULL)
		return;

	if (frame_active->y < frame_active->split->y) {
		survives = frame_active;
		dies = frame_active->split;
	} else {
		dies = frame_active;
		survives = frame_active->split;
	}

	focus = dies->focus;

	for (client = TAILQ_FIRST(&dies->clients);
	    client != NULL; client = next) {
		next = TAILQ_NEXT(client, list);
		TAILQ_REMOVE(&dies->clients, client, list);

		client->frame = survives;
		TAILQ_INSERT_TAIL(&survives->clients, client, list);
	}

	if (dies->flags & COMA_FRAME_INLIST)
		TAILQ_REMOVE(&frames, dies, list);

	XDestroyWindow(dpy, dies->bar);
	XftDrawDestroy(dies->xft_draw);
	free(dies);

	survives->split = NULL;
	survives->h = frame_height;
	survives->orig_h = survives->h;

	frame_active = survives;

	TAILQ_FOREACH(client, &frame_active->clients, list)
		coma_client_adjust(client);

	if (focus != NULL) {
		coma_client_focus(focus);
		coma_client_warp_pointer(focus);
	}

	frame_bar_create(frame_active);
	coma_frame_bar_update(frame_active);
}

void
coma_frame_split_next(void)
{
	if (frame_active->split == NULL)
		return;

	coma_frame_focus(frame_active->split, 1);
}

void
coma_frame_select_any(void)
{
	struct frame	*frame;

	frame = NULL;

	TAILQ_FOREACH(frame, &frames, list) {
		if (TAILQ_EMPTY(&frame->clients))
			continue;
		break;
	}

	if (frame == NULL)
		frame = TAILQ_FIRST(&frames);

	coma_frame_focus(frame, 1);
}

void
coma_frame_select_id(u_int32_t id)
{
	struct frame	*frame;

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame->id == id) {
			coma_frame_focus(frame, 1);
			return;
		}
	}

	if (frame_popup->id == id)
		coma_frame_popup_show();
}

void
coma_frame_mouseover(u_int16_t x, u_int16_t y)
{
	struct client		*client, *prev;
	struct frame		*frame, *prev_frame;

	if (frame_active == frame_popup)
		return;

	if (frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	frame = NULL;
	client = NULL;
	prev_frame = frame_active;
	prev = frame_active->focus;

	TAILQ_FOREACH(frame, &frames, list) {
		if (x >= frame->x && x <= frame->x + frame->w &&
		    y >= frame->y && y <= frame->y + frame->h)
			break;
	}

	if (frame == NULL)
		return;

	frame_active = frame;
	if (frame_active->focus != NULL)
		client = frame_active->focus;
	else
		client = TAILQ_FIRST(&frame_active->clients);

	if (client != NULL && prev != client)
		coma_client_focus(client);

	if (prev_frame)
		coma_frame_bar_update(prev_frame);

	coma_frame_bar_update(frame_active);
}

struct client *
coma_frame_find_client(Window window)
{
	struct client	*client;

	TAILQ_FOREACH(client, &clients, glist) {
		if (client->window == window)
			return (client);
	}

	return (NULL);
}

void
coma_frame_zoom(void)
{
	struct client		*client;

	if (frame_active->focus == NULL)
		return;

	if (frame_active == frame_popup)
		return;

	if (frame_active->flags & COMA_FRAME_ZOOMED) {
		frame_active->w = frame_active->orig_w;
		frame_active->h = frame_active->orig_h;
		frame_active->x = frame_active->orig_x;
		frame_active->y = frame_active->orig_y;
		frame_active->flags &= ~COMA_FRAME_ZOOMED;
	} else {
		frame_active->w = zoom_width;
		frame_active->h = frame_height;
		frame_active->x = frame_offset;
		frame_active->y = frame_y_offset;
		frame_active->flags |= COMA_FRAME_ZOOMED;
	}

	TAILQ_FOREACH(client, &frame_active->clients, list) {
		coma_client_hide(client);
		coma_client_adjust(client);
	}

	coma_client_unhide(frame_active->focus);

	frame_bar_create(frame_active);
	coma_frame_bar_update(frame_active);
}

void
coma_frame_bar_sort(void)
{
	struct frame		*frame;

	TAILQ_FOREACH(frame, &frames, list)
		frame_bar_sort(frame);

	frame_bar_sort(frame_popup);
	coma_frame_bars_update();
}

void
coma_frame_bars_create(void)
{
	struct frame		*frame;

	TAILQ_FOREACH(frame, &frames, list)
		frame_bar_create(frame);

	frame_bar_create(frame_popup);
	XUnmapWindow(dpy, frame_popup->bar);

	coma_frame_bars_update();
}

void
coma_frame_bars_update(void)
{
	struct frame		*frame;

	TAILQ_FOREACH(frame, &frames, list)
		coma_frame_bar_update(frame);

	coma_frame_bar_update(frame_popup);
}

void
coma_frame_bar_update(struct frame *frame)
{
	XGlyphInfo		gi;
	u_int32_t		pos;
	size_t			slen;
	u_int16_t		offset;
	struct client		*client;
	int			len, idx;
	char			buf[64], status[256];
	XftColor		*bar_active, *bar_inactive;
	XftColor		*active, *inactive, *color, *dir;

	/* Can be called before bars are setup. */
	if (frame->bar == None)
		return;

	pos = 1;
	TAILQ_FOREACH_REVERSE(client, &frame->clients, client_list, list) {
		client->pos = pos++;
		if (client->pos != client->prev) {
			coma_wm_property_write(client->window,
			    atom_client_pos, client->pos);
			client->prev = client->pos;
		}
	}

	idx = 0;
	offset = 5;
	buf[0] = '\0';

	dir = coma_wm_color("frame-bar-directory");
	active = coma_wm_color("frame-bar-client-active");
	inactive = coma_wm_color("frame-bar-client-inactive");

	bar_active = coma_wm_color("frame-bar");
	bar_inactive = coma_wm_color("frame-bar-inactive");

	if (frame_active == frame) {
		color = coma_wm_color("client-active");
		XSetWindowBorder(dpy, frame->bar, color->pixel);
		XSetWindowBackground(dpy, frame->bar, bar_active->pixel);
	} else {
		dir = inactive;
		active = inactive;
		color = coma_wm_color("client-inactive");
		XSetWindowBorder(dpy, frame->bar, color->pixel);
		XSetWindowBackground(dpy, frame->bar, bar_inactive->pixel);
	}

	XClearWindow(dpy, frame->bar);

	if (frame == frame_popup) {
		(void)strlcpy(buf, "[popup bar]", sizeof(buf));
		slen = strlen(buf);
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, slen, &gi);
		XftDrawStringUtf8(frame->xft_draw, active, font,
		    offset, 30, (const FcChar8 *)buf, slen);
		offset += gi.width + 4;
	}

	if (frame->focus != NULL)
		client = frame->focus;
	else
		client = NULL;

	if (client != NULL && client->pwd != NULL) {
		if (client->host) {
			len = snprintf(status, sizeof(status), "%s - %s",
			    client->host, client->pwd);
		} else {
			len = snprintf(status, sizeof(status), "%s",
			    client->pwd);
		}

		if (len == -1 || (size_t)len >= sizeof(status))
			(void)strlcpy(buf, "[error]", sizeof(buf));

		XftDrawStringUtf8(frame->xft_draw, dir, font,
		    5, 15, (const FcChar8 *)status, len);
	}

	TAILQ_FOREACH_REVERSE(client, &frame->clients, client_list, list) {
		if (client->tag) {
			len = snprintf(buf, sizeof(buf), "[%s]", client->tag);
		} else if (client->cmd) {
			len = snprintf(buf, sizeof(buf), "[%s]", client->cmd);
		} else if (client->host) {
			len = snprintf(buf, sizeof(buf), "[%s]", client->host);
		} else {
			len = snprintf(buf, sizeof(buf), "[%u]", idx);
		}

		if (len == -1 || (size_t)len >= sizeof(buf))
			(void)strlcpy(buf, "[?]", sizeof(buf));

		idx++;

		if (client == frame->focus)
			color = active;
		else
			color = inactive;

		slen = strlen(buf);
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, slen, &gi);

		XftDrawStringUtf8(frame->xft_draw, color, font,
		    offset, 30, (const FcChar8 *)buf, slen);

		client->fbo = offset;
		client->fbw = gi.width;

		offset += gi.width + 4;
	}
}

void
coma_frame_bar_click(Window bar, u_int16_t offset)
{
	struct frame		*frame;
	struct client		*client;

	frame = NULL;
	TAILQ_FOREACH(frame, &frames, list) {
		if (frame->bar == bar)
			break;
	}

	if (frame == NULL)
		return;

	client = NULL;

	TAILQ_FOREACH(client, &frame->clients, list) {
		if (offset >= client->fbo &&
		    offset <= client->fbo + client->fbw)
			break;
	}

	if (client != NULL) {
		frame->focus = client;
		coma_frame_focus(frame, 0);
		coma_frame_bar_update(frame);
	}
}

void
coma_frame_update_titles(void)
{
	struct frame	*frame;
	struct client	*client;

	TAILQ_FOREACH(frame, &frames, list) {
		TAILQ_FOREACH(client, &frame->clients, list)
			coma_client_update_title(client);
		coma_frame_bar_update(frame);
	}

	TAILQ_FOREACH(client, &frame_popup->clients, list)
		coma_client_update_title(client);

	coma_frame_bar_update(frame_popup);

	if (frame_popup->split != NULL) {
		TAILQ_FOREACH(client, &frame_popup->split->clients, list)
			coma_client_update_title(client);
		coma_frame_bar_update(frame_popup->split);
	}
}

static void
frame_layout_default(void)
{
	struct frame	*frame;
	u_int16_t	i, count, width, offset, x;

	count = 0;

	if (frame_offset != -1)
		width = screen_width - frame_offset;
	else
		width = screen_width;

	if (frame_height == 0) {
		frame_y_offset = frame_gap;
		frame_height = screen_height - (frame_gap * 2) - frame_bar -
		    (frame_border * 2);
	} else {
		frame_y_offset = (screen_height -
		    (frame_bar + frame_height + (frame_border * 2))) / 2;
	}

	while (width > frame_width) {
		if (frame_count != -1 && count == frame_count)
			break;
		count++;
		width -= frame_width;
	}

	if (frame_offset != -1) {
		offset = frame_offset;
	} else {
		offset = width / 2;
	}

	if (offset > (frame_gap * count))
		offset -= frame_gap;

	x = offset;
	zoom_width = 0;

	for (i = 0; i < count; i++) {
		frame = coma_frame_create(frame_width,
		    frame_height, offset, frame_y_offset);
		coma_frame_register(frame);
		offset += frame_width + frame_gap + (frame_border * 2);
		zoom_width += frame_width + frame_gap + (frame_border * 2);
	}

	if (frame_offset != -1) {
		width = screen_width / 2;
		if (frame_offset < width) {
			i = frame_offset;
			width = screen_width;
			offset = frame_offset - (i / 2);
			width -= i;
		} else {
			i = frame_offset - width;
			offset = width + (i / 2);
			width -= i * 2;
		}
	} else {
		offset = frame_gap;
		width = screen_width - (frame_gap * 2) - (frame_border * 2);
	}

	frame_offset = x;
	zoom_width -= frame_gap + (frame_border * 2);

	frame_popup = coma_frame_create(zoom_width,
	    frame_height, x, frame_y_offset);
}

struct frame *
coma_frame_lookup(u_int32_t id)
{
	struct frame	*frame;

	if (frame_popup->id == id)
		return (frame_popup);

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame->id == id)
			return (frame);
	}

	return (NULL);
}

void
coma_frame_focus(struct frame *frame, int warp)
{
	struct frame		*prev;
	struct client		*client;

	prev = frame_active;
	frame_active = frame;

	if ((client = frame->focus) == NULL)
		client = TAILQ_FIRST(&frame->clients);

	if (client != NULL) {
		coma_client_focus(client);
		if (warp)
			coma_client_warp_pointer(client);
	}

	coma_frame_bar_update(prev);
	coma_frame_bar_update(frame_active);
}

struct frame *
coma_frame_create(u_int16_t width, u_int16_t height, u_int16_t x, u_int16_t y)
{
	struct frame		*frame;

	frame = coma_calloc(1, sizeof(*frame));

	frame->bar = None;
	frame->id = frame_id++;

	frame->x = x;
	frame->y = y;
	frame->orig_x = x;
	frame->orig_y = y;

	frame->w = width;
	frame->h = height;
	frame->orig_w = width;
	frame->orig_h = height;

	frame->screen = DefaultScreen(dpy);
	frame->visual = DefaultVisual(dpy, frame->screen);
	frame->colormap = DefaultColormap(dpy, frame->screen);

	TAILQ_INIT(&frame->clients);

	return (frame);
}

void
coma_frame_register(struct frame *frame)
{
	frame->flags = COMA_FRAME_INLIST;
	TAILQ_INSERT_TAIL(&frames, frame, list);
}

static void
frame_layout_small_large(int dual)
{
	struct frame	*frame;
	u_int16_t	offset, width;

	zoom_width = 0;

	if (frame_offset == -1) {
		offset = frame_gap;
		frame_offset = offset;
	} else {
		offset = frame_offset;
	}

	frame_y_offset = frame_gap;
	frame_height = screen_height - (frame_gap * 2) - frame_bar -
	    (frame_border * 2);

	/* Small frame on the left hand-side. */
	frame = coma_frame_create(frame_width, frame_height,
	    offset, frame_y_offset);
	coma_frame_register(frame);
	offset += frame_width + frame_gap + (frame_border * 2);

	/* Rest of the screen covered by large/dual frame(s). */
	if (dual) {
		width = ((screen_width - offset - frame_gap) / 2) -
		    frame_border;
	} else {
		width = screen_width - offset - frame_gap - (frame_border * 2);
	}

	frame = coma_frame_create(width, frame_height, offset, frame_y_offset);
	coma_frame_register(frame);

	if (dual) {
		offset += width;
		frame = coma_frame_create(width, frame_height, offset,
		    frame_y_offset);
		coma_frame_register(frame);
	}

	/* Popup covers entire screen. */
	frame_popup = coma_frame_create(screen_width - (frame_border * 2) -
	    (frame_gap * 2), frame_height, frame_gap, frame_y_offset);

	zoom_width = screen_width - (frame_gap * 2);
}

static void
frame_bar_create(struct frame *frame)
{
	XftColor	*color;
	u_int16_t	y_offset;

	if (frame->bar != None) {
		XDestroyWindow(dpy, frame->bar);
		XftDrawDestroy(frame->xft_draw);
	}

	y_offset = frame->y + frame->h + (frame_border * 2);
	color = coma_wm_color("frame-bar");

	frame->bar = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
	    frame->x, y_offset + (frame_gap / 2), frame->w,
	    frame_bar, 0, WhitePixel(dpy, frame->screen), color->pixel);

	XSetWindowBorderWidth(dpy, frame->bar, frame_border);

	XSelectInput(dpy, frame->bar, ButtonReleaseMask);

	if ((frame->xft_draw = XftDrawCreate(dpy,
	    frame->bar, frame->visual, frame->colormap)) == NULL)
		fatal("XftDrawCreate failed");

	XMapWindow(dpy, frame->bar);
}

static void
frame_bar_sort(struct frame *frame)
{
	struct client	*c1, *c2, *next;

	for (c1 = TAILQ_FIRST(&frame->clients); c1 != NULL; c1 = next) {
		next = TAILQ_NEXT(c1, list);
		TAILQ_FOREACH(c2, &frame->clients, list) {
			if (c1->pos < c2->pos) {
				TAILQ_REMOVE(&frame->clients, c1, list);
				TAILQ_INSERT_AFTER(&frame->clients,
				    c2, c1, list);
				break;
			}
		}
	}
}

static struct frame *
frame_find_left(void)
{
	struct frame	*frame, *candidate;

	candidate = NULL;

	TAILQ_FOREACH_REVERSE(frame, &frames, frame_list, list) {
		if (frame->x < frame_active->x) {
			if (candidate == NULL)
				candidate = frame;
			if (frame->y == frame_active->y)
				return (frame);
		}
	}

	return (candidate);
}

static struct frame *
frame_find_right(void)
{
	struct frame	*frame, *candidate;

	candidate = NULL;

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame->x > frame_active->x) {
			if (candidate == NULL)
				candidate = frame;
			if (frame->y == frame_active->y)
				return (frame);
		}
	}

	return (candidate);
}

static void
frame_client_move(int which)
{
	struct client	*client;
	struct frame	*other, *prev;

	if (!(frame_active->flags & COMA_FRAME_INLIST))
		return;

	if (TAILQ_EMPTY(&frame_active->clients))
		return;

	prev = frame_active;

	switch (which) {
	case CLIENT_MOVE_LEFT:
		other = frame_find_left();
		break;
	case CLIENT_MOVE_RIGHT:
		other = frame_find_right();
		break;
	default:
		other = NULL;
		break;
	}

	if (other == NULL)
		return;

	client = frame_active->focus;
	frame_active->focus = TAILQ_NEXT(client, list);

	if (frame_active->focus == NULL)
		frame_active->focus = TAILQ_FIRST(&frame_active->clients);

	if (frame_active->focus)
		coma_client_focus(frame_active->focus);

	TAILQ_REMOVE(&frame_active->clients, client, list);
	TAILQ_INSERT_HEAD(&other->clients, client, list);

	client->frame = other;
	client->x = other->x;

	coma_client_adjust(client);

	frame_active = other;
	coma_client_focus(client);
	coma_client_warp_pointer(client);

	coma_frame_bar_update(prev);
	coma_frame_bar_update(frame_active);
}
