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

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "coma.h"

#define CLIENT_MOVE_LEFT		1
#define CLIENT_MOVE_RIGHT		2

static void		frame_focus(struct frame *);
static void		frame_bar_create(struct frame *);
static struct frame	*frame_create(u_int16_t,
			    u_int16_t, u_int16_t, u_int16_t);

static void		frame_client_move(int);
static struct frame	*frame_find_left(void);
static struct frame	*frame_find_right(void);

static struct frame_list	frames;
static u_int16_t		zoom_width = 0;

int				frame_count = -1;
u_int16_t			frame_offset = 0;
u_int16_t			frame_height = 0;
struct frame			*frame_popup = NULL;
struct frame			*frame_active = NULL;
u_int16_t			frame_gap = COMA_FRAME_GAP;
u_int16_t			frame_bar = COMA_FRAME_BAR;
u_int16_t			frame_width = COMA_FRAME_WIDTH;

void
coma_frame_setup(void)
{
	struct frame	*frame;
	u_int16_t	i, count, width, offset, x;

	TAILQ_INIT(&frames);

	count = 0;
	width = screen_width - frame_offset;
	frame_height = screen_height - (frame_gap * 2) - frame_bar;

	while (width > frame_width) {
		if (frame_count != -1 && count == frame_count)
			break;
		count++;
		width -= frame_width;
	}

	if (frame_offset != 0) {
		offset = frame_offset;
	} else {
		offset = width / 2;
	}

	if (offset > (frame_gap * count))
		offset -= frame_gap * count;

	x = offset;
	zoom_width = 0;

	for (i = 0; i < count; i++) {
		frame = frame_create(frame_width,
		    frame_height, offset, frame_gap);
		frame->flags = COMA_FRAME_INLIST;
		TAILQ_INSERT_TAIL(&frames, frame, list);
		offset += frame_width + frame_gap;
		zoom_width += frame_width + frame_gap;
	}

	if (frame_offset != 0) {
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
		width = screen_width - (frame_gap * 2);
	}

	frame_popup = frame_create(width, frame_height, offset, frame_gap);

	frame_offset = x;
	zoom_width -= frame_gap;

	frame_active = TAILQ_FIRST(&frames);
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
coma_frame_popup(void)
{
	struct client	*client, *focus;

	if (frame_active == frame_popup) {
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
	} else {
		if (frame_active->flags & COMA_FRAME_ZOOMED)
			return;

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
}

void
coma_frame_next(void)
{
	struct frame	*next;

	if (!(frame_active->flags & COMA_FRAME_INLIST) ||
	    frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	if ((next = frame_find_right()) != NULL)
		frame_focus(next);
}

void
coma_frame_prev(void)
{
	struct frame	*prev;

	if (!(frame_active->flags & COMA_FRAME_INLIST) ||
	    frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	if ((prev = frame_find_left()) != NULL)
		frame_focus(prev);
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
	u_int16_t	height, y;

	if (frame_active == frame_popup)
		return;

	if (frame_active->split != NULL)
		return;

	height = frame_active->h / 2 - frame_gap;
	y = (frame_active->h / 2) + frame_bar;

	frame = frame_create(frame_active->w, height, frame_active->x, y);

	if (frame_active->flags & COMA_FRAME_INLIST)
		TAILQ_INSERT_TAIL(&frames, frame, list);

	frame->split = frame_active;
	frame->flags = frame_active->flags;

	frame_active->split = frame;
	frame_active->h = height - frame_gap;

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
	survives->h = screen_height - (frame_gap * 2) - frame_bar;
	survives->orig_h = survives->h;

	frame_active = survives;

	TAILQ_FOREACH(client, &frame_active->clients, list)
		coma_client_adjust(client);

	if (focus != NULL)
		coma_client_focus(focus);

	frame_bar_create(frame_active);
	coma_frame_bar_update(frame_active);
}

void
coma_frame_split_next(void)
{
	if (frame_active->split == NULL)
		return;

	frame_focus(frame_active->split);
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

	frame_focus(frame);
}

void
coma_frame_mouseover(u_int16_t x, u_int16_t y)
{
	struct frame		*frame;
	struct client		*client, *prev;

	if (frame_active == frame_popup)
		return;

	if (frame_active->flags & COMA_FRAME_ZOOMED)
		return;

	frame = NULL;
	client = NULL;
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
}

struct client *
coma_frame_find_client(Window window)
{
	struct frame	*frame;
	struct client	*client;

	TAILQ_FOREACH(frame, &frames, list) {
		TAILQ_FOREACH(client, &frame->clients, list) {
			if (client->window == window)
				return (client);
		}
	}

	TAILQ_FOREACH(client, &frame_popup->clients, list) {
		if (client->window == window)
			return (client);
	}

	if (frame_popup->split != NULL) {
		TAILQ_FOREACH(client, &frame_popup->split->clients, list) {
			if (client->window == window)
				return (client);
		}
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
		frame_active->y = frame_gap;
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
	size_t			slen;
	u_int16_t		offset;
	char			buf[32];
	struct client		*client;
	int			len, idx;
	XftColor		*active, *inactive, *color;

	/* Can be called before bars are setup. */
	if (frame->bar == None)
		return;

	idx = 0;
	offset = 5;
	buf[0] = '\0';

	active = coma_wm_color("frame-bar-client-active");
	inactive = coma_wm_color("frame-bar-client-inactive");

	XClearWindow(dpy, frame->bar);

	if (frame == frame_popup) {
		(void)strlcpy(buf, "[popup bar]", sizeof(buf));
		slen = strlen(buf);
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, slen, &gi);
		XftDrawStringUtf8(frame->xft_draw, active, font,
		    offset, 15, (const FcChar8 *)buf, slen);
		offset += gi.width + 4;
	}

	TAILQ_FOREACH_REVERSE(client, &frame->clients, client_list, list) {
		len = snprintf(buf, sizeof(buf), "[%u]", idx);
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
		    offset, 15, (const FcChar8 *)buf, slen);

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
		frame_focus(frame);
		coma_frame_bar_update(frame);
	}
}

static void
frame_focus(struct frame *frame)
{
	struct client		*client;

	frame_active = frame;

	if ((client = frame->focus) == NULL)
		client = TAILQ_FIRST(&frame->clients);

	if (client != NULL)
		coma_client_focus(client);
}

static struct frame *
frame_create(u_int16_t width, u_int16_t height, u_int16_t x, u_int16_t y)
{
	struct frame		*frame;

	frame = coma_calloc(1, sizeof(*frame));

	frame->bar = None;

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

static void
frame_bar_create(struct frame *frame)
{
	XftColor	*color;
	u_int16_t	y_offset;

	if (frame->bar != None) {
		XDestroyWindow(dpy, frame->bar);
		XftDrawDestroy(frame->xft_draw);
	}

	y_offset = frame->y + frame->h;
	color = coma_wm_color("frame-bar");

	frame->bar = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
	    frame->x, y_offset, frame->w + 2,
	    frame_bar, 0, WhitePixel(dpy, frame->screen), color->pixel);

	XSelectInput(dpy, frame->bar, ButtonReleaseMask);

	if ((frame->xft_draw = XftDrawCreate(dpy,
	    frame->bar, frame->visual, frame->colormap)) == NULL)
		fatal("XftDrawCreate failed");

	XMapWindow(dpy, frame->bar);
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
	struct frame	*other, *prev;
	struct client	*c1, *c2, *n1, *n2;

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

	c1 = frame_active->focus;

	if (other->focus != NULL)
		c2 = other->focus;
	else
		c2 = TAILQ_FIRST(&other->clients);

	n1 = TAILQ_NEXT(c1, list);
	if (c2 != NULL)
		n2 = TAILQ_NEXT(c2, list);
	else
		n2 = NULL;

	frame_active->focus = NULL;
	TAILQ_REMOVE(&frame_active->clients, c1, list);

	if (c2 != NULL)
		TAILQ_REMOVE(&other->clients, c2, list);

	c1->frame = other;
	c1->x = other->x;
	if (n2 != NULL)
		TAILQ_INSERT_BEFORE(n2, c1, list);
	else
		TAILQ_INSERT_HEAD(&other->clients, c1, list);

	if (c2 != NULL) {
		c2->frame = frame_active;
		c2->x = frame_active->x;

		if (n1 != NULL)
			TAILQ_INSERT_BEFORE(n1, c2, list);
		else
			TAILQ_INSERT_HEAD(&frame_active->clients, c2, list);
	}

	coma_client_adjust(c1);

	if (c2 != NULL) {
		c2->frame->focus = c2;
		coma_client_adjust(c2);
		XRaiseWindow(dpy, c2->window);
	}

	frame_active = other;
	coma_client_focus(c1);

	coma_frame_bar_update(prev);
	coma_frame_bar_update(frame_active);
}
