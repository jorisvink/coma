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

struct client_list	clients;
static u_int32_t	client_id = 1;
struct client		*client_active = NULL;

void
coma_client_init(void)
{
	TAILQ_INIT(&clients);
}

void
coma_client_create(Window window)
{
	XWindowAttributes	attr;
	struct frame		*frame;
	struct client		*client;
	u_int32_t		frame_id, pos, visible;

	XGetWindowAttributes(dpy, window, &attr);

	if (coma_wm_property_read(window, atom_frame_id, &frame_id) == -1) {
		frame_id = 0;
		frame = frame_active;
	} else {
		if ((frame = coma_frame_lookup(frame_id)) == NULL)
			frame = frame_active;
	}

	if (coma_wm_property_read(window, atom_client_visible, &visible) == -1)
		visible = 0;

	if (client_discovery == 0)
		visible = 1;

	coma_log("window 0x%08x - visible=%d - frame:%u",
	    window, visible, frame_id);

	client = coma_calloc(1, sizeof(*client));
	TAILQ_INSERT_TAIL(&clients, client, glist);

	if (coma_wm_property_read(window, atom_client_pos, &pos) == 0)
		client->pos = pos;

	if (frame->focus != NULL) {
		TAILQ_INSERT_BEFORE(frame->focus, client, list);
	} else {
		TAILQ_INSERT_HEAD(&frame->clients, client, list);
	}

	if (frame == frame_popup && frame != frame_active)
		visible = 0;

	if (client_active == NULL)
		client_active = client;

	client->x = attr.x;
	client->y = attr.y;
	client->w = attr.width;
	client->h = attr.height;

	client->frame = frame;
	client->window = window;
	client->id = client_id++;
	client->bw = frame_border;

	coma_client_update_title(client);

	XSelectInput(dpy, client->window,
	    StructureNotifyMask | PropertyChangeMask | FocusChangeMask);

	XAddToSaveSet(dpy, client->window);
	XSetWindowBorderWidth(dpy, client->window, client->bw);

	coma_wm_register_prefix(client->window);
	coma_client_adjust(client);

	if (visible) {
		coma_client_map(client);
		coma_client_warp_pointer(client);
	} else {
		coma_client_hide(client);
	}

	if (client_discovery == 0) {
		coma_frame_bar_update(frame);
		XSync(dpy, False);
	}
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

	next = TAILQ_NEXT(client, list);
	TAILQ_REMOVE(&clients, client, glist);
	TAILQ_REMOVE(&frame->clients, client, list);

	if (client->status)
		free(client->status);

	free(client);

	coma_frame_bar_update(frame);

	if (was_active == 0)
		return;

	if (frame_active == frame_popup) {
		if (TAILQ_EMPTY(&frame_popup->clients)) {
			coma_frame_popup_toggle();
			return;
		}
	}

	if (next == NULL) {
		if ((next = TAILQ_FIRST(&frame->clients)) == NULL) {
			if (frame->split != NULL)
				coma_frame_merge();
		}
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
	client->w = client->frame->w;
	client->h = client->frame->h;
	client->x = client->frame->x;
	client->y = client->frame->y;

	coma_client_send_configure(client);

	coma_wm_property_write(client->window,
	    atom_frame_id, client->frame->id);
}

void
coma_client_map(struct client *client)
{
	XMapWindow(dpy, client->window);
	coma_client_focus(client);
	coma_wm_property_write(client->window, atom_client_visible, 1);
}

void
coma_client_hide(struct client *client)
{
	if (!(client->flags & COMA_CLIENT_HIDDEN)) {
		client->flags |= COMA_CLIENT_HIDDEN;
		XUnmapWindow(dpy, client->window);
		coma_wm_property_write(client->window, atom_client_visible, 0);
	}
}

void
coma_client_unhide(struct client *client)
{
	if (client->flags & COMA_CLIENT_HIDDEN) {
		client->flags &= ~COMA_CLIENT_HIDDEN;
		coma_client_map(client);
	}
}

void
coma_client_warp_pointer(struct client *client)
{
	XWarpPointer(dpy, None, client->window, 0, 0, 0, 0,
	    client->w / 2, client->h / 2);
}

void
coma_client_focus(struct client *client)
{
	XftColor	*color;

	if (client->flags & COMA_CLIENT_HIDDEN) {
		XMapWindow(dpy, client->window);
		client->flags &= ~COMA_CLIENT_HIDDEN;
	}

	XRaiseWindow(dpy, client->window);
	XSetInputFocus(dpy, client->window, RevertToPointerRoot, CurrentTime);

	color = coma_wm_color("client-active");
	XSetWindowBorder(dpy, client->window, color->pixel);

	if (client_active != NULL && client_active->id != client->id) {
		color = coma_wm_color("client-inactive");
		XSetWindowBorder(dpy, client_active->window, color->pixel);
	}

	client_active = client;
	client->frame->focus = client;

	if (client_discovery == 0) {
		coma_frame_bar_update(client->frame);
		coma_wm_property_write(DefaultRootWindow(dpy),
		    atom_client_act, client->window);
		XSync(dpy, True);
	}
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

void
coma_client_update_title(struct client *client)
{
	int		n, len;
	char		*name, *args[4], pwd[PATH_MAX];

	if (!XFetchName(dpy, client->window, &name))
		return;

	free(client->pwd);
	free(client->status);

	client->pwd = NULL;
	client->cmd = NULL;
	client->host = NULL;

	if ((client->status = strdup(name)) == NULL)
		fatal("strdup");

	if ((n = coma_split_string(client->status, ";", args, 4)) < 2) {
		client->cmd = args[0];
		return;
	}

	if (args[1] != NULL && !strncmp(args[1], homedir, strlen(homedir))) {
		len = snprintf(pwd, sizeof(pwd), "~%s",
		    args[1] + strlen(homedir));
		if (len != -1 && (size_t)len < sizeof(pwd))
			client->pwd = strdup(pwd);
	}

	if (client->pwd == NULL) {
		if ((client->pwd = strdup(args[1])) == NULL)
			fatal("strdup failed");
	}

	client->host = args[0];

	if (n == 3)
		client->cmd = args[2];

	XFree(name);
}
