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

#include <ctype.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if defined(__linux__)
#include <bsd/string.h>
#endif

#include "coma.h"

#define CONFIG_ARGV_MAX		64
#define CONFIG_LINE_MAX		1024

static void	config_bind(int, char **);
static void	config_font(int, char **);
static void	config_color(int, char **);
static void	config_shell(int, char **);
static void	config_prefix(int, char **);
static void	config_term_font(int, char **);
static void	config_term_name(int, char **);
static void	config_scrollback(int, char **);
static void	config_screen_width(int, char **);
static void	config_screen_height(int, char **);
static void	config_term_font_bold(int, char **);
static void	config_window_decorations(int, char **);

static void	config_frame_gap(int, char **);
static void	config_frame_bar(int, char **);
static void	config_frame_count(int, char **);
static void	config_frame_width(int, char **);
static void	config_frame_height(int, char **);
static void	config_frame_offset(int, char **);
static void	config_frame_border(int, char **);
static void	config_frame_layout(int, char **);
static void	config_frame_create(int, char **);

static int		config_args(int, int);
static void		config_parse(const char *);
static void		config_fatal(const char *, const char *, ...);

static char		*config_read_line(FILE *, char *, size_t);
static long long	config_strtonum(const char *, const char *, int,
			    long long, long long);

struct {
	const char		*name;
	int			args;
	void			(*cb)(int, char **);
} keywords[] = {
	{ "font",			1,	config_font },
	{ "bind",			-2,	config_bind },
	{ "color",			2,	config_color },
	{ "shell",			1,	config_shell },
	{ "prefix",			1,	config_prefix },
	{ "scrollback",			1,	config_scrollback },
	{ "screen-width",		1,	config_screen_width },
	{ "screen-height",		1,	config_screen_height },
	{ "terminal-font",		1,	config_term_font },
	{ "terminal-name",		1,	config_term_name },
	{ "terminal-font-bold",		1,	config_term_font_bold },
	{ "window-decorations",		1,	config_window_decorations },

	{ "frame-gap",			1,	config_frame_gap },
	{ "frame-bar",			1,	config_frame_bar },
	{ "frame-count",		1,	config_frame_count },
	{ "frame-width",		1,	config_frame_width },
	{ "frame-height",		1,	config_frame_height },
	{ "frame-offset",		1,	config_frame_offset },
	{ "frame-border",		1,	config_frame_border },
	{ "frame-layout",		1,	config_frame_layout },
	{ "frame-create",		4,	config_frame_create },

	{ NULL, 0, NULL }
};

struct {
	const char	*mod;
	u_int32_t	mask;
} modmasks[] = {
	{ "C",		COMA_MOD_CTRL },
	{ "S",		COMA_MOD_SHIFT },
	{ "M",		COMA_MOD_ALT },
	{ "M2",		COMA_MOD_NUM },
	{ "M3",		COMA_MOD_MODE },
	{ "M4",		COMA_MOD_SUPER },
	{ NULL,		0 }
};

static int	config_line = 1;

void
coma_config_parse(const char *cpath)
{
	int			len;
	struct passwd		*pw;
	char			path[PATH_MAX];

	if (cpath == NULL) {
		if ((pw = getpwuid(getuid())) == NULL)
			fatal("getpwuid(): %s", errno_s);

		len = snprintf(path, sizeof(path), "%s/.comarc", pw->pw_dir);
		if (len == -1 || (size_t)len >= sizeof(path))
			fatal("failed to create path to config file");

		cpath = path;
	}

	config_parse(cpath);
}

static void
config_parse(const char *path)
{
	FILE		*fp;
	int		i, argc;
	char		*line, buf[CONFIG_LINE_MAX], *argv[CONFIG_ARGV_MAX];

	if ((fp = fopen(path, "r")) == NULL)
		return;

	while ((line = config_read_line(fp, buf, sizeof(buf))) != NULL) {
		argc = coma_split_string(line, " ", argv, CONFIG_ARGV_MAX);

		if (argc < 2) {
			config_line++;
			continue;
		}

		for (i = 0; keywords[i].name != NULL; i++) {
			if (!strcmp(argv[0], keywords[i].name)) {
				coma_log("got '%s' with %d", argv[0], argc - 1);
				if (config_args(keywords[i].args,
				    argc - 1) == -1) {
					config_fatal(argv[0],
					    "requires %d args, got %d",
					    abs(keywords[i].args), argc - 1);
				} else {
					keywords[i].cb(argc, argv);
				}
			}
		}

		config_line++;
	}

	(void)fclose(fp);
}

