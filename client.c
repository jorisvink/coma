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

static struct client	*client_active = NULL;

void
coma_client_create(Window window)
{
	XWindowAttributes	attr;
	struct client		*client;

	XGetWindowAttributes(dpy, window, &attr);

	client = coma_calloc(1, sizeof(*client));
	TAILQ_INSERT_HEAD(&frame_active->clients, client, list);

	if (client_active == NULL)
		client_active = client;

	client->x = attr.x;
	client->y = attr.y;
	client->w = attr.width;
	client->h = attr.height;
	client->window = window;
	client->frame = frame_active;
	client->bw = 1;

	printf("client_create %ld: %dx%d (%dx%d)\n", client->window,
	    client->w, client->h, client->x, client->y);

	XSelectInput(dpy, client->window,
	    StructureNotifyMask | PropertyChangeMask | FocusChangeMask);

	XAddToSaveSet(dpy, client->window);
	XSetWindowBorderWidth(dpy, client->window, client->bw);

	coma_wm_register_prefix(client->window);
	coma_client_adjust(client);
	coma_client_map(client);

	XSync(dpy, False);
}

struct client *
coma_client_find(Window window)
{
	return (coma_frame_find_client(window));
}

void
coma_client_destroy(struct client *client)
{
	struct client		*next;
	struct frame		*frame;
	int			was_active;

	frame = client->frame;

	if (client == client_active) {
		was_active = 1;
		client_active = NULL;
	} else {
		was_active = 0;
	}

	printf("client_destroy: %ld (%d)\n", client->window, was_active);
	TAILQ_REMOVE(&frame->clients, client, list);
	free(client);

	if (was_active == 0)
		return;

	if (TAILQ_EMPTY(&frame->clients)) {
		coma_frame_select_any();
	} else {
		next = TAILQ_FIRST(&frame->clients);
		printf("\tnext: %ld\n", next != NULL ? next->window : 0);
		if (next != NULL)
			coma_client_focus(next);
	}
}

void
coma_client_adjust(struct client *client)
{
	client->y = COMA_FRAME_GAP;
	client->x = client->frame->offset;
	client->h = screen_height - (COMA_FRAME_GAP * 2);

	coma_client_send_configure(client);
}

void
coma_client_map(struct client *client)
{
	XMapWindow(dpy, client->window);
	coma_client_focus(client);
}

void
coma_client_focus(struct client *client)
{
	XftColor	*color;

	XRaiseWindow(dpy, client->window);
	XSetInputFocus(dpy, client->window, RevertToPointerRoot, CurrentTime);

	color = coma_wm_xftcolor(COMA_WM_COLOR_WIN_ACTIVE);
	XSetWindowBorder(dpy, client->window, color->pixel);

	if (client_active != NULL && client_active != client) {
		color = coma_wm_xftcolor(COMA_WM_COLOR_WIN_INACTIVE);
		XSetWindowBorder(dpy, client_active->window, color->pixel);
	}

	client_active = client;
	frame_active->focus = client;

	XSync(dpy, False);
}

void
coma_client_send_configure(struct client *client)
{
	XConfigureEvent		cfg;

	memset(&cfg, 0, sizeof(cfg));

	cfg.type = ConfigureNotify;
	cfg.event = client->window;
	cfg.window = client->window;

	cfg.x = client->x;
	cfg.y = client->y;
	cfg.width = client->w;
	cfg.height = client->h;
	cfg.border_width = client->bw;

	printf("client_configure %ld: %dx%d (%dx%d)\n", client->window,
	    client->w, client->h, client->x, client->y);

	XMoveResizeWindow(dpy, client->window,
	    client->x, client->y, client->w, client->h);

	XSendEvent(dpy, client->window, False,
	    StructureNotifyMask, (XEvent *)&cfg);
}
