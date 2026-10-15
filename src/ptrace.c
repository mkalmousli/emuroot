/* Linux backend: traces the guest and every process/thread it spawns with
 * PTRACE_SYSCALL. On syscall entry, an absolute path argument is read from
 * the guest, translated (path.c), and the argument register is repointed at
 * a rewritten copy in the guest's own stack. On exit, identity syscalls and
 * uname() have their results patched.
 *
 * Syscall numbers come straight from <sys/syscall.h>; a syscall the target
 * doesn't have is simply compiled out by its #ifdef. */
#include "internal.h"
#include "arch.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

#define MAX_TRACEES 256
#define SCRATCH_SIZE 4096       /* room for one rewritten path */
#define SCRATCH_GAP 8192        /* distance below the guest's sp */

extern char **environ;

/* Which argument slots (1-6) of a syscall hold paths the kernel resolves.
 * rename/link-style calls have two; symlink-style calls only list the link
 * location, never the target string (stored verbatim, never resolved). */
typedef struct { long sysno; int slot1, slot2; } path_syscall_t;

static const path_syscall_t path_syscalls[] = {
#ifdef __NR_open
    { __NR_open, 1, 0 },
#endif
#ifdef __NR_creat
    { __NR_creat, 1, 0 },
#endif
#ifdef __NR_stat
    { __NR_stat, 1, 0 },
#endif
#ifdef __NR_stat64
    { __NR_stat64, 1, 0 },
#endif
#ifdef __NR_lstat
    { __NR_lstat, 1, 0 },
#endif
#ifdef __NR_lstat64
    { __NR_lstat64, 1, 0 },
#endif
#ifdef __NR_access
    { __NR_access, 1, 0 },
#endif
#ifdef __NR_readlink
    { __NR_readlink, 1, 0 },
#endif
#ifdef __NR_chdir
    { __NR_chdir, 1, 0 },
#endif
#ifdef __NR_rmdir
    { __NR_rmdir, 1, 0 },
#endif
#ifdef __NR_unlink
    { __NR_unlink, 1, 0 },
#endif
#ifdef __NR_mkdir
    { __NR_mkdir, 1, 0 },
#endif
#ifdef __NR_chmod
    { __NR_chmod, 1, 0 },
#endif
#ifdef __NR_chown
    { __NR_chown, 1, 0 },
#endif
#ifdef __NR_chown32
    { __NR_chown32, 1, 0 },
#endif
#ifdef __NR_lchown
    { __NR_lchown, 1, 0 },
#endif
#ifdef __NR_lchown32
    { __NR_lchown32, 1, 0 },
#endif
#ifdef __NR_truncate
    { __NR_truncate, 1, 0 },
#endif
#ifdef __NR_truncate64
    { __NR_truncate64, 1, 0 },
#endif
#ifdef __NR_utime
    { __NR_utime, 1, 0 },
#endif
#ifdef __NR_utimes
    { __NR_utimes, 1, 0 },
#endif
#ifdef __NR_mknod
    { __NR_mknod, 1, 0 },
#endif
#ifdef __NR_statfs
    { __NR_statfs, 1, 0 },
#endif
#ifdef __NR_statfs64
    { __NR_statfs64, 1, 0 },
#endif
#ifdef __NR_execve
    { __NR_execve, 1, 0 },
#endif
#ifdef __NR_rename
    { __NR_rename, 1, 2 },
#endif
#ifdef __NR_link
    { __NR_link, 1, 2 },
#endif
#ifdef __NR_symlink
    { __NR_symlink, 2, 0 },
#endif
#ifdef __NR_openat
    { __NR_openat, 2, 0 },
#endif
#ifdef __NR_openat2
    { __NR_openat2, 2, 0 },
#endif
#ifdef __NR_mkdirat
    { __NR_mkdirat, 2, 0 },
#endif
#ifdef __NR_unlinkat
    { __NR_unlinkat, 2, 0 },
#endif
#ifdef __NR_newfstatat
    { __NR_newfstatat, 2, 0 },
#endif
#ifdef __NR_fstatat64
    { __NR_fstatat64, 2, 0 },
#endif
#ifdef __NR_statx
    { __NR_statx, 2, 0 },
#endif
#ifdef __NR_readlinkat
    { __NR_readlinkat, 2, 0 },
#endif
#ifdef __NR_fchmodat
    { __NR_fchmodat, 2, 0 },
#endif
#ifdef __NR_fchownat
    { __NR_fchownat, 2, 0 },
#endif
#ifdef __NR_utimensat
    { __NR_utimensat, 2, 0 },
#endif
#ifdef __NR_faccessat
    { __NR_faccessat, 2, 0 },
#endif
#ifdef __NR_faccessat2
    { __NR_faccessat2, 2, 0 },
#endif
#ifdef __NR_mknodat
    { __NR_mknodat, 2, 0 },
#endif
#ifdef __NR_execveat
    { __NR_execveat, 2, 0 },
#endif
#ifdef __NR_renameat
    { __NR_renameat, 2, 4 },
#endif
#ifdef __NR_renameat2
    { __NR_renameat2, 2, 4 },
#endif
#ifdef __NR_linkat
    { __NR_linkat, 2, 4 },
#endif
#ifdef __NR_symlinkat
    { __NR_symlinkat, 3, 0 },
#endif
};

