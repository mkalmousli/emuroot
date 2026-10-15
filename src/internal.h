/* Internal types shared by emuroot's sources. */
#ifndef EMUROOT_INTERNAL_H
#define EMUROOT_INTERNAL_H

#include "emuroot.h"
#include <stdint.h>
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

/* creds.c: a process's fake credentials. uid/gid arrays are {real, effective, saved}. */
#define CREDS_MAX_GROUPS 32
typedef struct {
    uint32_t uid[3], gid[3], fsuid, fsgid;
    uint32_t groups[CREDS_MAX_GROUPS];
    int ngroups;
} creds_t;

void creds_init(creds_t *c, uint32_t uid, uint32_t gid);
/* Each set* returns 0, or -errno as the real call would. setfs* return the old id. */
long creds_setuid(creds_t *c, uint32_t u);
long creds_setgid(creds_t *c, uint32_t g);
long creds_setresuid(creds_t *c, uint32_t r, uint32_t e, uint32_t s);
long creds_setresgid(creds_t *c, uint32_t r, uint32_t e, uint32_t s);
long creds_setreuid(creds_t *c, uint32_t r, uint32_t e);
long creds_setregid(creds_t *c, uint32_t r, uint32_t e);
long creds_setgroups(creds_t *c, const uint32_t *list, long n);
long creds_setfsuid(creds_t *c, uint32_t u);
long creds_setfsgid(creds_t *c, uint32_t g);

/* path.c: map an absolute guest path to a host path via root, bindings and
 * symlinks. follow_last=0 leaves a symlink in the last component alone
 * (lstat, unlink, ...). Relative paths are never rewritten: the guest runs
 * with its working directory already inside the root. Returns 1 if the
 * result differs from the input. */
int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *guest_path,
                           int follow_last, char *out, size_t outsz);

/* Inverse, for paths reported back to the guest (getcwd). Returns 1 if
 * `host` lies under the root or a binding. */
int emuroot_host_to_guest(const struct emuroot_ctx *ctx, const char *host,
                          char *out, size_t outsz);

/* seccomp.c: the fast path (see there). */
int emuroot_seccomp_supported(void);
int emuroot_seccomp_trace(const long *nrs, size_t n);

/* Backends. */
#ifndef EMUROOT_NO_PTRACE
int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);
#endif
int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);

#endif
