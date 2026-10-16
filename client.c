/*
 * Copyright (c) 2019-2026 Joris Vink <joris@sanctorum.se>
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

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "coma.h"

static struct client	*client_alloc(struct frame *);
static void		client_attach(struct client *);

struct client_list	clients;
static u_int32_t	client_id = 1;
struct client		*client_active = NULL;

void
coma_client_init(void)
{
	TAILQ_INIT(&clients);
}

void
coma_client_spawn(char **argv, int secure)
{
	const char	*pwd;
	struct client	*client;
	char		path[PATH_MAX];

	pwd = NULL;

#if !defined(__linux__)
	secure = 0;
	coma_log("secure terminal not available on this platform");
#endif

	if (client_active != NULL &&
	    coma_term_cwd(client_active->term, path, sizeof(path)) == 0)
		pwd = path;

	client = client_alloc(frame_active);
	client->secure = secure;

	if ((client->term = coma_term_spawn(argv,
	    pwd, client, secure)) == NULL) {
		TAILQ_REMOVE(&clients, client, glist);
		TAILQ_REMOVE(&client->frame->clients, client, list);
		free(client);
		return;
	}

	client_attach(client);
}

void
coma_client_cleanup(void)
{
	struct client	*client;

	while ((client = TAILQ_FIRST(&clients)) != NULL) {
		TAILQ_REMOVE(&clients, client, glist);
		TAILQ_REMOVE(&client->frame->clients, client, list);
		coma_term_free(client->term);
		free(client->tag);
		free(client->pwd);
		free(client->title);
		free(client->status);
		free(client);
	}

	client_active = NULL;
}

void
coma_client_kill_active(void)
{
	if (client_active == NULL)
		return;

	coma_client_destroy(client_active);
}

void
coma_client_exited(struct client *client)
{
	coma_log("client %u (pid %d) exited", client->id,
	    coma_term_pid(client->term));

	coma_client_destroy(client);
}

void
coma_client_reap(pid_t pid)
{
	struct client	*client;

	TAILQ_FOREACH(client, &clients, glist) {
		if (coma_term_pid(client->term) == pid) {
			coma_client_exited(client);
			return;
		}
	}
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

	coma_wm_term_gone(client->term);
	coma_term_free(client->term);
	coma_wm_io_update();

	free(client->tag);
	free(client->pwd);
	free(client->title);
	free(client->status);
	free(client);

	coma_frame_bar_update(frame);

	if (was_active == 0) {
		if (frame->focus == NULL) {
			if (next == NULL)
				next = TAILQ_FIRST(&frame->clients);
			frame->focus = next;
		}
		return;
	}

	if (next == NULL) {
		if ((next = TAILQ_FIRST(&frame->clients)) == NULL &&
		    frame->split != NULL) {
			frame_active = frame;
			coma_frame_merge();
			frame = frame_active;
			if ((next = frame->focus) == NULL)
				next = TAILQ_FIRST(&frame->clients);
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

	coma_term_resize(client->term, client->w, client->h);
}

void
coma_client_warp_pointer(struct client *client)
{
	coma_wm_warp(client->x + client->bw + (client->w / 2),
	    client->y + client->bw + (client->h / 2));
}

void
coma_client_focus(struct client *client)
{
	if (client_active != NULL && client_active->id != client->id)
		coma_term_focus(client_active->term, 0);

	coma_term_focus(client->term, coma_wm_focused());

	client_active = client;
	client->frame->focus = client;

	coma_frame_bar_update(client->frame);
	coma_wm_dirty();
}

void
coma_client_update_title(struct client *client, const char *title)
{
	size_t		hlen;
	int		n, len;
	char		*args[4], pwd[PATH_MAX];

	free(client->pwd);
	free(client->title);
	free(client->status);

	client->pwd = NULL;
	client->cmd = NULL;
	client->host = NULL;

	if ((client->title = strdup(title)) == NULL)
		fatal("strdup");

	if ((client->status = strdup(title)) == NULL)
		fatal("strdup");

	if ((n = coma_split_string(client->status, ";", args, 4)) < 2) {
		client->cmd = args[0];
		coma_frame_bar_update(client->frame);
		return;
	}

	hlen = strlen(homedir);

	if (args[1] != NULL && !strncmp(args[1], homedir, hlen) &&
	    (args[1][hlen] == '/' || args[1][hlen] == '\0')) {
		len = snprintf(pwd, sizeof(pwd), "~%s", args[1] + hlen);
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

	coma_frame_bar_update(client->frame);
}

static struct client *
client_alloc(struct frame *frame)
{
	struct client	*client;

	client = coma_calloc(1, sizeof(*client));
	TAILQ_INSERT_TAIL(&clients, client, glist);

	if (frame->focus != NULL) {
		TAILQ_INSERT_BEFORE(frame->focus, client, list);
	} else {
		TAILQ_INSERT_HEAD(&frame->clients, client, list);
	}

	client->frame = frame;
	client->id = client_id++;
	client->bw = frame_border;

	return (client);
}

static void
client_attach(struct client *client)
{
	coma_client_adjust(client);
	coma_wm_io_update();

	coma_client_focus(client);
	coma_client_warp_pointer(client);

	coma_frame_bar_update(client->frame);
}
