"""Renders src/regs_io.h.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/regs_io.h"

_BODY = r"""
#ifndef EMUROOT_REGS_IO_H
#define EMUROOT_REGS_IO_H

#include "arch.h"
#include <sys/ptrace.h>
#include <sys/uio.h>

static inline int regs_get(pid_t pid, emuroot_regs_t *regs)
{
    struct iovec iov = { .iov_base = regs, .iov_len = sizeof(*regs) };
    return ptrace(PTRACE_GETREGSET, pid, (void *)1 /* NT_PRSTATUS */, &iov);
}

static inline int regs_set(pid_t pid, emuroot_regs_t *regs)
{
    struct iovec iov = { .iov_base = regs, .iov_len = sizeof(*regs) };
    return ptrace(PTRACE_SETREGSET, pid, (void *)1 /* NT_PRSTATUS */, &iov);
}

#endif
"""


def render(p: Project) -> str:
    banner = c_header(p, "regs_io.h - portable get/set of tracee registers across architectures.", (
        "Always goes through PTRACE_GETREGSET/SETREGSET with NT_PRSTATUS(1) and\n"
        "iov_len == sizeof(emuroot_regs_t). The kernel bounds every copy to\n"
        "min(iov_len, actual regset size), so this is safe even though\n"
        "emuroot_regs_t only declares a prefix of each architecture's real\n"
        "register set (see the comment atop arch.h)."
    ))
    return banner + "\n" + _BODY
