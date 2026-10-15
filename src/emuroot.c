/* Public API: context setup, backend selection, run. */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef EMUROOT_NO_PTRACE               /* non-Linux Unixes build without the engine */
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#endif

emuroot_ctx_t *emuroot_create(void) { return calloc(1, sizeof(struct emuroot_ctx)); }
void emuroot_destroy(emuroot_ctx_t *ctx) { free(ctx); }

static int copy_path(char *dst, const char *src)
{
    if (strlen(src) >= PATH_LEN) return EMUROOT_ERR_INVAL;
    strcpy(dst, src);
    return EMUROOT_OK;
}

int emuroot_set_root(emuroot_ctx_t *ctx, const char *new_root)
{
    if (!ctx || !new_root || copy_path(ctx->root, new_root)) return EMUROOT_ERR_INVAL;
    size_t len = strlen(ctx->root);
    if (len > 1 && ctx->root[len - 1] == '/')   /* so root + "/x" never doubles the slash */
        ctx->root[len - 1] = '\0';
    return EMUROOT_OK;
}

int emuroot_bind(emuroot_ctx_t *ctx, const char *host, const char *guest)
{
    if (!ctx || !host || !guest) return EMUROOT_ERR_INVAL;
    if (ctx->nbindings >= MAX_BINDINGS) return EMUROOT_ERR_NOMEM;
    binding_t *b = &ctx->bindings[ctx->nbindings];
    if (copy_path(b->host, host) || copy_path(b->guest, guest)) return EMUROOT_ERR_INVAL;
    b->guest_len = strlen(b->guest);
    ctx->nbindings++;
    return EMUROOT_OK;
}

int emuroot_set_fake_ids(emuroot_ctx_t *ctx, unsigned int uid, unsigned int gid)
{
    if (!ctx) return EMUROOT_ERR_INVAL;
    ctx->has_fake_ids = 1;
    ctx->fake_uid = uid;
    ctx->fake_gid = gid;
    return EMUROOT_OK;
}

int emuroot_set_fake_uname(emuroot_ctx_t *ctx, const char *sysname,
                           const char *release, const char *machine)
{
    if (!ctx) return EMUROOT_ERR_INVAL;
    ctx->has_fake_uname = 1;
    snprintf(ctx->uname_sysname, sizeof ctx->uname_sysname, "%s", sysname ? sysname : "");
    snprintf(ctx->uname_release, sizeof ctx->uname_release, "%s", release ? release : "");
    snprintf(ctx->uname_machine, sizeof ctx->uname_machine, "%s", machine ? machine : "");
    return EMUROOT_OK;
}

int emuroot_set_verbose(emuroot_ctx_t *ctx, int enable)
{
    if (!ctx) return EMUROOT_ERR_INVAL;
    ctx->verbose = enable;
    return EMUROOT_OK;
}

/* ptrace is usable iff a forked child can PTRACE_TRACEME and stop.
 * EMUROOT_BACKEND=envonly forces the fallback (for testing it). */
emuroot_backend_t emuroot_probe_backend(void)
{
#ifdef EMUROOT_NO_PTRACE
    return EMUROOT_BACKEND_ENVONLY;
#else
    const char *forced = getenv("EMUROOT_BACKEND");
    if (forced && strcmp(forced, "envonly") == 0) return EMUROOT_BACKEND_ENVONLY;

    pid_t pid = fork();
    if (pid < 0) return EMUROOT_BACKEND_ENVONLY;
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) _exit(1);
        raise(SIGSTOP);
        _exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    int ok = WIFSTOPPED(status);
    if (ok) ptrace(PTRACE_KILL, pid, NULL, NULL);
    waitpid(pid, &status, 0);
    return ok ? EMUROOT_BACKEND_PTRACE : EMUROOT_BACKEND_ENVONLY;
#endif
}

const char *emuroot_backend_name(emuroot_backend_t backend)
{
    switch (backend) {
    case EMUROOT_BACKEND_PTRACE:  return "ptrace";
    case EMUROOT_BACKEND_ENVONLY: return "envonly";
    default:                      return "none";
    }
}

int emuroot_run(emuroot_ctx_t *ctx, char *const argv[], char *const envp[])
{
    if (!ctx || !argv || !argv[0]) return EMUROOT_ERR_INVAL;

    emuroot_backend_t backend = emuroot_probe_backend();
    if (ctx->verbose)
        fprintf(stderr, "[emuroot] backend: %s\n", emuroot_backend_name(backend));
#ifndef EMUROOT_NO_PTRACE
    if (backend == EMUROOT_BACKEND_PTRACE) return emuroot_run_ptrace(ctx, argv, envp);
#endif
    return emuroot_run_envonly(ctx, argv, envp);
}

const char *emuroot_strerror(int status)
{
    switch (status) {
    case EMUROOT_OK:              return "success";
    case EMUROOT_ERR_NOMEM:       return "out of memory / binding table full";
    case EMUROOT_ERR_INVAL:       return "invalid argument";
    case EMUROOT_ERR_UNSUPPORTED: return "unsupported on this platform";
    case EMUROOT_ERR_SYSTEM:      return "system call failed (see errno)";
    case EMUROOT_ERR_NOT_FOUND:   return "not found";
    default:                      return "unknown error";
    }
}

void emuroot_version_string(char *buf, size_t len)
{
    snprintf(buf, len, "emuroot %d.%d.%d", EMUROOT_VERSION_MAJOR,
             EMUROOT_VERSION_MINOR, EMUROOT_VERSION_PATCH);
}