static void
config_fatal(const char *kw, const char *fmt, ...)
{
	va_list		args;

	fprintf(stderr, "config error on line %d for keyword '%s': ",
	    config_line, kw);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");

	exit(1);
}

static int
config_args(int want, int have)
{
	if (want < 0 && have >= -want)
		return (0);

	if (want == have)
		return (0);

	return (-1);
}

static void
config_bind(int argc, char **argv)
{
	int		i;
	u_int32_t	sym;
	char		action[CONFIG_LINE_MAX];

	if ((sym = coma_wm_key_parse(argv[argc - 1])) == 0)
		config_fatal(argv[0], "invalid key '%s'", argv[argc - 1]);

	action[0] = '\0';

	for (i = 1; i < argc - 1; i++) {
		if (i > 1)
			(void)strlcat(action, " ", sizeof(action));
		if (strlcat(action, argv[i], sizeof(action)) >= sizeof(action))
			config_fatal(argv[0], "action is too long");
	}

	if (coma_wm_register_action(action, sym) == -1)
		config_fatal(argv[0], "unknown action '%s'", action);
}

static void
config_font(int argc, char **argv)
{
	free(font_name);

	if ((font_name = strdup(argv[1])) == NULL)
		fatal("strdup");
}

static void
config_color(int argc, char **argv)
{
	int		valid;
	char		*color, *p, *c;

	if (*argv[2] != '"')
		config_fatal(argv[0], "missing beginning '\"'");

	color = argv[2] + 1;

	if ((p = strchr(color, '"')) == NULL)
		config_fatal(argv[0], "missing ending '\"'");
	*p = '\0';

	if (*color != '#')
		config_fatal(argv[0], "missing '#' in rgb color '%s'", color);

	if (strlen(color) != 7)
		config_fatal(argv[0], "invalid rgb color '%s'", color);

	for (c = color + 1; *c != '\0'; c++) {
		valid = 0;
		if (!isdigit(*(unsigned char *)c)) {
			valid |= (*c >= 'a' && *c <= 'f');
			valid |= (*c >= 'A' && *c <= 'F');
			if (valid == 0) {
				config_fatal(argv[0],
				    "invalid rgb color '%s'", color);
			}
		}
	}

	if (coma_wm_register_color(argv[1], color) == -1)
		config_fatal(argv[0], "unknown color '%s'", argv[1]);
}

static void
config_prefix(int argc, char **argv)
{
	int		i;
	char		*mod, *key;

	mod = argv[1];

	if ((key = strchr(mod, '-')) == NULL)
		config_fatal(argv[0], "missing '-' in prefix key");

	*(key)++ = '\0';

	if (*mod == '\0')
		config_fatal(argv[0], "missing mod value before '-'");

	if (*key == '\0')
		config_fatal(argv[0], "missing key value after '-'");

	if ((prefix_key = coma_wm_key_parse(key)) == 0)
		config_fatal(argv[0], "invalid key '%s'", key);

	for (i = 0; modmasks[i].mod != NULL; i++) {
		if (!strcmp(mod, modmasks[i].mod)) {
			prefix_mod = modmasks[i].mask;
			break;
		}
	}

	if (modmasks[i].mask == 0)
		config_fatal(argv[0], "invalid mod key '%s'", mod);
}

static void
config_shell(int argc, char **argv)
{
	free(shell);

	if ((shell = strdup(argv[1])) == NULL)
		fatal("strdup");
}

static void
config_term_font(int argc, char **argv)
{
	free(term_font_name);

	if ((term_font_name = strdup(argv[1])) == NULL)
		fatal("strdup");
}

