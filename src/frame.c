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

#include "coma.h"

#define FRAME_POPUP_ID	0xffff

static struct frame	*frame_create(u_int16_t, u_int16_t, u_int16_t);

static struct frame_list	frames;

struct frame			*frame_popup = NULL;
struct frame			*frame_active = NULL;
u_int16_t			frame_width = COMA_FRAME_WIDTH_DEFAULT;

void
coma_frame_setup(void)
{
	struct frame	*frame;
	u_int16_t	i, count, width, offset;

	TAILQ_INIT(&frames);

	count = 0;
	width = screen_width;

	while (width > frame_width) {
		count++;
		width -= frame_width;
	}

	offset = width / 2;
	if (offset > (COMA_FRAME_GAP * count))
		offset -= COMA_FRAME_GAP * count;

	for (i = 0; i < count; i++) {
		frame = frame_create(i, frame_width, offset);
		TAILQ_INSERT_TAIL(&frames, frame, list);
		offset += frame_width + COMA_FRAME_GAP;
	}

	frame_active = TAILQ_FIRST(&frames);

	width = screen_width - (COMA_FRAME_GAP * 2);
	frame_popup = frame_create(FRAME_POPUP_ID, width, COMA_FRAME_GAP);
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
}

void
coma_frame_popup(void)
{
	struct client	*client, *focus;

	if (frame_active == frame_popup) {
		TAILQ_FOREACH(client, &frame_popup->clients, list)
			coma_client_hide(client);
		coma_frame_select_any();
	} else {
		focus = frame_popup->focus;
		frame_active = frame_popup;

		TAILQ_FOREACH(client, &frame_popup->clients, list)
			coma_client_unhide(client);

		if (focus != NULL)
			coma_client_focus(focus);
	}
}

void
coma_frame_next(void)
{
	struct frame	*next;
	struct client	*client;

	if (frame_active == frame_popup)
		return;

	next = TAILQ_NEXT(frame_active, list);
	if (next != NULL) {
		frame_active = next;

		if (frame_active->focus == NULL)
			client = TAILQ_FIRST(&frame_active->clients);
		else
			client = frame_active->focus;

		if (client != NULL)
			coma_client_focus(client);
	}
}

void
coma_frame_prev(void)
{
	struct frame	*prev;
	struct client	*client;

	if (frame_active == frame_popup)
		return;

	prev = TAILQ_PREV(frame_active, frame_list, list);
	if (prev != NULL) {
		frame_active = prev;

		if (frame_active->focus == NULL)
			client = TAILQ_FIRST(&frame_active->clients);
		else
			client = frame_active->focus;

		if (client != NULL)
			coma_client_focus(client);
	}
}

void
coma_frame_client_next(void)
{
	struct client	*next;

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

	if ((prev = TAILQ_NEXT(frame_active->focus, list)) == NULL)
		prev = TAILQ_FIRST(&frame_active->clients);

	if (prev != NULL) {
		coma_client_focus(prev);
		coma_frame_bar_update(frame_active);
	}
}

void
coma_frame_client_move(int which)
{
	struct frame	*other;
	struct client	*c1, *c2, *n1, *n2;

	if (frame_active == frame_popup)
		return;

	if (TAILQ_EMPTY(&frame_active->clients))
		return;

	switch (which) {
	case COMA_CLIENT_MOVE_LEFT:
		other = TAILQ_PREV(frame_active, frame_list, list);
		break;
	case COMA_CLIENT_MOVE_RIGHT:
		other = TAILQ_NEXT(frame_active, list);
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
	c1->x = other->offset;
	if (n2 != NULL)
		TAILQ_INSERT_BEFORE(n2, c1, list);
	else
		TAILQ_INSERT_HEAD(&other->clients, c1, list);

	if (c2 != NULL) {
		c2->frame = frame_active;
		c2->x = frame_active->offset;

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
		coma_frame_bar_update(c2->frame);
	}

	frame_active = other;
	coma_client_focus(c1);
	coma_frame_bar_update(c1->frame);
}

void
coma_frame_select_any(void)
{
	struct frame	*frame;
	struct client	*client;

	frame = NULL;

	TAILQ_FOREACH(frame, &frames, list) {
		if (TAILQ_EMPTY(&frame->clients))
			continue;
		break;
	}

	if (frame == NULL)
		frame = TAILQ_FIRST(&frames);

	frame_active = frame;

	if (frame->focus == NULL)
		client = TAILQ_FIRST(&frame_active->clients);
	else
		client = frame->focus;

	if (client != NULL)
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

	return (NULL);
}

void
coma_frame_bars_create(void)
{
	Window			root;
	XftColor		*color;
	struct frame		*frame;
	int			screen;
	Visual			*visual;
	Colormap		colormap;
	u_int16_t		y_offset;

	screen = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);

	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);

	color = coma_wm_xftcolor(COMA_WM_COLOR_FRAME_BAR);
	y_offset = screen_height - COMA_FRAME_GAP - COMA_FRAME_BAR;

	TAILQ_FOREACH(frame, &frames, list) {
		frame->bar = XCreateSimpleWindow(dpy, root,
		    frame->offset, y_offset, frame->width + 2,
		    COMA_FRAME_BAR, 0, WhitePixel(dpy, screen), color->pixel);

		if ((frame->xft_draw = XftDrawCreate(dpy,
		    frame->bar, visual, colormap)) == NULL)
			fatal("XftDrawCreate failed");

		XMapWindow(dpy, frame->bar);
	}

	coma_frame_bars_update();
}

void
coma_frame_bars_update(void)
{
	struct frame		*frame;

	TAILQ_FOREACH(frame, &frames, list)
		coma_frame_bar_update(frame);
}

void
coma_frame_bar_update(struct frame *frame)
{
	XGlyphInfo		gi;
	size_t			slen;
	char			buf[6];
	u_int16_t		offset;
	struct client		*client;
	int			len, idx;
	XftColor		*active, *inactive, *color;

	/* Can be called before bars are setup. */
	if (frame->bar == None)
		return;

	idx = 0;
	offset = 5;
	buf[0] = '\0';

	active = coma_wm_xftcolor(COMA_WM_COLOR_FRAME_BAR_ACTIVE);
	inactive = coma_wm_xftcolor(COMA_WM_COLOR_FRAME_BAR_INACTIVE);

	XClearWindow(dpy, frame->bar);

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
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, len, &gi);

		XftDrawStringUtf8(frame->xft_draw, color, font,
		    offset, 15, (const FcChar8 *)buf, strlen(buf));

		offset += gi.width + 4;
	}
}

static struct frame *
frame_create(u_int16_t id, u_int16_t width, u_int16_t offset)
{
	struct frame		*frame;

	frame = coma_calloc(1, sizeof(*frame));

	frame->id = id;
	frame->bar = None;
	frame->width = width;
	frame->offset = offset;

	TAILQ_INIT(&frame->clients);

	return (frame);
}
