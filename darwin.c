/*
 * Copyright (c) 2026 Joris Vink <joris@sanctorum.se>
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
#include <sys/stat.h>

#include <mach-o/dyld.h>

#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <sandbox.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coma.h"

#define COMA_SANDBOX_FILE	"/usr/local/share/coma/sb/coma.sb"

static int	coma_sandbox_open(void);

int		sandbox_init_with_parameters(const char *,
		    uint64_t, const char *const [], char **);

void
coma_platform_sandbox(void)
{
	int		fd;
	struct stat	st;
	ssize_t		ret;
	size_t		flen;
	char		*errmsg;
	char		*profile;

	if ((fd = coma_sandbox_open()) == -1) {
		fatal("failed to open sandbox profile (tried app bundle "
		    "and '%s')", COMA_SANDBOX_FILE);
	}

	if (fstat(fd, &st) == -1)
		fatal("fstat(%s): %s", COMA_SANDBOX_FILE, errno_s);

	if (st.st_size < 0 || st.st_size > 1024 * 1024)
		fatal("sandbox profile filesize is weird");

	flen = (size_t)st.st_size;

	if ((profile = calloc(1, flen + 1)) == NULL)
		fatal("calloc failed");

	if ((ret = read(fd, profile, flen)) == -1)
		fatal("read(%s): %s", COMA_SANDBOX_FILE, errno_s);

	if (ret != st.st_size)
		fatal("failed to read all of sandbox profile");

	profile[flen] = '\0';

	if (sandbox_init_with_parameters(profile, 0, NULL, &errmsg) == -1)
		fatal("sandbox init: %s", errmsg);

	free(profile);
	(void)close(fd);
}

static int
coma_sandbox_open(void)
{
	char		*dir;
	uint32_t	size;
	int		fd, len;
	char		exe[PATH_MAX], real[PATH_MAX], path[PATH_MAX];

	size = sizeof(exe);

	if (_NSGetExecutablePath(exe, &size) == 0 &&
	    realpath(exe, real) != NULL) {
		dir = dirname(real);

		len = snprintf(path, sizeof(path),
		    "%s/../Resources/coma.sb", dir);
		if (len < 0 || (size_t)len >= sizeof(path))
			fatal("failed to obtain path to sb");

		if ((fd = open(path, O_RDONLY)) != -1)
			return (fd);
	}

	return (open(COMA_SANDBOX_FILE, O_RDONLY));
}
