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

#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>

#include "coma.h"

static void	coma_signal(int);

char			myhost[256];
int			restart = 0;
volatile sig_atomic_t	sig_recv = -1;

static void
usage(void)
{
	printf("Help for coma %s\n", COMA_VERSION);
	printf("\n");
	printf("-c\tconfiguration file ($HOME/.comarc by default)\n");
	printf("\n");
	printf("Mail bugs and patches to joris@coders.se\n");
	printf("\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sigaction	sa;
	int			ch;
	struct passwd		*pw;
	const char		*config;
	char			**cargv;

	config = NULL;
	cargv = argv;
	coma_wm_init();

	while ((ch = getopt(argc, argv, "c:h")) != -1) {
		switch (ch) {
		case 'c':
			config = optarg;
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}

	coma_config_parse(config);

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

	if ((pw = getpwuid(getuid())) != NULL) {
		if (chdir(pw->pw_dir) == -1)
			fatal("chdir(%s): %s", pw->pw_dir, errno_s);
	}

	if (gethostname(myhost, sizeof(myhost)) == -1)
		fatal("gethostname: %s", errno_s);

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
	char	*argv[2];

	argv[0] = "xterm";
	argv[1] = NULL;

	coma_execute(argv);
}

void
coma_execute(char **argv)
{
	pid_t		pid;
	const char	*pwd;

	pid = fork();

	switch (pid) {
	case -1:
		fprintf(stderr, "failed to spawn terminal: %s\n", errno_s);
		return;
	case 0:
		if (frame_active->focus)
			pwd = frame_active->focus->pwd;
		else
			pwd = NULL;

		if (pwd != NULL && chdir(pwd) == -1)
			fprintf(stderr, "chdir: %s\n", errno_s);

		(void)setsid();
		execvp(argv[0], argv);
		fprintf(stderr, "failed to start '%s': %s\n", argv[0], errno_s);
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

int
coma_split_arguments(char *args, char **argv, size_t elm)
{
	size_t		idx;
	int		count;
	char		*p, *line, *end;

	if (elm <= 1)
		fatal("not enough elements (%zu)", elm);

	idx = 0;
	count = 0;
	line = args;

	for (p = line; *p != '\0'; p++) {
		if (idx >= elm - 1)
			break;

		if (*p == ' ') {
			*p = '\0';
			if (*line != '\0') {
				argv[idx++] = line;
				count++;
			}
			line = p + 1;
			continue;
		}

		if (*p != '"')
			continue;

		line = p + 1;
		if ((end = strchr(line, '"')) == NULL)
			break;

		*end = '\0';
		argv[idx++] = line;
		count++;
		line = end + 1;

		while (isspace(*(unsigned char *)line))
			line++;

		p = line;
	}

	if (idx < elm - 1 && *line != '\0') {
		argv[idx++] = line;
		count++;
	}

	argv[idx] = NULL;

	return (count);
}

int
coma_split_string(char *input, const char *delim, char **out, size_t ele)
{
	int		count;
	char		**ap;

	if (ele == 0)
		return (0);

	count = 0;
	for (ap = out; ap < &out[ele - 1] &&
	    (*ap = strsep(&input, delim)) != NULL;) {
		if (**ap != '\0') {
			ap++;
			count++;
		}
	}

	*ap = NULL;
	return (count);
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
