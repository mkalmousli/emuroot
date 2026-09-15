"""Renders src/envonly_engine.c - the best-effort fallback backend.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/envonly_engine.c"

_BODY = r"""
#include "internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[])
{
    fprintf(stderr,
        "[emuroot] warning: ptrace-based interception unavailable on this "
        "platform; falling back to best-effort env-only mode (no true "
        "syscall virtualization).\n");

    char bindings_buf[EMUROOT_MAX_BINDINGS * (EMUROOT_PATH_MAX + 1) * 2];
    size_t pos = 0;
    bindings_buf[0] = '\0';
    for (size_t i = 0; i < ctx->nbindings; i++) {
        int n = snprintf(bindings_buf + pos, sizeof(bindings_buf) - pos,
                          "%s%s:%s", pos ? ";" : "",
                          ctx->bindings[i].host, ctx->bindings[i].guest);
        if (n < 0) break;
        pos += (size_t)n;
        if (pos >= sizeof(bindings_buf)) break;
    }

    if (ctx->root[0])
        setenv("EMUROOT_ROOT", ctx->root, 1);
    if (bindings_buf[0])
        setenv("EMUROOT_BINDINGS", bindings_buf, 1);
    if (ctx->has_fake_ids) {
        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%u:%u", ctx->fake_uid, ctx->fake_gid);
        setenv("EMUROOT_FAKE_IDS", idbuf, 1);
    }

    if (ctx->root[0] && chdir(ctx->root) != 0)
        fprintf(stderr, "[emuroot] warning: chdir(%s) failed: %s\n",
                ctx->root, strerror(errno));

    pid_t pid = fork();
    if (pid < 0)
        return EMUROOT_ERR_SYSTEM;
    if (pid == 0) {
        if (envp)
            execve(argv[0], argv, envp);
        else
            execv(argv[0], argv);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return EMUROOT_ERR_SYSTEM;
}
"""


def render(p: Project) -> str:
    banner = c_header(p, "envonly_engine.c - best-effort fallback backend.", (
        "Used when the host has no working ptrace/process_vm_* (some hardened\n"
        "kernels, containers without CAP_SYS_PTRACE, or a non-Linux Unix this\n"
        "build was ported to). It cannot intercept individual syscalls, so it\n"
        "cannot truly virtualize arbitrary paths; instead it does the best\n"
        "approximation available without privileges:\n"
        "  - chdir() into the mapped root/binding for \"/\" if one is configured\n"
        "  - export EMUROOT_ROOT / EMUROOT_BINDINGS in the environment so a\n"
        "    cooperating guest (or a libemuroot-aware libc shim, see docs/)\n"
        "    can honor them voluntarily."
    ))
    return banner + "\n" + _BODY
