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
#include <sys/wait.h>
#include <sys/queue.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "framework.h"

#define COLOR_RED	"\033[31m"
#define COLOR_GREEN	"\033[32m"
#define COLOR_RESET	"\033[0m"

/*
 * A single test with its name and callback function. The child process
 * its exit status is recorded in status.
 */
struct test {
	pid_t			pid;
	int			status;

	const char		*name;
	void			(*entry)(void);

	TAILQ_ENTRY(test)	list;
};

/* List of all tests that are registered by test_framework_register(). */
static TAILQ_HEAD(, test)		tests;

static const char			*wait_text = NULL;

/*
 * Testing entry point, we immediately hand over control to the test
 * its test_entry() function that it should have defined.
 */
int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);

	TAILQ_INIT(&tests);

	test_entry();

	return (0);
}

/*
 * Stub for compilation.
 */
void
coma_platform_sandbox(void)
{
}

/*
 * Registers a new test to be executed when test_framework_run() is called.
 * The test gets a name and entry point that is called.
 */
void
test_framework_register(const char *name, void (*entry)(void))
{
	struct test	*test;

	if ((test = calloc(1, sizeof(*test))) == NULL)
		fatal("calloc failed");

	test->name = name;
	test->entry = entry;

	TAILQ_INSERT_TAIL(&tests, test, list);
}

/*
 * Run all tests by forking for each test and waiting for its exit.
 * A normal exit code of 0 means the test went OK, anything else is fail.
 */
void
test_framework_run(void)
{
	pid_t		pid;
	struct test	*test;
	const char	*pass, *fail, *reset;
	int		status, failed, total, color;

	color = isatty(STDOUT_FILENO);
	pass = color ? COLOR_GREEN : "";
	fail = color ? COLOR_RED : "";
	reset = color ? COLOR_RESET : "";

	printf("Test schedule\n");
	TAILQ_FOREACH(test, &tests, list)
		printf("  %s\n", test->name);

	printf("\nRunning\n");

	TAILQ_FOREACH(test, &tests, list) {
		if ((test->pid = fork()) == -1) {
			printf("failed to execute '%s', cannot fork (%s)\n",
			    test->name, strerror(errno));
			continue;
		}

		if (test->pid == 0) {
			test->entry();
			exit(0);
		}
	}

	for (;;) {
		if ((pid = waitpid(-1, &status, 0)) == -1) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD)
				break;
			fatal("waitpid: %s", strerror(errno));
		}

		TAILQ_FOREACH(test, &tests, list) {
			if (test->pid != pid)
				continue;

			test->status = status;
		}
	}

	failed = 0;
	total = 0;

	TAILQ_FOREACH(test, &tests, list) {
		total++;

		if (WIFEXITED(test->status) && WEXITSTATUS(test->status) == 0) {
			printf("  %s[PASS]%s %s\n", pass, reset, test->name);
			continue;
		}

		failed++;

		if (WIFEXITED(test->status)) {
			printf("  %s[FAIL]%s %s (exit code %d)\n",
			    fail, reset, test->name,
			    WEXITSTATUS(test->status));
		} else if (WIFSIGNALED(test->status)) {
			printf("  %s[FAIL]%s %s (%d)\n", fail, reset,
			    test->name, WTERMSIG(test->status));
		} else {
			printf("  %s[FAIL]%s %s (status 0x%x)\n",
			    fail, reset, test->name, test->status);
		}
	}

	printf("\nSummary: %s%d/%d passed%s", failed == 0 ? pass : fail,
	    total - failed, total, reset);
	if (failed != 0)
		printf(", %s%d failed%s", fail, failed, reset);
	printf("\n");

	exit(failed == 0 ? 0 : 1);
}

void
test_coma_start(void)
{
	test_coma_config("/dev/null");
}

void
test_coma_config(char *path)
{
	char	*argv[] = { "coma", "-c", NULL, NULL };

	argv[2] = path;

	if (setenv("SDL_VIDEODRIVER", "offscreen", 0) == -1)
		fatal("setenv: %s", errno_s);

	if (setenv("SHELL", "/bin/sh", 1) == -1)
		fatal("setenv: %s", errno_s);

	if (setenv("PS1", "$ ", 1) == -1)
		fatal("setenv: %s", errno_s);

	if (unsetenv("ENV") == -1)
		fatal("unsetenv: %s", errno_s);

	coma_init(3, argv);
	test_wait("$");
}

