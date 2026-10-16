/*
 * Copyright (c) 2026 Joris Vink <joris@coders.se>
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "framework.h"

static void	test_wm_tag(void);
static void	test_wm_cwd(void);
static void	test_wm_quit(void);
static void	test_wm_bind(void);
static void	test_wm_narrow(void);
static void	test_wm_zoom_fit(void);
static void	test_wm_zoom_move(void);
static void	test_wm_bar_input(void);
static void	test_wm_empty_cwd(void);
static void	test_wm_split_nav(void);
static void	test_wm_layout_fit(void);
static void	test_wm_spawn_kill(void);
static void	test_wm_tag_quoted(void);
static void	test_wm_shell_exit(void);
static void	test_wm_zoom_split(void);
static void	test_wm_kill_split(void);
static void	test_wm_kill_merge(void);
static void	test_wm_exit_focus(void);
static void	test_wm_move_client(void);
static void	test_wm_split_merge(void);
static void	test_wm_zoom_switch(void);
static void	test_wm_config_path(void);
static void	test_wm_split_resize(void);
static void	test_wm_shrink_focus(void);
static void	test_wm_window_focus(void);

static int	wm_one_client(void);
static int	wm_two_clients(void);
static int	wm_three_clients(void);
static void	wm_window_focus(u_int8_t);

void
test_entry(void)
{
	test_framework_register("wm-tag", test_wm_tag);
	test_framework_register("wm-cwd", test_wm_cwd);
	test_framework_register("wm-quit", test_wm_quit);
	test_framework_register("wm-bind", test_wm_bind);
	test_framework_register("wm-narrow", test_wm_narrow);
	test_framework_register("wm-zoom-fit", test_wm_zoom_fit);
	test_framework_register("wm-zoom-move", test_wm_zoom_move);
	test_framework_register("wm-bar-input", test_wm_bar_input);
	test_framework_register("wm-empty-cwd", test_wm_empty_cwd);
	test_framework_register("wm-split-nav", test_wm_split_nav);
	test_framework_register("wm-layout-fit", test_wm_layout_fit);
	test_framework_register("wm-spawn-kill", test_wm_spawn_kill);
	test_framework_register("wm-tag-quoted", test_wm_tag_quoted);
	test_framework_register("wm-shell-exit", test_wm_shell_exit);
	test_framework_register("wm-zoom-split", test_wm_zoom_split);
	test_framework_register("wm-kill-split", test_wm_kill_split);
	test_framework_register("wm-kill-merge", test_wm_kill_merge);
	test_framework_register("wm-exit-focus", test_wm_exit_focus);
	test_framework_register("wm-move-client", test_wm_move_client);
	test_framework_register("wm-split-merge", test_wm_split_merge);
	test_framework_register("wm-zoom-switch", test_wm_zoom_switch);
	test_framework_register("wm-config-path", test_wm_config_path);
	test_framework_register("wm-split-resize", test_wm_split_resize);
	test_framework_register("wm-shrink-focus", test_wm_shrink_focus);
	test_framework_register("wm-window-focus", test_wm_window_focus);

	test_framework_run();
}

static void
test_wm_spawn_kill(void)
{
	struct client	*first;

	test_coma_start();

	first = client_active;

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	if (client_active == first)
		fatal("the new client is not active");

	test_prefix('k');
	test_until(wm_one_client, "the second client to go away");

	if (client_active != first)
		fatal("the first client is not active again");
}

static void
test_wm_split_merge(void)
{
	struct client	*upper;

	test_coma_start();

	test_prefix('s');

	if (frame_active->split == NULL)
		fatal("frame was not split");

	test_until(wm_two_clients, "a second client");

	test_prefix('f');
	upper = client_active;

	test_prefix('m');

	if (frame_active->split != NULL)
		fatal("frame was not merged");

	if (client_active != upper)
		fatal("merge changed the active client");
}

static void
test_wm_tag(void)
{
	test_coma_start();

	test_prefix(':');
	test_type("tag hello\n");
	test_pump(50);

	if (client_active->tag == NULL)
		fatal("client was not tagged");

	if (strcmp(client_active->tag, "hello"))
		fatal("client has tag '%s'", client_active->tag);
}

static void
test_wm_quit(void)
{
	SDL_Event	evt;

	test_coma_start();

	memset(&evt, 0, sizeof(evt));
	evt.type = SDL_QUIT;

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	alarm(TEST_TIMEOUT / 1000);
	coma_wm_run();
	alarm(0);

	if (test_clients() != 0)
		fatal("clients were not cleaned up");
}

static void
test_wm_bind(void)
{
	test_coma_start();

	if (coma_wm_register_action("frame-split", 'l') == -1)
		fatal("failed to bind frame-split");

	test_prefix('l');

	if (frame_active->split == NULL)
		fatal("bind did not take over the key");
}

static void
test_wm_config_path(void)
{
	FILE	*fp;
	char	*path, abs[PATH_MAX];

	path = "obj/wm-config-path.rc";

	if ((fp = fopen(path, "w")) == NULL)
		fatal("fopen(%s): %s", path, errno_s);

	fprintf(fp, "frame-layout small-large\n");
	(void)fclose(fp);

	if (realpath(path, abs) == NULL)
		fatal("realpath(%s): %s", path, errno_s);

	test_coma_config(path);
	(void)unlink(abs);

	if (frame_layout != COMA_FRAME_LAYOUT_SMALL_LARGE)
		fatal("relative config path was not used");
}

static void
test_wm_cwd(void)
{
	test_coma_start();

	test_type("cd /tmp && printf '\\033]0;h;/nonexistent;x\\007'; "
	    "echo set$((1 + 1))\n");
	test_wait("set2");

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_type("pwd\n");
	test_wait("/tmp");
}

static void
test_wm_narrow(void)
{
	test_coma_start();

	test_resize(300, 600);
	test_pump(50);

	if (frame_active == NULL)
		fatal("no frame after a narrow resize");

	test_type("echo alive$((2 + 2))\n");
	test_wait("alive4");
}

static void
test_wm_layout_fit(void)
{
	u_int32_t	id;
	int		width;
	struct frame	*frame;

	test_coma_start();

	for (width = 1000; width < 2100; width += 7) {
		test_resize(width, 800);
		test_pump(10);

		for (id = 1; (frame = coma_frame_lookup(id)) != NULL; id++) {
			if (frame->x + frame->w + (frame_border * 2) > width)
				fatal("frame %u does not fit in %d", id, width);
		}
	}
}

static void
test_wm_tag_quoted(void)
{
	test_coma_start();

	test_prefix(':');
	test_type("\"tag\" \"my project\"\n");
	test_pump(50);

	if (client_active->tag == NULL)
		fatal("client was not tagged");

	if (strcmp(client_active->tag, "my project"))
		fatal("client has tag '%s'", client_active->tag);
}

static void
test_wm_move_client(void)
{
	struct frame	*frame;

	test_coma_start();

	frame = frame_active;
	test_prefix('o');

	if (frame_active == frame)
		fatal("client was not moved");

	if (frame->focus != NULL)
		fatal("old frame still points at the moved client");
}

static void
test_wm_split_resize(void)
{
	test_coma_start();

	test_prefix('s');
	test_until(wm_two_clients, "a second client");

	test_prefix('z');
	test_resize(1200, 700);
	test_pump(50);

	if (frame_active->split == NULL)
		fatal("split was lost on resize");

	if (!(frame_active->flags & COMA_FRAME_ZOOMED))
		fatal("zoom was lost on resize");

	if (test_clients() != 2)
		fatal("clients went away on resize");
}

static void
test_wm_shell_exit(void)
{
	test_coma_start();

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_type("exec sh\n");
	test_pump(100);

	test_type("sleep 10 & exit\n");
	test_until(wm_one_client, "the exited client to go away");
}

static void
test_wm_bar_input(void)
{
	int		x, y;
	struct client	*first;

	test_coma_start();

	first = client_active;

	test_prefix('c');
	test_until(wm_two_clients, "a second client");
	test_pump(50);

	test_prefix(':');

	x = first->frame->x + frame_border + first->fbo + 2;
	y = first->frame->y + first->frame->h + (frame_border * 3) +
	    (frame_gap / 2) + frame_bar - 5;

	test_click(x, y, SDL_BUTTON_LEFT, 1);
	test_type("tag hello\n");
	test_pump(50);

	if (first->tag != NULL)
		fatal("bar click changed the client during input");
}

static void
test_wm_empty_cwd(void)
{
	test_coma_start();

	test_type("cd /tmp && echo in$((1 + 1))\n");
	test_wait("in2");

	test_prefix('l');

	if (client_active == NULL || client_active->frame == frame_active)
		fatal("did not move to an empty frame");

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_type("pwd\n");
	test_wait("/tmp");
}

static void
test_wm_zoom_fit(void)
{
	int		right;
	struct frame	*large;

	test_coma_start();

	test_prefix('w');
	test_key('2', 0);
	test_pump(50);

	if (frame_layout != COMA_FRAME_LAYOUT_SMALL_LARGE)
		fatal("layout was not swapped");

	if ((large = coma_frame_lookup(2)) == NULL)
		fatal("no large frame");

	right = large->x + large->w + (frame_border * 2);

	test_prefix('z');
	test_pump(50);

	if (frame_active->x + frame_active->w + (frame_border * 2) != right)
		fatal("zoomed frame does not fit the layout");
}

static void
test_wm_zoom_move(void)
{
	struct frame	*zoomed;

	test_coma_start();

	test_resize(1700, 800);
	test_pump(50);

	test_prefix('o');
	zoomed = frame_active;

	test_prefix('z');
	test_prefix('o');
	test_pump(50);

	if (client_active == NULL || client_active->frame != zoomed)
		fatal("client was moved out of the zoomed frame");
}

static void
test_wm_split_nav(void)
{
	struct frame	*middle;
	struct client	*client;

	test_coma_start();

	test_resize(1700, 800);
	test_pump(50);

	if ((middle = coma_frame_lookup(2)) == NULL ||
	    coma_frame_lookup(3) == NULL)
		fatal("expected three frames");

	test_prefix('3');
	test_prefix('s');
	test_until(wm_two_clients, "a second client");

	test_prefix('1');
	test_prefix('s');
	test_until(wm_three_clients, "a third client");

	client = client_active;
	test_prefix('o');

	if (client->frame != middle)
		fatal("client move skipped the middle frame");
}

static void
test_wm_zoom_split(void)
{
	test_coma_start();

	test_prefix('z');
	test_prefix('s');
	test_until(wm_two_clients, "a second client");

	if (frame_active->flags & COMA_FRAME_ZOOMED)
		fatal("new split frame inherited the zoom");

	if (frame_active->split->flags & COMA_FRAME_ZOOMED)
		fatal("split parent is still zoomed");
}

static void
test_wm_kill_split(void)
{
	struct frame	*upper;

	test_coma_start();

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_prefix('o');
	upper = frame_active;

	test_prefix('s');
	test_until(wm_three_clients, "a third client");

	test_prefix('k');
	test_until(wm_two_clients, "the third client to go away");

	if (frame_active != upper)
		fatal("focus left the merged frame");
}

static void
test_wm_kill_merge(void)
{
	struct frame	*upper, *lower, *other;

	test_coma_start();

	upper = frame_active;

	test_prefix('s');
	test_until(wm_two_clients, "a second client");

	lower = frame_active;

	test_prefix('l');
	other = frame_active;

	test_prefix('s');
	test_until(wm_three_clients, "a third client");

	coma_frame_focus(lower, 0);
	coma_frame_focus(other, 0);

	test_prefix('k');
	test_until(wm_two_clients, "the second client to go away");

	if (other->split == NULL)
		fatal("the wrong frame was merged");

	if (upper->split != NULL)
		fatal("the empty frame was not merged");
}

static void
test_wm_exit_focus(void)
{
	struct frame	*frame;

	test_coma_start();

	frame = frame_active;

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_prefix('c');
	test_until(wm_three_clients, "a third client");

	test_prefix('o');

	if (frame_active == frame || frame->focus == NULL)
		fatal("client was not moved");

	if (kill(coma_term_pid(frame->focus->term), SIGKILL) == -1)
		fatal("kill: %s", errno_s);

	test_until(wm_two_clients, "the killed client to go away");

	if (frame->focus == NULL)
		fatal("frame has no focused client");
}

static void
test_wm_zoom_switch(void)
{
	struct frame	*first, *zoomed;

	test_coma_start();

	first = frame_active;

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_prefix('o');
	zoomed = frame_active;

	test_prefix('z');

	if (!(zoomed->flags & COMA_FRAME_ZOOMED))
		fatal("frame was not zoomed");

	test_prefix('q');
	test_key('0', 0);
	test_pump(50);

	if (frame_active != first)
		fatal("first frame is not active");

	if (zoomed->flags & COMA_FRAME_ZOOMED)
		fatal("previous frame is still zoomed");
}

static void
test_wm_shrink_focus(void)
{
	struct client	*moved;

	test_coma_start();

	test_prefix('c');
	test_until(wm_two_clients, "a second client");

	test_prefix('o');
	moved = client_active;

	test_resize(500, 768);
	test_pump(50);

	if (client_active != moved)
		fatal("active client changed on relayout");

	if (moved->frame != frame_active || moved->frame->focus != moved)
		fatal("active client is not focused in the active frame");
}

static void
test_wm_window_focus(void)
{
	test_coma_start();

	test_type("printf '\\033[?1004h'; cat -v\n");
	test_pump(100);

	wm_window_focus(SDL_WINDOWEVENT_FOCUS_LOST);

	if (coma_wm_focused())
		fatal("window still focused");

	wm_window_focus(SDL_WINDOWEVENT_FOCUS_GAINED);
	test_type("\n");

	test_wait("^[[O^[[I");
}

static int
wm_one_client(void)
{
	return (test_clients() == 1);
}

static int
wm_two_clients(void)
{
	return (test_clients() == 2);
}

static int
wm_three_clients(void)
{
	return (test_clients() == 3);
}

static void
wm_window_focus(u_int8_t event)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = SDL_WINDOWEVENT;
	evt.window.event = event;

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	test_pump(10);
}
