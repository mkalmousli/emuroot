/* Fallback backend for platforms without ptrace: no interception, just
 * chdir into the root and export EMUROOT_* for cooperating guests. */
#include "internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[])
{
    fprintf(stderr, "[emuroot] warning: ptrace unavailable; falling back to "
                    "best-effort env-only mode (no syscall virtualization).\n");

    static char bindings[MAX_BINDINGS * (2 * PATH_LEN + 2)];
    size_t pos = 0;
    for (size_t i = 0; i < ctx->nbindings && pos < sizeof bindings; i++)
        pos += snprintf(bindings + pos, sizeof bindings - pos, "%s%s:%s",
                        pos ? ";" : "", ctx->bindings[i].host, ctx->bindings[i].guest);

    if (ctx->root[0]) setenv("EMUROOT_ROOT", ctx->root, 1);
    if (pos)          setenv("EMUROOT_BINDINGS", bindings, 1);
    if (ctx->has_fake_ids) {
        char ids[32];
        snprintf(ids, sizeof ids, "%u:%u", ctx->fake_uid, ctx->fake_gid);
        setenv("EMUROOT_FAKE_IDS", ids, 1);
    }
    if (ctx->root[0] && chdir(ctx->root) != 0)
        fprintf(stderr, "[emuroot] warning: chdir(%s) failed: %s\n", ctx->root, strerror(errno));

    pid_t pid = fork();
    if (pid < 0) return EMUROOT_ERR_SYSTEM;
    if (pid == 0) {
        if (envp) execve(argv[0], argv, envp);
        else      execv(argv[0], argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return EMUROOT_ERR_SYSTEM;
}
