/* Internal types shared by emuroot's sources. */
#ifndef EMUROOT_INTERNAL_H
#define EMUROOT_INTERNAL_H

#include "emuroot.h"
#include <sys/types.h>

#define MAX_BINDINGS 128
#define PATH_LEN 4096

typedef struct {
    char host[PATH_LEN];
    char guest[PATH_LEN];
    size_t guest_len;
} binding_t;

struct emuroot_ctx {
    char root[PATH_LEN];            /* "" = no root redirect */
    binding_t bindings[MAX_BINDINGS];
    size_t nbindings;

    int has_fake_ids;
    unsigned int fake_uid, fake_gid;

    int has_fake_uname;
    char uname_sysname[65], uname_release[65], uname_machine[65];

    int verbose;
};

/* path.c: map an absolute guest path to a host path via root + bindings.
 * Returns 1 if rewritten; 0 if left as is (relative paths always are: the
 * guest runs with its working directory already inside the root). */
int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *guest_path,
                           char *out, size_t outsz);

/* Backends. */
int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);
int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);

#endif
