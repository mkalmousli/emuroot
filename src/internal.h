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
    /* Where the binding is attached on the host (the target directory, as the
     * root maps it). A chroot'ed process names the same place differently, so
     * lookups also match on this. Filled in when the guest starts or mounts. */
    char mount[PATH_LEN];
    size_t mount_len;
} binding_t;

struct emuroot_ctx {
    char root[PATH_LEN];            /* "" = no root redirect */
    binding_t bindings[MAX_BINDINGS];
    size_t nbindings;

    int has_fake_ids;
    unsigned int fake_uid, fake_gid;

    int has_fake_uname;
    char uname_sysname[65], uname_release[65], uname_machine[65];

    char ids_file[PATH_LEN];        /* where to persist the ownership table, or "" */
    struct owner_db *owners;        /* the fake ownership table (ownership.c) */

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

/* ownership.c: who the fake root says owns each file, keyed by (device, inode). */
int emuroot_owner_lookup(struct emuroot_ctx *ctx, uint64_t dev, uint64_t ino, uint32_t *uid, uint32_t *gid);
void emuroot_owner_set(struct emuroot_ctx *ctx, uint64_t dev, uint64_t ino, uint32_t uid, uint32_t gid);
void emuroot_owners_load(struct emuroot_ctx *ctx, const char *file);
void emuroot_owners_save(struct emuroot_ctx *ctx, const char *file);
void emuroot_owners_free(struct emuroot_ctx *ctx);

/* path.c: map an absolute guest path (`root` is the host directory the guest sees as /;
 * it starts as ctx->root, and a guest chroot changes it for that process) to a host path via root, bindings and
 * symlinks. follow_last=0 leaves a symlink in the last component alone
 * (lstat, unlink, ...). Relative paths are never rewritten: the guest runs
 * with its working directory already inside the root. Returns 1 if the
 * result differs from the input. */
int emuroot_translate_path(const struct emuroot_ctx *ctx, const char *root, const char *guest_path,
                           int follow_last, char *out, size_t outsz);

/* The same, also returning the guest path with its symlinks resolved. */
int emuroot_translate_path_ex(const struct emuroot_ctx *ctx, const char *root, const char *guest_path,
                              int follow_last, char *out, size_t outsz, char *guest_out, size_t guest_outsz);

/* Drops the most recent binding of `guest`; 1 if there was one. */
int emuroot_unbind(struct emuroot_ctx *ctx, const char *guest);

/* Inverse, for paths reported back to the guest (getcwd). Returns 1 if
 * `host` lies under the root or a binding. */
int emuroot_host_to_guest(const struct emuroot_ctx *ctx, const char *root, const char *host,
                          char *out, size_t outsz);

/* seccomp.c: the fast path (see there). */
int emuroot_seccomp_supported(void);
int emuroot_seccomp_installable(void);
int emuroot_seccomp_trace(const long *nrs, size_t n);

/* Backends. */
#ifndef EMUROOT_NO_PTRACE
int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);
#endif
int emuroot_run_envonly(struct emuroot_ctx *ctx, char *const argv[], char *const envp[]);

#endif
