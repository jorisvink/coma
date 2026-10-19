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
#include <sys/wait.h>

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>

#include "coma.h"

static void	coma_signal(int);
static void	coma_log_init(void);

volatile sig_atomic_t	sig_chld = 0;
char			*shell = NULL;
volatile sig_atomic_t	sig_recv = -1;
char			*homedir = NULL;
char			*term_name = NULL;
char			*term_font_name = NULL;
char			*term_font_bold_name = NULL;
int			scrollback_lines = COMA_SCROLLBACK;

static FILE		*logfp = NULL;

static void
usage(void)
{
	printf("Help for coma %s\n", COMA_VERSION);
	printf("\n");
	printf("-c\tconfiguration file ($HOME/.comarc by default)\n");
	printf("-l\tframe layout (default, small-large or small-dual)\n");
	printf("\n");
	printf("Mail bugs and patches to joris@sanctorum.se\n");
	printf("\n");
	exit(1);
}

#if !defined(COMA_TESTS)
int
main(int argc, char *argv[])
{
	coma_init(argc, argv);
	coma_wm_run();

	return (0);
}
#endif

void
coma_init(int argc, char *argv[])
{
	struct sigaction	sa;
	int			ch;
	const char		*sh;
	struct passwd		*pw;
	char			*config, *layout;

	if (setlocale(LC_CTYPE, "") == NULL || MB_CUR_MAX == 1)
		(void)setlocale(LC_CTYPE, "C.UTF-8");

	layout = NULL;
	config = NULL;

	while ((ch = getopt(argc, argv, "c:hl:")) != -1) {
		switch (ch) {
		case 'c':
			if ((config = realpath(optarg, NULL)) == NULL)
				fatal("%s: %s", optarg, errno_s);
			break;
		case 'l':
			layout = optarg;
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}

	if ((pw = getpwuid(getuid())) == NULL)
		fatal("who are you?");

	if ((homedir = strdup(pw->pw_dir)) == NULL)
		fatal("strdup failed");

	if (chdir(homedir) == -1)
		fatal("chdir(%s): %s", homedir, errno_s);

	coma_log_init();
	coma_wm_init();

	if ((sh = getenv("SHELL")) == NULL || *sh == '\0') {
		if (pw->pw_shell != NULL && *pw->pw_shell != '\0')
			sh = pw->pw_shell;
		else
			sh = "/bin/sh";
	}

	if ((shell = strdup(sh)) == NULL)
		fatal("strdup");

	if ((term_name = strdup(COMA_TERM_NAME)) == NULL)
		fatal("strdup");

	if ((term_font_name = strdup(COMA_TERM_FONT)) == NULL)
		fatal("strdup");

	coma_frame_init();
	coma_config_parse(config);
	free(config);

	if (layout != NULL)
		coma_frame_layout(layout);

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

	(void)signal(SIGPIPE, SIG_IGN);

	coma_client_init();
	coma_wm_setup();
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

		coma_client_reap(pid);
	}
}

void
coma_spawn_terminal(void)
{
	char	*argv[2];

	argv[0] = shell;
	argv[1] = NULL;

	coma_client_spawn(argv, 0);
}

void
coma_spawn_terminal_secure(void)
{
	char	*argv[2];

	argv[0] = shell;
	argv[1] = NULL;

	coma_client_spawn(argv, 1);
}

void
coma_spawn_terminal_ce(void)
{
	char	*argv[2];

	argv[0] = "/usr/local/bin/ce";
	argv[1] = NULL;

	coma_client_spawn(argv, 1);
}

