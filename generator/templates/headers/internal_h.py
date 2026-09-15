"""Renders src/internal.h - private types shared across emuroot's TUs.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header

PATH = "src/internal.h"


def render(p: Project) -> str:
    banner = c_header(p, "internal.h - private types shared across emuroot's translation units.")
    return f"""{banner}
#ifndef EMUROOT_INTERNAL_H
#define EMUROOT_INTERNAL_H

#include "emuroot.h"
#include <sys/types.h>

#define EMUROOT_MAX_BINDINGS 128
#define EMUROOT_PATH_MAX 4096

typedef struct {{
    char host[EMUROOT_PATH_MAX];
    char guest[EMUROOT_PATH_MAX];
    size_t guest_len;
}} binding_t;

struct emuroot_ctx {{
    char root[EMUROOT_PATH_MAX];   /* "" means "/" i.e. no rootfs redirect */
    binding_t bindings[EMUROOT_MAX_BINDINGS];
    size_t nbindings;

    int has_fake_ids;
    unsigned int fake_uid;
    unsigned int fake_gid;

    int has_fake_uname;
    char uname_sysname[65];
    char uname_release[65];
    char uname_machine[65];

    int verbose;
    pid_t child_pid;
}};

/* path.c: translate a guest-visible absolute/relative path to a host path
 * given the context's root + bindings. cwd is the guest's current
 * directory (needed to resolve relative paths); result written to out
 * (size EMUROOT_PATH_MAX). Returns 1 if the path was rewritten, 0 if left
 * unchanged, -1 on error. */
int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *cwd,
                            const char *guest_path, char *out, size_t outsz);

/* bindings.c helpers */
int emuroot_bindings_add(struct emuroot_ctx *ctx, const char *host, const char *guest);

/* ptrace_engine.c: the Linux syscall-interception backend. */
int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);

/* envonly_engine.c: best-effort fallback backend for platforms without
 * ptrace/process_vm_* (e.g. restricted sandboxes, non-Linux Unix). */
int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);

#endif
"""