typedef struct {
    pid_t pid;                  /* 0 = free slot */
    struct emuroot_ctx *ctx;
    int in_syscall;             /* toggles entry/exit on each syscall stop */
    int fresh;                  /* new child that hasn't reported its initial SIGSTOP yet */
    long sysno;                 /* syscall being executed (valid at exit) */
} tracee_t;

static unsigned long *arg_slot(emuroot_regs_t *r, int slot)
{
    switch (slot) {
    case 1: return (unsigned long *)&REG_ARG1(*r);
    case 2: return (unsigned long *)&REG_ARG2(*r);
    case 3: return (unsigned long *)&REG_ARG3(*r);
    case 4: return (unsigned long *)&REG_ARG4(*r);
    case 5: return (unsigned long *)&REG_ARG5(*r);
    default: return (unsigned long *)&REG_ARG6(*r);
    }
}

static ssize_t guest_read_str(pid_t pid, unsigned long addr, char *buf, size_t size)
{
    struct iovec local = { buf, size - 1 }, remote = { (void *)addr, size - 1 };
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (n < 0) return -1;
    buf[strnlen(buf, (size_t)n)] = '\0';
    return 0;
}

static int guest_write(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    struct iovec local = { (void *)data, len }, remote = { (void *)addr, len };
    return process_vm_writev(pid, &local, 1, &remote, 1, 0) == (ssize_t)len ? 0 : -1;
}

/* Rewrite one path argument. `which` picks a distinct scratch buffer so two
 * paths in one syscall don't clobber each other. */
static void rewrite_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot, int which)
{
    if (!slot) return;
    unsigned long *reg = arg_slot(regs, slot);
    char guest[PATH_LEN], host[PATH_LEN];
    if (*reg == 0 || guest_read_str(t->pid, *reg, guest, sizeof guest) < 0)
        return;

    int rewritten = emuroot_translate_path(t->ctx, guest, host, sizeof host);
    if (t->ctx->verbose)
        fprintf(stderr, "[emuroot] sys=%ld \"%s\" -> \"%s\"%s\n", sysno, guest, host,
                rewritten ? "" : " (unchanged)");
    if (!rewritten) return;

    unsigned long scratch = (REG_SP(*regs) - SCRATCH_GAP - (unsigned long)which * SCRATCH_SIZE) & ~0xFUL;
    if (guest_write(t->pid, scratch, host, strlen(host) + 1) == 0) {
        *reg = scratch;
        regs_set(t->pid, regs);
    }
}

