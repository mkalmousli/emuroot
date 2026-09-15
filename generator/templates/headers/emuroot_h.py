"""Renders include/emuroot.h - the public libemuroot API.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "include/emuroot.h"


def render(p: Project) -> str:
    banner = c_header(p, f"{p.name}.h - public API for lib{p.name}", (
        f"{p.name} is an independent, from-scratch user-space \"fake root\" /\n"
        "filesystem-virtualization engine. It emulates a chroot-like environment\n"
        "(rootfs redirection, path bindings, fake uid/gid, faked syscalls that\n"
        "are unavailable or unprivileged on the host) without requiring root\n"
        "privileges, kernel modules, or containers."
    ))
    major, minor, patch = p.version
    return f"""{banner}

#ifndef EMUROOT_H
#define EMUROOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define EMUROOT_VERSION_MAJOR {major}
#define EMUROOT_VERSION_MINOR {minor}
#define EMUROOT_VERSION_PATCH {patch}

typedef struct emuroot_ctx emuroot_ctx_t;

typedef enum {{
    EMUROOT_OK = 0,
    EMUROOT_ERR_NOMEM = -1,
    EMUROOT_ERR_INVAL = -2,
    EMUROOT_ERR_UNSUPPORTED = -3,   /* platform lacks required primitives */
    EMUROOT_ERR_SYSTEM = -4,        /* see errno */
    EMUROOT_ERR_NOT_FOUND = -5,
}} emuroot_status_t;

/* Backend actually selected at runtime for this platform. */
typedef enum {{
    EMUROOT_BACKEND_NONE = 0,
    EMUROOT_BACKEND_PTRACE,   /* full syscall interception (Linux) */
    EMUROOT_BACKEND_ENVONLY,  /* best-effort: env + chdir, no interception */
}} emuroot_backend_t;

/* Create / destroy a session context. */
emuroot_ctx_t *emuroot_create(void);
void emuroot_destroy(emuroot_ctx_t *ctx);

/* Report which backend this platform/build will actually use. */
emuroot_backend_t emuroot_probe_backend(void);
const char *emuroot_backend_name(emuroot_backend_t backend);

/*
 * Set the emulated filesystem root. Any absolute path the guest process
 * resolves that is not covered by a more specific binding is rewritten
 * to be relative to new_root, exactly like chroot(2) but without needing
 * privileges.
 */
int emuroot_set_root(emuroot_ctx_t *ctx, const char *new_root);

/*
 * Bind a host path onto a guest (emulated) path, e.g.
 *   emuroot_bind(ctx, "/dev", "/dev")
 *   emuroot_bind(ctx, "/home/user/project", "/root/project")
 * Later bindings take precedence over earlier, more general ones.
 */
int emuroot_bind(emuroot_ctx_t *ctx, const char *host_path, const char *guest_path);

/* Fake the uid/gid reported to the guest process (does not grant privilege). */
int emuroot_set_fake_ids(emuroot_ctx_t *ctx, unsigned int uid, unsigned int gid);

/* Fake uname() machine/sysname/release strings reported to the guest. */
int emuroot_set_fake_uname(emuroot_ctx_t *ctx, const char *sysname,
                            const char *release, const char *machine);

/* Enable/disable verbose syscall tracing to stderr (debugging). */
int emuroot_set_verbose(emuroot_ctx_t *ctx, int enable);

/*
 * Run argv[0] inside the emulated environment, replacing bindings/root as
 * configured. envp may be NULL to inherit the current environment.
 * Blocks until the guest exits; returns its exit status (>=0) or a
 * negative emuroot_status_t on failure to even start it.
 */
int emuroot_run(emuroot_ctx_t *ctx, char *const argv[], char *const envp[]);

const char *emuroot_strerror(int status);
void emuroot_version_string(char *buf, size_t len);

#ifdef __cplusplus
}}
#endif

#endif /* EMUROOT_H */
"""
