"""Renders src/emuroot_api.c - implementation of the public libemuroot API.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/emuroot_api.c"

_BODY = r"""
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

emuroot_ctx_t *emuroot_create(void)
{
    emuroot_ctx_t *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void emuroot_destroy(emuroot_ctx_t *ctx)
{
    free(ctx);
}

int emuroot_set_root(emuroot_ctx_t *ctx, const char *new_root)
{
    if (!ctx || !new_root) return EMUROOT_ERR_INVAL;
    if (strlen(new_root) >= EMUROOT_PATH_MAX) return EMUROOT_ERR_INVAL;
    strncpy(ctx->root, new_root, EMUROOT_PATH_MAX - 1);
    ctx->root[EMUROOT_PATH_MAX - 1] = '\0';
    /* strip trailing slash so "root + norm_path" doesn't double it up */
    size_t len = strlen(ctx->root);
    if (len > 1 && ctx->root[len - 1] == '/')
        ctx->root[len - 1] = '\0';
    return EMUROOT_OK;
}

int emuroot_bind(emuroot_ctx_t *ctx, const char *host_path, const char *guest_path)
{
    if (!ctx) return EMUROOT_ERR_INVAL;
    return emuroot_bindings_add(ctx, host_path, guest_path);
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
    if (sysname) strncpy(ctx->uname_sysname, sysname, sizeof(ctx->uname_sysname) - 1);
    if (release) strncpy(ctx->uname_release, release, sizeof(ctx->uname_release) - 1);
    if (machine) strncpy(ctx->uname_machine, machine, sizeof(ctx->uname_machine) - 1);
    return EMUROOT_OK;
}

int emuroot_set_verbose(emuroot_ctx_t *ctx, int enable)
{
    if (!ctx) return EMUROOT_ERR_INVAL;
    ctx->verbose = enable;
    return EMUROOT_OK;
}

int emuroot_run(emuroot_ctx_t *ctx, char *const argv[], char *const envp[])
{
    if (!ctx || !argv || !argv[0]) return EMUROOT_ERR_INVAL;

    emuroot_backend_t backend = emuroot_probe_backend();
    if (ctx->verbose)
        fprintf(stderr, "[emuroot] backend: %s\n", emuroot_backend_name(backend));

    switch (backend) {
        case EMUROOT_BACKEND_PTRACE:
            return emuroot_run_ptrace(ctx, argv, envp);
        case EMUROOT_BACKEND_ENVONLY:
            return emuroot_run_envonly(ctx, argv, envp);
        default:
            return EMUROOT_ERR_UNSUPPORTED;
    }
}

const char *emuroot_strerror(int status)
{
    switch (status) {
        case EMUROOT_OK: return "success";
        case EMUROOT_ERR_NOMEM: return "out of memory / binding table full";
        case EMUROOT_ERR_INVAL: return "invalid argument";
        case EMUROOT_ERR_UNSUPPORTED: return "unsupported on this platform";
        case EMUROOT_ERR_SYSTEM: return "system call failed (see errno)";
        case EMUROOT_ERR_NOT_FOUND: return "not found";
        default: return "unknown error";
    }
}

void emuroot_version_string(char *buf, size_t len)
{
    snprintf(buf, len, "emuroot %d.%d.%d", EMUROOT_VERSION_MAJOR,
             EMUROOT_VERSION_MINOR, EMUROOT_VERSION_PATCH);
}
"""


def render(p: Project) -> str:
    banner = c_header(p, "emuroot_api.c - implementation of the public libemuroot API.")
    return (banner + "\n" + _BODY).replace('"emuroot %d.%d.%d"', f'"{p.name} %d.%d.%d"')
