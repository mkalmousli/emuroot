/* The state emuroot keeps per traced process (or thread). */
#ifndef EMUROOT_TRACEE_H
#define EMUROOT_TRACEE_H

#include "internal.h"
#include "arch.h"

typedef struct {
    pid_t pid;                  /* 0 = free slot */
    struct emuroot_ctx *ctx;
    int in_syscall;             /* plain mode: toggles entry/exit on each syscall stop */
    int awaiting_exit;          /* seccomp mode: entry handled, stop again at the syscall's exit */
    int fresh;                  /* new child that hasn't reported its initial SIGSTOP yet */
    int logged;                 /* -v printed this syscall's path; print its result too */
    long sysno;                 /* syscall being executed (valid at exit) */
    unsigned long args[6];      /* its arguments, as registers may be clobbered by the result */
    creds_t creds;              /* the fake credentials */
} tracee_t;

/* rewrite.c: a traced syscall is about to run - redirect its paths. */
void emuroot_on_entry(tracee_t *t, emuroot_regs_t *regs);
/* fake.c: it has just run - patch its result to match the fake root. */
void emuroot_on_exit(tracee_t *t, emuroot_regs_t *regs);
/* exec.c: expand an execve through the root's own loader / script interpreters. */
int emuroot_rewrite_exec(struct emuroot_ctx *ctx, pid_t pid, emuroot_regs_t *regs,
                         int path_slot, int argv_slot);

#endif
