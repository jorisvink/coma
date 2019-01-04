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

	if (next != NULL)
		coma_client_focus(next);
}

void
coma_frame_client_prev(void)
{
	struct client	*prev;

	if ((prev = TAILQ_NEXT(frame_active->focus, list)) == NULL)
		prev = TAILQ_FIRST(&frame_active->clients);

	if (prev != NULL)
		coma_client_focus(prev);
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

static struct frame *
frame_create(u_int16_t id, u_int16_t width, u_int16_t offset)
{
	struct frame		*frame;

	frame = coma_calloc(1, sizeof(*frame));

	frame->id = id;
	frame->width = width;
	frame->offset = offset;

	TAILQ_INIT(&frame->clients);

	return (frame);
}