static void on_entry(tracee_t *t, emuroot_regs_t *regs)
{
    long sysno = (long)REG_SYSNO(*regs);
    t->sysno = sysno;
    for (size_t i = 0; i < sizeof path_syscalls / sizeof path_syscalls[0]; i++) {
        if (path_syscalls[i].sysno == sysno) {
            rewrite_path_arg(t, regs, sysno, path_syscalls[i].slot1, 0);
            rewrite_path_arg(t, regs, sysno, path_syscalls[i].slot2, 1);
            return;
        }
    }
}

/* Identity syscalls. 32-bit ABIs call the *32 variants; both are matched. */
enum id_action { ID_NONE, ID_UID, ID_GID, ID_SET_OK, ID_RES_UID, ID_RES_GID };

static enum id_action identity_action(long sysno)
{
    switch (sysno) {
#ifdef __NR_getuid
    case __NR_getuid:
#endif
#ifdef __NR_getuid32
    case __NR_getuid32:
#endif
#ifdef __NR_geteuid
    case __NR_geteuid:
#endif
#ifdef __NR_geteuid32
    case __NR_geteuid32:
#endif
        return ID_UID;
#ifdef __NR_getgid
    case __NR_getgid:
#endif
#ifdef __NR_getgid32
    case __NR_getgid32:
#endif
#ifdef __NR_getegid
    case __NR_getegid:
#endif
#ifdef __NR_getegid32
    case __NR_getegid32:
#endif
        return ID_GID;
#ifdef __NR_getresuid
    case __NR_getresuid:
#endif
#ifdef __NR_getresuid32
    case __NR_getresuid32:
#endif
        return ID_RES_UID;
#ifdef __NR_getresgid
    case __NR_getresgid:
#endif
#ifdef __NR_getresgid32
    case __NR_getresgid32:
#endif
        return ID_RES_GID;
#ifdef __NR_setuid
    case __NR_setuid:
#endif
#ifdef __NR_setuid32
    case __NR_setuid32:
#endif
#ifdef __NR_setgid
    case __NR_setgid:
#endif
#ifdef __NR_setgid32
    case __NR_setgid32:
#endif
#ifdef __NR_setreuid
    case __NR_setreuid:
#endif
#ifdef __NR_setreuid32
    case __NR_setreuid32:
#endif
#ifdef __NR_setregid
    case __NR_setregid:
#endif
#ifdef __NR_setregid32
    case __NR_setregid32:
#endif
#ifdef __NR_setresuid
    case __NR_setresuid:
#endif
#ifdef __NR_setresuid32
    case __NR_setresuid32:
#endif
#ifdef __NR_setresgid
    case __NR_setresgid:
#endif
#ifdef __NR_setresgid32
    case __NR_setresgid32:
#endif
        return ID_SET_OK;
    default:
        return ID_NONE;
    }
}

static void on_exit_(tracee_t *t, emuroot_regs_t *regs)
{
    struct emuroot_ctx *ctx = t->ctx;
    pid_t pid = t->pid;
    int ok = (long)REG_RET(*regs) == 0;

    if (ctx->has_fake_ids) {
        unsigned int id;
        switch (identity_action(t->sysno)) {
        case ID_UID:
            REG_RET(*regs) = ctx->fake_uid;
            regs_set(pid, regs);
            break;
        case ID_GID:
            REG_RET(*regs) = ctx->fake_gid;
            regs_set(pid, regs);
            break;
        case ID_SET_OK:                 /* privileged id changes always "succeed" */
            REG_RET(*regs) = 0;
            regs_set(pid, regs);
            break;
        case ID_RES_UID:
        case ID_RES_GID:
            if (!ok) break;
            id = identity_action(t->sysno) == ID_RES_UID ? ctx->fake_uid : ctx->fake_gid;
            guest_write(pid, REG_ARG1(*regs), &id, sizeof id);
            guest_write(pid, REG_ARG2(*regs), &id, sizeof id);
            guest_write(pid, REG_ARG3(*regs), &id, sizeof id);
            break;
        default:
            break;
        }
    }

#ifdef __NR_uname
    if (t->sysno == __NR_uname && ctx->has_fake_uname && ok) {
        struct utsname u;
        memset(&u, 0, sizeof u);
        memcpy(u.sysname, ctx->uname_sysname, sizeof u.sysname - 1);
        memcpy(u.release, ctx->uname_release, sizeof u.release - 1);
        memcpy(u.machine, ctx->uname_machine, sizeof u.machine - 1);
        guest_write(pid, REG_ARG1(*regs), &u, sizeof u);
    }
#endif
}

