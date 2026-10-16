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

#define FRAME_CONFIG_MAX		16

#define FRAME_CLIENT_DEFAULT		"[normal]"
#define FRAME_CLIENT_SECURE		"[secure]"

static void	frame_free(struct frame *);
static void	frame_draw(struct frame *);
static void	frame_layout_default(void);
static void	frame_unzoom(struct frame *);
static void	frame_layout_small_large(int);
static void	frame_bar_draw(struct frame *);
static int	frame_is_lower(struct frame *);
static void	frame_bar_origin(struct frame *, int *, int *);
static void	frame_collect(struct frame *, struct client **,
		    u_int32_t *, int *, int *, size_t *, size_t);

static u_int16_t	frame_fit(int);
static void		frame_client_move(int);
static struct frame	*frame_find_left(void);
static struct frame	*frame_find_right(void);
static struct frame	*frame_split(struct frame *);
static struct client	*frame_client_hit(struct frame *,
			    u_int16_t, u_int16_t);

static struct frame_list	frames;
static u_int32_t		frame_id = 1;
static u_int16_t		zoom_width = 0;

static int			setup_done = 0;
static u_int16_t		cfg_frame_height = 0;
static int			cfg_frame_offset = -1;

static int			config_count = 0;
static u_int16_t		config_frames[FRAME_CONFIG_MAX][4];

int				frame_count = -1;
int				frame_offset = -1;
u_int16_t			frame_height = 0;
u_int16_t			frame_y_offset = 0;
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
	if (setup_done == 0) {
		setup_done = 1;
		cfg_frame_height = frame_height;
		cfg_frame_offset = frame_offset;
	} else {
		frame_height = cfg_frame_height;
		frame_offset = cfg_frame_offset;
	}

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
		frame_free(frame);
	}

	frame_active = NULL;
}

void
coma_frame_relayout(void)
{
	size_t		count, idx, i;
	u_int32_t	*ids, active_id;
	struct frame	*frame, **targets;
	struct client	*client, *active, **list;
	int		*focus, *lower, zoomed, active_lower, lost, j;

	count = 0;
	TAILQ_FOREACH(client, &clients, glist)
		count++;

	ids = coma_calloc(count + 1, sizeof(*ids));
	list = coma_calloc(count + 1, sizeof(*list));
	lower = coma_calloc(count + 1, sizeof(*lower));
	focus = coma_calloc(count + 1, sizeof(*focus));
	targets = coma_calloc(count + 1, sizeof(*targets));

	idx = 0;

	TAILQ_FOREACH(frame, &frames, list)
		frame_collect(frame, list, ids, focus, lower, &idx, count);

	active = client_active;
	active_lower = frame_is_lower(frame_active);
	active_id = coma_frame_base_id(frame_active);
	zoomed = (frame_active->flags & COMA_FRAME_ZOOMED) != 0;

	coma_frame_cleanup();

	frame_id = 1;
	for (j = 0; j < config_count; j++) {
		frame = coma_frame_create(config_frames[j][2],
		    config_frames[j][3], config_frames[j][0],
		    config_frames[j][1]);
		coma_frame_register(frame);
	}

	coma_frame_setup();

	for (i = 0; i < idx; i++) {
		if ((targets[i] = coma_frame_lookup(ids[i])) == NULL) {
			lower[i] = 0;
			targets[i] = TAILQ_FIRST(&frames);
		}
	}

	lost = 1;

	if ((frame = coma_frame_lookup(active_id)) != NULL) {
		if (active_lower) {
			if (frame->split == NULL)
				(void)frame_split(frame);
			frame = frame->split;
		}
		frame_active = frame;
		lost = 0;
	}

	for (i = 0; i < idx; i++) {
		frame = targets[i];

		if (lower[i]) {
			if (frame->split == NULL)
				(void)frame_split(frame);
			frame = frame->split;
		}

		client = list[i];
		client->frame = frame;
		TAILQ_INSERT_TAIL(&frame->clients, client, list);

		if (focus[i] && frame->focus == NULL)
			frame->focus = client;

		coma_client_adjust(client);
	}

	free(ids);
	free(list);
	free(lower);
	free(focus);
	free(targets);

	if (active != NULL) {
		active->frame->focus = active;
		if (lost)
			frame_active = active->frame;
	}

	if (frame_active->focus != NULL)
		coma_client_focus(frame_active->focus);

	if (zoomed)
		coma_frame_zoom();

	coma_frame_bars_update();
}

