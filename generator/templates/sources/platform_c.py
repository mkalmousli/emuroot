"""Renders src/platform.c - runtime capability probing.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/platform.c"

_BODY = r"""
#include "internal.h"

#if defined(__linux__)
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

emuroot_backend_t emuroot_probe_backend(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return EMUROOT_BACKEND_ENVONLY;

    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
            _exit(1);
        raise(SIGSTOP);
        _exit(0);
    }

    int status;
    waitpid(pid, &status, 0);
    int ok = WIFSTOPPED(status);
    if (ok) {
        ptrace(PTRACE_KILL, pid, NULL, NULL);
    }
    waitpid(pid, &status, 0);
    return ok ? EMUROOT_BACKEND_PTRACE : EMUROOT_BACKEND_ENVONLY;
}
#else
emuroot_backend_t emuroot_probe_backend(void)
{
    return EMUROOT_BACKEND_ENVONLY;
}
#endif

const char *emuroot_backend_name(emuroot_backend_t backend)
{
    switch (backend) {
        case EMUROOT_BACKEND_PTRACE:  return "ptrace";
        case EMUROOT_BACKEND_ENVONLY: return "envonly";
        default: return "none";
    }
}
"""


def render(p: Project) -> str:
    banner = c_header(p, "platform.c - runtime capability probing.", (
        "The ptrace backend is a Linux-only ABI regardless of CPU architecture,\n"
        "so the only guard needed here is __linux__ - build.py already only\n"
        "ever generates C source for an architecture it has a register\n"
        "descriptor for (see generator/spec/architectures.py), so unlike an older\n"
        "revision of this file there's no need to re-check the CPU architecture\n"
        "here too."
    ))
    return banner + "\n" + _BODY