void
coma_execute(char **argv)
{
	pid_t		pid;
	const char	*pwd;
	char		path[PATH_MAX];

	pwd = NULL;

	if (client_active != NULL &&
	    coma_term_cwd(client_active->term, path, sizeof(path)) == 0)
		pwd = path;

	pid = fork();

	switch (pid) {
	case -1:
		fprintf(stderr, "failed to spawn process: %s\n", errno_s);
		return;
	case 0:
		(void)signal(SIGPIPE, SIG_DFL);

		if (pwd != NULL && chdir(pwd) == -1)
			fprintf(stderr, "chdir(%s): %s\n", pwd, errno_s);

		(void)setsid();
		execvp(argv[0], argv);
		fprintf(stderr, "failed to start '%s': %s\n", argv[0], errno_s);
		_exit(1);
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
	char		*p, *end;

	if (elm <= 1)
		fatal("not enough elements (%zu)", elm);

	idx = 0;
	p = args;

	while (idx < elm - 1) {
		while (isspace(*(unsigned char *)p))
			p++;

		if (*p == '\0')
			break;

		if (*p == '"') {
			p++;
			if ((end = strchr(p, '"')) == NULL)
				break;
			*end = '\0';
			argv[idx++] = p;
			p = end + 1;
			continue;
		}

		argv[idx++] = p;

		while (*p != '\0' && !isspace(*(unsigned char *)p))
			p++;

		if (*p != '\0')
			*(p)++ = '\0';
	}

	argv[idx] = NULL;

	return (idx);
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

size_t
coma_utf8_decode(const char *str, u_int32_t *cp)
{
	const unsigned char	*s;
	size_t			i, len;

	s = (const unsigned char *)str;

	if (s[0] == 0)
		return (0);

	if (s[0] < 0x80) {
		*cp = s[0];
		return (1);
	}

	if ((s[0] & 0xe0) == 0xc0) {
		len = 2;
		*cp = s[0] & 0x1f;
	} else if ((s[0] & 0xf0) == 0xe0) {
		len = 3;
		*cp = s[0] & 0x0f;
	} else if ((s[0] & 0xf8) == 0xf0) {
		len = 4;
		*cp = s[0] & 0x07;
	} else {
		*cp = 0xfffd;
		return (1);
	}

	for (i = 1; i < len; i++) {
		if ((s[i] & 0xc0) != 0x80) {
			*cp = 0xfffd;
			return (i);
		}
		*cp = (*cp << 6) | (s[i] & 0x3f);
	}

	return (len);
}

size_t
coma_utf8_encode(u_int32_t cp, char *out)
{
	if (cp < 0x80) {
		out[0] = cp;
		return (1);
	}

	if (cp < 0x800) {
		out[0] = 0xc0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3f);
		return (2);
	}

	if (cp < 0x10000) {
		out[0] = 0xe0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3f);
		out[2] = 0x80 | (cp & 0x3f);
		return (3);
	}

	out[0] = 0xf0 | ((cp >> 18) & 0x07);
	out[1] = 0x80 | ((cp >> 12) & 0x3f);
	out[2] = 0x80 | ((cp >> 6) & 0x3f);
	out[3] = 0x80 | (cp & 0x3f);

	return (4);
}

void
coma_log(const char *fmt, ...)
{
	va_list		args;

	if (logfp == NULL)
		return;

	va_start(args, fmt);
	vfprintf(logfp, fmt, args);
	va_end(args);

	fprintf(logfp, "\n");
	fflush(logfp);
}

void
fatal(const char *fmt, ...)
{
	va_list		args;

	fprintf(stderr, "error: ");

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (logfp != NULL) {
		fprintf(logfp, "FATAL: ");
		va_start(args, fmt);
		vfprintf(logfp, fmt, args);
		va_end(args);
		fprintf(logfp, "\n");
		fflush(logfp);
	}

	fprintf(stderr, "\n");
	exit(1);
}

static void
coma_log_init(void)
{
	if ((logfp = fopen(COMA_LOG_FILE, "a")) == NULL)
		fatal("failed to open logfile: %s", strerror(errno));

	if (fcntl(fileno(logfp), F_SETFD, FD_CLOEXEC) == -1)
		fatal("fcntl: %s", errno_s);

	coma_log("coma %s starting", COMA_VERSION);
}

static void
coma_signal(int sig)
{
	if (sig == SIGCHLD)
		sig_chld = 1;
	else
		sig_recv = sig;

	coma_wm_wakeup();
}