void
test_pump(u_int32_t ms)
{
	u_int32_t	start;

	start = SDL_GetTicks();

	while (SDL_GetTicks() - start < ms) {
		if (coma_wm_pump(10) == 0)
			fatal("coma stopped running");
	}
}

void
test_until(int (*check)(void), const char *what)
{
	u_int32_t	start;

	start = SDL_GetTicks();

	while (check() == 0) {
		if (SDL_GetTicks() - start > TEST_TIMEOUT)
			fatal("timed out waiting for %s", what);
		if (coma_wm_pump(10) == 0)
			fatal("coma stopped running");
	}
}

int
test_find(const char *text)
{
	int		row;
	char		line[1024];

	if (client_active == NULL)
		return (-1);

	for (row = 0; ; row++) {
		if (coma_term_line(client_active->term,
		    row, line, sizeof(line)) == -1)
			break;

		if (strstr(line, text) != NULL)
			return (row);
	}

	return (-1);
}

static int
test_found(void)
{
	return (test_find(wait_text) != -1);
}

void
test_wait(const char *text)
{
	wait_text = text;
	test_until(test_found, text);
}

int
test_clients(void)
{
	int		count;
	struct client	*client;

	count = 0;

	TAILQ_FOREACH(client, &clients, glist)
		count++;

	return (count);
}

void
test_key(SDL_Keycode sym, u_int16_t mod)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = SDL_KEYDOWN;
	evt.key.state = SDL_PRESSED;
	evt.key.keysym.sym = sym;
	evt.key.keysym.mod = mod;

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	test_pump(10);
}

void
test_type(const char *text)
{
	SDL_Event	evt;
	u_int16_t	mod;

	for (; *text != '\0'; text++) {
		if (*text == '\n') {
			test_key(SDLK_RETURN, KMOD_NONE);
			continue;
		}

		if (isupper((unsigned char)*text))
			mod = KMOD_LSHIFT;
		else
			mod = KMOD_NONE;

		test_key(tolower((unsigned char)*text), mod);

		memset(&evt, 0, sizeof(evt));

		evt.type = SDL_TEXTINPUT;
		evt.text.text[0] = *text;

		if (SDL_PushEvent(&evt) < 0)
			fatal("SDL_PushEvent: %s", SDL_GetError());

		test_pump(10);
	}
}

void
test_prefix(SDL_Keycode sym)
{
	char	text[2];

	test_key(COMA_PREFIX_KEY, KMOD_LCTRL);

	text[0] = sym;
	text[1] = '\0';

	test_type(text);
}

void
test_cell_pos(int row, int col, int *x, int *y)
{
	u_int16_t	cw, ch;

	coma_draw_cell_size(&cw, &ch);

	*x = client_active->x + client_active->bw + COMA_TERM_PAD +
	    (col * cw) + (cw / 2);
	*y = client_active->y + client_active->bw + COMA_TERM_PAD +
	    (row * ch) + (ch / 2);
}

void
test_mouse(u_int32_t type, int x, int y, u_int8_t button, u_int8_t clicks)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = type;

	if (type == SDL_MOUSEMOTION) {
		evt.motion.x = x;
		evt.motion.y = y;
	} else {
		evt.button.x = x;
		evt.button.y = y;
		evt.button.button = button;
		evt.button.clicks = clicks;
	}

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	test_pump(10);
}

void
test_click(int x, int y, u_int8_t button, u_int8_t clicks)
{
	test_mouse(SDL_MOUSEBUTTONDOWN, x, y, button, clicks);
	test_mouse(SDL_MOUSEBUTTONUP, x, y, button, clicks);
}

void
test_resize(int w, int h)
{
	SDL_Event	evt;

	memset(&evt, 0, sizeof(evt));

	evt.type = SDL_WINDOWEVENT;
	evt.window.data1 = w;
	evt.window.data2 = h;
	evt.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;

	if (SDL_PushEvent(&evt) < 0)
		fatal("SDL_PushEvent: %s", SDL_GetError());

	test_pump(10);
}
