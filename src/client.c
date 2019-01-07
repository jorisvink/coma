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

static u_int32_t	client_id = 0;
struct client		*client_active = NULL;

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

	client->bw = 1;
	client->x = attr.x;
	client->y = attr.y;
	client->w = attr.width;
	client->h = attr.height;
	client->window = window;
	client->id = client_id++;
	client->frame = frame_active;

	XSelectInput(dpy, client->window,
	    StructureNotifyMask | PropertyChangeMask | FocusChangeMask);

	XAddToSaveSet(dpy, client->window);
	XSetWindowBorderWidth(dpy, client->window, client->bw);

	coma_wm_register_prefix(client->window);
	coma_client_adjust(client);
	coma_client_map(client);
	coma_frame_bar_update(frame_active);

	XSync(dpy, False);
}

void
coma_client_kill_active(void)
{
	if (client_active == NULL)
		return;

	XKillClient(dpy, client_active->window);
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

	if (client_active != NULL && client_active->id == client->id) {
		was_active = 1;
		client_active = NULL;
	} else {
		was_active = 0;
	}

	if (frame->focus != NULL && frame->focus->id == client->id)
		frame->focus = NULL;

	TAILQ_REMOVE(&frame->clients, client, list);
	free(client);

	coma_frame_bar_update(frame);

	if (was_active == 0)
		return;

	if (frame_active == frame_popup) {
		coma_frame_popup();
		return;
	}

	if ((next = TAILQ_FIRST(&frame->clients)) == NULL) {
		if (frame->split != NULL)
			coma_frame_merge();
	}

	if (next == NULL) {
		coma_frame_select_any();
	} else {
		coma_client_focus(next);
		coma_frame_bar_update(frame);
	}
}

void
coma_client_adjust(struct client *client)
{
	client->w = client->frame->width;
	client->h = client->frame->height;
	client->x = client->frame->x_offset;
	client->y = client->frame->y_offset;

	coma_client_send_configure(client);
}

void
coma_client_map(struct client *client)
{
	XMapWindow(dpy, client->window);
	coma_client_focus(client);
}

void
coma_client_hide(struct client *client)
{
	XUnmapWindow(dpy, client->window);
}

void
coma_client_unhide(struct client *client)
{
	coma_client_map(client);
}

void
coma_client_focus(struct client *client)
{
	XftColor	*color;

	XRaiseWindow(dpy, client->window);
	XSetInputFocus(dpy, client->window, RevertToPointerRoot, CurrentTime);

	color = coma_wm_xftcolor(COMA_WM_COLOR_WIN_ACTIVE);
	XSetWindowBorder(dpy, client->window, color->pixel);

	if (client_active != NULL && client_active->id != client->id) {
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

	XMoveResizeWindow(dpy, client->window,
	    client->x, client->y, client->w, client->h);

	XSendEvent(dpy, client->window, False,
	    StructureNotifyMask, (XEvent *)&cfg);
}
