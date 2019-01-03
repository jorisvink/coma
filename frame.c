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

static void	frame_create(u_int16_t, u_int16_t);

static struct frame_list	frames;
struct frame			*frame_active = NULL;

void
coma_frame_setup(void)
{
	u_int16_t	i, count, width, offset;

	TAILQ_INIT(&frames);

	count = 0;
	width = screen_width;

	while (width > COMA_FRAME_WIDTH) {
		count++;
		width -= COMA_FRAME_WIDTH;
	}

	offset = width / 2;
	if (offset > (COMA_FRAME_GAP * count))
		offset -= COMA_FRAME_GAP * count;

	printf("screen fits %u frames (start:%u)\n", count, offset);

	for (i = 0; i < count; i++) {
		frame_create(i, offset);
		offset += COMA_FRAME_WIDTH + COMA_FRAME_GAP;
	}

	frame_active = TAILQ_FIRST(&frames);
}

void
coma_frame_next(void)
{
	struct frame	*next;
	struct client	*client;

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

	if (frame != NULL) {
		frame_active = frame;
		client = TAILQ_FIRST(&frame_active->clients);
		coma_client_focus(client);
	}
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

	return (NULL);
}

static void
frame_create(u_int16_t id, u_int16_t offset)
{
	struct frame		*frame;

	frame = coma_calloc(1, sizeof(*frame));

	frame->id = id;
	frame->offset = offset;

	TAILQ_INIT(&frame->clients);
	TAILQ_INSERT_TAIL(&frames, frame, list);
}