static tracee_t *tracee_find(tracee_t *tab, pid_t pid)
{
    for (int i = 0; i < MAX_TRACEES; i++)
        if (tab[i].pid == pid) return &tab[i];
    return NULL;
}

static tracee_t *tracee_add(tracee_t *tab, struct emuroot_ctx *ctx, pid_t pid)
{
    tracee_t *t = tracee_find(tab, 0);
    if (!t) return NULL;
    memset(t, 0, sizeof *t);
    t->pid = pid;
    t->ctx = ctx;
    t->fresh = 1;
    return t;
}

int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[])
{
    pid_t root_pid = fork();
    if (root_pid < 0) return EMUROOT_ERR_SYSTEM;

    if (root_pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) _exit(127);
        /* Start inside the root so relative paths resolve there untranslated. */
        if (ctx->root[0] && chdir(ctx->root) < 0) {
            perror("emuroot: chdir to root");
            _exit(127);
        }
        raise(SIGSTOP);
        if (envp) environ = (char **)envp;      /* so execvp's PATH search sees it */
        execvp(argv[0], argv);
        _exit(127);
    }

    int status;
    waitpid(root_pid, &status, 0);              /* the initial SIGSTOP */
    ptrace(PTRACE_SETOPTIONS, root_pid, 0,
           (void *)(long)(PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL | PTRACE_O_TRACEFORK |
                          PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE));

    tracee_t *tracees = calloc(MAX_TRACEES, sizeof *tracees);
    if (!tracees) return EMUROOT_ERR_SYSTEM;
    tracee_add(tracees, ctx, root_pid)->fresh = 0;
    ptrace(PTRACE_SYSCALL, root_pid, NULL, NULL);

    int rc = EMUROOT_ERR_SYSTEM;
    for (;;) {
        pid_t w = waitpid(-1, &status, __WALL);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        tracee_t *t = tracee_find(tracees, w);
        if (!t) t = tracee_add(tracees, ctx, w);    /* child seen before its parent's fork event */
        if (!t) continue;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (w == root_pid) {
                rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                break;
            }
            t->pid = 0;
            continue;
        }
        if (!WIFSTOPPED(status)) continue;

        int sig = WSTOPSIG(status), event = status >> 16;
        long forward = 0;
        if (sig == (SIGTRAP | 0x80)) {              /* syscall entry or exit */
            emuroot_regs_t regs;
            if (regs_get(w, &regs) == 0) {
                if (t->in_syscall) on_exit_(t, &regs);
                else               on_entry(t, &regs);
            }
            t->in_syscall = !t->in_syscall;
        } else if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK ||
                   event == PTRACE_EVENT_CLONE) {
            unsigned long child = 0;
            ptrace(PTRACE_GETEVENTMSG, w, NULL, &child);
            if (!tracee_find(tracees, (pid_t)child))
                tracee_add(tracees, ctx, (pid_t)child);
        } else if (sig == SIGSTOP && t->fresh) {
            t->fresh = 0;                           /* a new child's initial stop */
        } else if (sig != SIGTRAP) {
            forward = sig;                          /* deliver other signals unchanged */
        }
        ptrace(PTRACE_SYSCALL, w, NULL, (void *)forward);
    }

    free(tracees);
    return rc;
}
