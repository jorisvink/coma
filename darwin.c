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

#include <fcntl.h>
#include <sandbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coma.h"

#define COMA_SANDBOX_FILE	"/usr/local/share/coma/sb/coma.sb"

int	sandbox_init_with_parameters(const char *profile,
	    uint64_t flags, const char *const parameters[], char **errorbuf);

void
coma_platform_sandbox(void)
{
	int		fd;
	struct stat	st;
	ssize_t		ret;
	size_t		flen;
	char		*errmsg;
	char		*profile;

	if ((fd = open(COMA_SANDBOX_FILE, O_RDONLY)) == -1)
		fatal("failed to open sandbox profile '%s'", COMA_SANDBOX_FILE);

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
