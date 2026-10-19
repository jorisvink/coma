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
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <linux/filter.h>
#include <linux/seccomp.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "coma.h"
#include "seccomp.h"

static struct sock_filter seccomp_filter[] = {
	KORE_BPF_LOAD(arch, 0),
	KORE_BPF_CMP(AUDIT_ARCH_X86_64, 1, 0),
	KORE_BPF_RET(SECCOMP_RET_KILL),
	KORE_BPF_LOAD(nr, 0),

	KORE_SYSCALL_DENY(socket, EPERM),
	KORE_SYSCALL_DENY(io_uring_setup, EPERM),

	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
};

void
coma_platform_sandbox(void)
{
	struct sock_fprog		prog;

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
		fatal("prctl(privs): %s", errno_s);

	prog.filter = seccomp_filter;
	prog.len = KORE_FILTER_LEN(seccomp_filter);

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == -1)
		fatal("prctl(seccomp): %s", errno_s);
}