u_int32_t
coma_frame_base_id(struct frame *frame)
{
	if (frame->split != NULL && frame->split->id < frame->id)
		return (frame->split->id);

	return (frame->id);
}

void
coma_frame_config_create(u_int16_t x, u_int16_t y, u_int16_t w, u_int16_t h)
{
	struct frame	*frame;

	if (config_count < FRAME_CONFIG_MAX) {
		config_frames[config_count][0] = x;
		config_frames[config_count][1] = y;
		config_frames[config_count][2] = w;
		config_frames[config_count][3] = h;
		config_count++;
	}

	frame = coma_frame_create(w, h, x, y);
	coma_frame_register(frame);
}

void
coma_frame_draw_all(void)
{
	struct frame	*frame;

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame != frame_active &&
		    !(frame->flags & COMA_FRAME_ZOOMED))
			frame_draw(frame);
	}

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame != frame_active &&
		    (frame->flags & COMA_FRAME_ZOOMED))
			frame_draw(frame);
	}

	if (frame_active->flags & COMA_FRAME_INLIST)
		frame_draw(frame_active);
}

struct client *
coma_frame_client_at(u_int16_t x, u_int16_t y)
{
	struct frame	*frame;
	struct client	*client;

	if ((client = frame_client_hit(frame_active, x, y)) != NULL)
		return (client);

	TAILQ_FOREACH(frame, &frames, list) {
		if ((client = frame_client_hit(frame, x, y)) != NULL)
			return (client);
	}

	return (NULL);
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
	if (frame_active->split != NULL)
		return;

	if (frame_active->flags & COMA_FRAME_ZOOMED)
		frame_unzoom(frame_active);

	frame_active = frame_split(frame_active);
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

	if ((focus = frame_active->focus) == NULL)
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

	frame_free(dies);

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
}

void
coma_frame_mouseover(u_int16_t x, u_int16_t y)
{
	struct client		*client, *prev;
	struct frame		*frame, *prev_frame;

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

	if (frame == NULL || frame == frame_active)
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

void
coma_frame_zoom(void)
{
	struct client		*client;

	if (frame_active->flags & COMA_FRAME_ZOOMED) {
		frame_unzoom(frame_active);
		if (frame_active->focus != NULL)
			coma_client_focus(frame_active->focus);
		return;
	}

	if (frame_active->focus == NULL)
		return;

	frame_active->w = zoom_width;
	frame_active->h = frame_height;
	frame_active->x = frame_offset;
	frame_active->y = frame_y_offset;
	frame_active->flags |= COMA_FRAME_ZOOMED;

	TAILQ_FOREACH(client, &frame_active->clients, list)
		coma_client_adjust(client);

	coma_client_focus(frame_active->focus);
	coma_frame_bar_update(frame_active);
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
	coma_wm_dirty();
}

int
coma_frame_bar_click(u_int16_t x, u_int16_t y)
{
	int			bx, by;
	u_int16_t		offset;
	struct frame		*frame;
	struct client		*client;

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame != frame_active &&
		    (frame_active->flags & COMA_FRAME_ZOOMED))
			continue;

		frame_bar_origin(frame, &bx, &by);
		if (x >= bx && x < bx + frame->w &&
		    y >= by && y < by + frame_bar)
			break;
	}

	if (frame == NULL)
		return (0);

	client = NULL;
	offset = x - bx;

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

	return (1);
}

