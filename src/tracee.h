/* The state emuroot keeps per traced process (or thread). */
#ifndef EMUROOT_TRACEE_H
#define EMUROOT_TRACEE_H

#include "internal.h"
#include "arch.h"
#include "syscalls.h"

#include <sys/syscall.h>     /* the __NR_* numbers the #ifdefs in the handlers test */

typedef struct {
    pid_t pid;                  /* 0 = free slot */
    struct emuroot_ctx *ctx;
    char root[PATH_LEN];        /* host directory this process sees as "/" (changes on a guest chroot) */
    int in_syscall;             /* plain mode: toggles entry/exit on each syscall stop */
    int awaiting_exit;          /* seccomp mode: entry handled, stop again at the syscall's exit */
    int fresh;                  /* new child that hasn't reported its initial SIGSTOP yet */
    int logged;                 /* -v printed this syscall's path; print its result too */
    long sysno;                 /* syscall being executed (valid at exit) */
    long last_entry;            /* the last syscall seen entering (to name what killed it) */
    unsigned long args[6];      /* its arguments, as registers may be clobbered by the result */
    creds_t creds;              /* the fake credentials */
    unsigned long scratch_top;  /* guest memory below this is ours for the current syscall */
    char exe[PATH_LEN];         /* the program when it runs via a loader (what /proc/self/exe should show) */
    char pending_exe[PATH_LEN]; /* same, for an execve in progress */
    /* fake ownership (see ownership.c): a chown in progress, or a file being created */
    int chown_valid;
    uint64_t chown_dev, chown_ino;
    uint32_t chown_cur_uid, chown_cur_gid;      /* its owner as the guest saw it */
    uint32_t chown_uid, chown_gid;              /* the requested owner (-1 = unchanged) */
    char created[PATH_LEN];                     /* host path of a file this call may create */
    char pending_root[PATH_LEN];                /* a chroot to commit once the call returns */
    int mount_handled;                          /* a mount/umount emulated at entry: report success */
    int proc_link;              /* this readlink targets /proc/..., whose answer needs mapping back */
    int proc_link_exe;          /* ... and specifically /proc/.../exe */
} tracee_t;

/* rewrite.c: a traced syscall is about to run - redirect its paths. */
void emuroot_on_entry(tracee_t *t, emuroot_regs_t *regs);
/* fake.c: it has just run - patch its result to match the fake root. */
void emuroot_on_exit(tracee_t *t, emuroot_regs_t *regs);
/* rewrite.c: room for `len` bytes in the guest, just below its stack pointer
 * (valid for the current syscall only), or 0 if there is none. Kept small and
 * close to the stack pointer on purpose: runtimes with tiny stacks (Go) have
 * only a few hundred bytes of slack below it. */
unsigned long emuroot_scratch(tracee_t *t, size_t len);
/* rewrite.c helpers shared with the other entry handlers. */
int emuroot_follow_last(tracee_t *t, emuroot_regs_t *regs, const path_syscall_t *d);
const path_syscall_t *emuroot_find_path_syscall(long sysno);
/* The host path / the symlink-resolved guest path that path argument `slot` names. */
int emuroot_host_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot, int follow, char *host);
int emuroot_guest_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot, char *guest_out, char *host);
/* dac.c: give the fake root the owner-permission bits real root would ignore. */
void emuroot_grant_access(long sysno, emuroot_regs_t *regs, int which, const char *host);
/* identity.c: neutralize credential changes; note chowns and file creations. */
void emuroot_identity_entry(tracee_t *t, emuroot_regs_t *regs, long sysno, enum action action);
void emuroot_note_creation(tracee_t *t, emuroot_regs_t *regs, long sysno, const path_syscall_t *d);
/* mounts.c: emulate chroot/mount/umount; 1 if the call was handled. */
int emuroot_emulate_mounts(tracee_t *t, emuroot_regs_t *regs, long sysno, enum action action);

/* exec.c: expand an execve through the root's own loader / script interpreters.
 * Returns 1 if it rewrote the call, -1 if it had to and couldn't (the call has
 * been made to fail), 0 to leave it to the generic path translation. */
int emuroot_rewrite_exec(tracee_t *t, emuroot_regs_t *regs, int path_slot, int argv_slot);

#endif