static void
config_term_name(int argc, char **argv)
{
	free(term_name);

	if ((term_name = strdup(argv[1])) == NULL)
		fatal("strdup");
}

static void
config_scrollback(int argc, char **argv)
{
	scrollback_lines = config_strtonum(argv[0], argv[1], 10, 0, 1000000);
}

static void
config_screen_width(int argc, char **argv)
{
	screen_width = config_strtonum(argv[0], argv[1], 10, 1, USHRT_MAX);
}

static void
config_screen_height(int argc, char **argv)
{
	screen_height = config_strtonum(argv[0], argv[1], 10, 1, USHRT_MAX);
}

static void
config_term_font_bold(int argc, char **argv)
{
	free(term_font_bold_name);

	if ((term_font_bold_name = strdup(argv[1])) == NULL)
		fatal("strdup");
}

static void
config_window_decorations(int argc, char **argv)
{
	if (!strcmp(argv[1], "yes"))
		window_decorations = 1;
	else if (!strcmp(argv[1], "no"))
		window_decorations = 0;
	else
		config_fatal(argv[0], "expected yes or no, got '%s'", argv[1]);
}

static void
config_frame_bar(int argc, char **argv)
{
	frame_bar = config_strtonum(argv[0], argv[1], 10, 0, USHRT_MAX);
}

static void
config_frame_gap(int argc, char **argv)
{
	frame_gap = config_strtonum(argv[0], argv[1], 10, 0, USHRT_MAX);
}

static void
config_frame_count(int argc, char **argv)
{
	frame_count = config_strtonum(argv[0], argv[1], 10, 1, INT_MAX);
}

static void
config_frame_width(int argc, char **argv)
{
	frame_width = config_strtonum(argv[0], argv[1], 10, 1, USHRT_MAX);
}

static void
config_frame_height(int argc, char **argv)
{
	frame_height = config_strtonum(argv[0], argv[1], 10, 1, USHRT_MAX);
}

static void
config_frame_offset(int argc, char **argv)
{
	frame_offset = config_strtonum(argv[0], argv[1], 10, 0, USHRT_MAX);
}

static void
config_frame_border(int argc, char **argv)
{
	frame_border = config_strtonum(argv[0], argv[1], 10, 0, USHRT_MAX);
}

static void
config_frame_layout(int argc, char **argv)
{
	coma_frame_layout(argv[1]);
}

static void
config_frame_create(int argc, char **argv)
{
	u_int16_t	x, y, w, h;

	x = config_strtonum(argv[0], argv[1], 10, 0, USHRT_MAX);
	y = config_strtonum(argv[0], argv[2], 10, 0, USHRT_MAX);
	w = config_strtonum(argv[0], argv[3], 10, 0, USHRT_MAX);
	h = config_strtonum(argv[0], argv[4], 10, 0, USHRT_MAX);

	coma_frame_config_create(x, y, w, h);
}

static char *
config_read_line(FILE *fp, char *in, size_t len)
{
	char	*p, *t;

	if (fgets(in, len, fp) == NULL)
		return (NULL);

	if (strchr(in, '\n') == NULL && !feof(fp)) {
		fatal("config line %d is longer than %zu bytes",
		    config_line, len);
	}

	p = in;
	in[strcspn(in, "\n")] = '\0';

	while (isspace(*(unsigned char *)p))
		p++;

	if (p[0] == '#' || p[0] == '\0') {
		p[0] = '\0';
		return (p);
	}

	for (t = p; *t != '\0'; t++) {
		if (*t == '\t')
			*t = ' ';
	}

	return (p);
}

static long long
config_strtonum(const char *kw, const char *str, int base,
    long long min, long long max)
{
	long long	l;
	char		*ep;

	if (min > max)
		config_fatal(kw, "min > max");

	errno = 0;
	l = strtoll(str, &ep, base);
	if (errno != 0 || str == ep || *ep != '\0')
		config_fatal(kw, "'%s' is not a valid integer", str);

	if (l < min)
		config_fatal(kw, "'%s' is too low", str);

	if (l > max)
		config_fatal(kw, "'%s' is too high", str);

	return (l);
}