static void
frame_layout_default(void)
{
	struct frame	*frame;
	int		y_offset;
	u_int16_t	i, fw, need, count, width, offset, x;

	count = 0;
	fw = frame_width;

	if (frame_offset != -1)
		width = frame_fit(screen_width - frame_offset);
	else
		width = screen_width;

	if (frame_height == 0) {
		frame_y_offset = frame_gap;
		frame_height = frame_fit(screen_height - (frame_gap * 2) -
		    frame_bar - (frame_border * 2));
	} else {
		y_offset = (screen_height -
		    (frame_bar + frame_height + (frame_border * 2))) / 2;
		if (y_offset < 0)
			y_offset = 0;
		frame_y_offset = y_offset;
	}

	need = fw + (frame_border * 2);

	while (width >= need) {
		if (frame_count != -1 && count == frame_count)
			break;
		count++;
		width -= need;
		need = fw + (frame_border * 2) + frame_gap;
	}

	if (count == 0) {
		count = 1;
		width = frame_gap * 2;
		fw = frame_fit(screen_width - (frame_gap * 2) -
		    (frame_border * 2));
	}

	if (frame_offset != -1) {
		offset = frame_offset;
	} else {
		offset = width / 2;
	}

	x = offset;
	zoom_width = 0;

	for (i = 0; i < count; i++) {
		frame = coma_frame_create(fw,
		    frame_height, offset, frame_y_offset);
		coma_frame_register(frame);
		offset += fw + frame_gap + (frame_border * 2);
		zoom_width += fw + frame_gap + (frame_border * 2);
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
}

struct frame *
coma_frame_lookup(u_int32_t id)
{
	struct frame	*frame;

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

	if (prev != frame && (prev->flags & COMA_FRAME_ZOOMED))
		frame_unzoom(prev);

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

	frame->id = frame_id++;

	frame->x = x;
	frame->y = y;
	frame->orig_x = x;
	frame->orig_y = y;

	frame->w = width;
	frame->h = height;
	frame->orig_w = width;
	frame->orig_h = height;

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
	frame_height = frame_fit(screen_height - (frame_gap * 2) -
	    frame_bar - (frame_border * 2));

	/* Small frame on the left hand-side. */
	frame = coma_frame_create(frame_width, frame_height,
	    offset, frame_y_offset);
	coma_frame_register(frame);
	offset += frame_width + frame_gap + (frame_border * 2);

	/* Rest of the screen covered by large/dual frame(s). */
	if (dual) {
		width = frame_fit(((screen_width - offset -
		    (frame_gap * 2)) / 2) - (frame_border * 2));
	} else {
		width = frame_fit(screen_width - offset - frame_gap -
		    (frame_border * 2));
	}

	frame = coma_frame_create(width, frame_height, offset, frame_y_offset);
	coma_frame_register(frame);

	if (dual) {
		offset += width + frame_gap + (frame_border * 2);
		frame = coma_frame_create(width, frame_height, offset,
		    frame_y_offset);
		coma_frame_register(frame);
	}

	zoom_width = frame_fit(screen_width - frame_offset - frame_gap -
	    (frame_border * 2));
}

static void
frame_free(struct frame *frame)
{
	free(frame);
}

static u_int16_t
frame_fit(int value)
{
	if (value < 1)
		return (1);

	if (value > USHRT_MAX)
		return (USHRT_MAX);

	return (value);
}

static void
frame_collect(struct frame *frame, struct client **list, u_int32_t *ids,
    int *focus, int *lower, size_t *idx, size_t count)
{
	struct client		*client;

	TAILQ_FOREACH(client, &frame->clients, list) {
		if (*idx == count)
			return;
		list[*idx] = client;
		lower[*idx] = frame_is_lower(frame);
		ids[*idx] = coma_frame_base_id(frame);
		focus[*idx] = (frame->focus == client);
		(*idx)++;
	}
}

static int
frame_is_lower(struct frame *frame)
{
	if (frame->split == NULL)
		return (0);

	return (frame->orig_y > frame->split->orig_y);
}

static struct frame *
frame_split(struct frame *parent)
{
	struct frame	*frame;
	struct client	*client;
	u_int16_t	height, used, y;

	height = frame_border + parent->h + frame_border + frame_bar;
	used = (frame_border * 4) + (frame_bar * 2) + frame_gap;
	height = frame_fit((height - used) / 2);

	y = parent->y + frame_border + height + frame_border +
	    frame_bar + frame_gap;

	frame = coma_frame_create(parent->w, height, parent->x, y);

	if (parent->flags & COMA_FRAME_INLIST)
		TAILQ_INSERT_TAIL(&frames, frame, list);

	frame->split = parent;
	frame->flags = parent->flags;

	parent->split = frame;
	parent->h = height;

	parent->orig_h = parent->h;

	TAILQ_FOREACH(client, &parent->clients, list)
		coma_client_adjust(client);

	coma_frame_bar_update(frame);
	coma_frame_bar_update(parent);

	return (frame);
}

static void
frame_unzoom(struct frame *frame)
{
	struct client	*client;

	frame->w = frame->orig_w;
	frame->h = frame->orig_h;
	frame->x = frame->orig_x;
	frame->y = frame->orig_y;
	frame->flags &= ~COMA_FRAME_ZOOMED;

	TAILQ_FOREACH(client, &frame->clients, list)
		coma_client_adjust(client);

	coma_frame_bar_update(frame);
}

static struct client *
frame_client_hit(struct frame *frame, u_int16_t x, u_int16_t y)
{
	struct client		*client;

	if ((client = frame->focus) == NULL)
		client = TAILQ_FIRST(&frame->clients);

	if (client == NULL)
		return (NULL);

	if (x >= client->x && y >= client->y &&
	    x < client->x + client->w + (client->bw * 2) &&
	    y < client->y + client->h + (client->bw * 2))
		return (client);

	return (NULL);
}

static void
frame_draw(struct frame *frame)
{
	struct color		*color;
	struct client		*client;

	if ((client = frame->focus) == NULL)
		client = TAILQ_FIRST(&frame->clients);

	if (client != NULL) {
		if (client == client_active)
			color = coma_wm_color("client-active");
		else
			color = coma_wm_color("client-inactive");

		coma_draw_box(client->x, client->y,
		    client->w + (client->bw * 2), client->h + (client->bw * 2),
		    client->bw, color);

		coma_term_draw(client->term, client->x + client->bw,
		    client->y + client->bw, client->w, client->h,
		    client == client_active && coma_wm_focused());
	}

	frame_bar_draw(frame);
}

static void
frame_bar_origin(struct frame *frame, int *x, int *y)
{
	*x = frame->x + frame_border;
	*y = frame->y + frame->h +
	    (frame_border * 2) + (frame_gap / 2) + frame_border;
}

static void
frame_bar_draw(struct frame *frame)
{
	size_t			slen;
	struct client		*client;
	u_int16_t		offset, width;
	int			len, idx, x, y;
	char			buf[64], status[256];
	struct color		*bar_active, *bar_inactive, *normal;
	struct color		*active, *inactive, *color, *dir, *secure;

	frame_bar_origin(frame, &x, &y);

	idx = 0;
	offset = 5;
	buf[0] = '\0';

	secure = coma_wm_color("frame-bar-client-secure");
	normal = coma_wm_color("frame-bar-client-normal");

	dir = coma_wm_color("frame-bar-directory");
	active = coma_wm_color("frame-bar-client-active");
	inactive = coma_wm_color("frame-bar-client-inactive");

	bar_active = coma_wm_color("frame-bar");
	bar_inactive = coma_wm_color("frame-bar-inactive");

	if (frame_active == frame) {
		color = coma_wm_color("client-active");
		coma_draw_fill(x, y, frame->w, frame_bar, bar_active);
	} else {
		dir = inactive;
		active = inactive;
		color = coma_wm_color("client-inactive");
		coma_draw_fill(x, y, frame->w, frame_bar, bar_inactive);
	}

	coma_draw_box(x - frame_border, y - frame_border,
	    frame->w + (frame_border * 2), frame_bar + (frame_border * 2),
	    frame_border, color);

	coma_draw_clip(x, y, frame->w, frame_bar);

	if (frame->focus != NULL)
		client = frame->focus;
	else
		client = NULL;

	if (client != NULL) {
		if (frame_active != frame) {
			secure->r /= 4;
			secure->g /= 4;
			secure->b /= 4;

			normal->r /= 4;
			normal->g /= 4;
			normal->b /= 4;
		}

		if (client->secure) {
			coma_draw_text(x + 5, y + 15, secure,
			    FRAME_CLIENT_SECURE,
			    sizeof(FRAME_CLIENT_SECURE) - 1);
		} else {
			coma_draw_text(x + 5, y + 15, normal,
			    FRAME_CLIENT_DEFAULT,
			    sizeof(FRAME_CLIENT_DEFAULT) - 1);
		}

		if (frame_active != frame) {
			secure->r *= 4;
			secure->g *= 4;
			secure->b *= 4;

			normal->r *= 4;
			normal->g *= 4;
			normal->b *= 4;
		}

		if (client->pwd != NULL) {
			if (client->host) {
				len = snprintf(status, sizeof(status),
				    "%s - %s", client->host, client->pwd);
			} else {
				len = snprintf(status, sizeof(status), "%s",
				    client->pwd);
			}

			if (len > 0 && (size_t)len >= sizeof(status))
				len = sizeof(status) - 1;

			if (len > 0)
				coma_draw_text(x + 65,
				    y + 15, dir, status, len);
		}
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

		if (len < 0) {
			(void)strlcpy(buf, "[?]", sizeof(buf));
		} else if ((size_t)len >= sizeof(buf)) {
			slen = sizeof(buf) - 2;
			while (slen > 1 && (buf[slen] & 0xc0) == 0x80)
				slen--;
			buf[slen++] = ']';
			buf[slen] = '\0';
		}

		idx++;

		if (client == frame->focus)
			color = active;
		else
			color = inactive;

		slen = strlen(buf);
		width = coma_draw_text(x + offset, y + 30, color, buf, slen);

		client->fbo = offset;
		client->fbw = width;

		offset += width + 4;
	}

	coma_draw_unclip();
}

static struct frame *
frame_find_left(void)
{
	struct frame	*frame, *candidate;

	candidate = NULL;

	TAILQ_FOREACH_REVERSE(frame, &frames, frame_list, list) {
		if (frame->x >= frame_active->x)
			continue;
		if (candidate == NULL || frame->x > candidate->x ||
		    (frame->x == candidate->x && frame->y == frame_active->y))
			candidate = frame;
	}

	return (candidate);
}

static struct frame *
frame_find_right(void)
{
	struct frame	*frame, *candidate;

	candidate = NULL;

	TAILQ_FOREACH(frame, &frames, list) {
		if (frame->x <= frame_active->x)
			continue;
		if (candidate == NULL || frame->x < candidate->x ||
		    (frame->x == candidate->x && frame->y == frame_active->y))
			candidate = frame;
	}

	return (candidate);
}

static void
frame_client_move(int which)
{
	struct frame	*other, *prev;
	struct client	*client, *next;

	if (!(frame_active->flags & COMA_FRAME_INLIST) ||
	    frame_active->flags & COMA_FRAME_ZOOMED)
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

	if ((client = frame_active->focus) == NULL)
		client = TAILQ_FIRST(&frame_active->clients);

	if ((next = TAILQ_NEXT(client, list)) == NULL)
		next = TAILQ_FIRST(&frame_active->clients);

	if (next == client)
		next = NULL;

	TAILQ_REMOVE(&frame_active->clients, client, list);
	TAILQ_INSERT_HEAD(&other->clients, client, list);

	frame_active->focus = next;

	client->frame = other;
	client->x = other->x;

	coma_client_adjust(client);

	frame_active = other;
	coma_client_focus(client);
	coma_client_warp_pointer(client);

	coma_frame_bar_update(prev);
	coma_frame_bar_update(frame_active);
}
