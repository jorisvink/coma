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
#include <sys/wait.h>

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "coma.h"

static void	coma_signal(int);

int			restart = 0;
volatile sig_atomic_t	sig_recv = -1;

static void
usage(void)
{
	printf("Help for coma %s\n", COMA_VERSION);
	printf("\n");
	printf("-f\tFrame width, default (80) or large (161))\n");
	printf("-w\tSpecify frame width yourself\n");
	printf("\n");
	printf("Mail bugs and patches to joris@coders.se\n");
	printf("\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sigaction	sa;
	u_int16_t		fw;
	char			**cargv;
	int			ch, width, fflag;

	fflag = 0;
	width = -1;
	cargv = argv;

	while ((ch = getopt(argc, argv, "hf:w:")) != -1) {
		switch (ch) {
		case 'f':
			fflag = 1;
			if (!strcmp(optarg, "default")) {
				fw = COMA_FRAME_WIDTH_DEFAULT;
			} else if (!strcmp(optarg, "large")) {
				fw = COMA_FRAME_WIDTH_LARGE;
			} else {
				fatal("unknown frame type %s (default|large)",
				    optarg);
			}
			break;
		case 'w':
			/* XXX */
			width = atoi(optarg);
			if (width <= 0 && width >= USHRT_MAX) {
				fatal("width %d is probably not what you want",
				    width);
			}
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}

	if (fflag && width != -1)
		fatal("-f and -w are mutually exclusive options");

	if (width != -1) {
		frame_width = width;
	} else if (fflag) {
		frame_width = fw;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = coma_signal;

	if (sigfillset(&sa.sa_mask) == -1)
		fatal("sigfillset: %s", errno_s);

	if (sigaction(SIGINT, &sa, NULL) == -1)
		fatal("sigaction: %s", errno_s);
	if (sigaction(SIGHUP, &sa, NULL) == -1)
		fatal("sigaction: %s", errno_s);
	if (sigaction(SIGQUIT, &sa, NULL) == -1)
		fatal("sigaction: %s", errno_s);
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		fatal("sigaction: %s", errno_s);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		fatal("sigaction: %s", errno_s);

	coma_wm_setup();
	coma_wm_run();

	if (restart) {
		execvp(cargv[0], cargv);
		fatal("failed to restart process: %s", errno_s);
	}

	return (0);
}

void
coma_reap(void)
{
	pid_t		pid;
	int		status;

	for (;;) {
		if ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) == -1) {
			if (errno == ECHILD)
				return;
			if (errno == EINTR)
				continue;
			fprintf(stderr, "waitpid: %s\n", errno_s);
			return;
		}

		if (pid == 0)
			return;
	}
}

void
coma_spawn_terminal(void)
{
	pid_t		pid;
	char		*args[2];

	pid = fork();

	switch (pid) {
	case -1:
		fprintf(stderr, "failed to spawn terminal: %s\n", errno_s);
		return;
	case 0:
		(void)setsid();
		args[0] = "xterm";
		args[1] = NULL;
		execvp(args[0], args);
		fprintf(stderr, "failed to start terminal: %s\n", errno_s);
		exit(1);
		break;
	default:
		break;
	}
}

void *
coma_malloc(size_t len)
{
	void		*ptr;

	if ((ptr = malloc(len)) == NULL)
		fatal("malloc: %s", errno_s);

	return (ptr);
}

void *
coma_calloc(size_t memb, size_t len)
{
	void		*ptr;

	if (SIZE_MAX / memb < len)
		fatal("coma_calloc(): memb * len > SIZE_MAX");

	if ((ptr = calloc(memb, len)) == NULL)
		fatal("calloc: %s", errno_s);

	return (ptr);
}

void
fatal(const char *fmt, ...)
{
	va_list		args;

	fprintf(stderr, "error: ");

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
	exit(1);
}

static void
coma_signal(int sig)
{
	sig_recv = sig;
}
