/* Syscall exit: patch what the guest sees so that it lives in a consistent fake
 * root - its credentials (creds.c), file ownership, working directory, uname. */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

/* Make files owned by the real user look owned by the fake identity. */
static void fake_owner(tracee_t *t, unsigned long buf, size_t uid_off, size_t gid_off)
{
    struct emuroot_ctx *ctx = t->ctx;
    unsigned int ids[2] = { 0, 0 };
    if (!buf || guest_read(t->pid, buf + uid_off, &ids[0], 4) != 4 ||
        guest_read(t->pid, buf + gid_off, &ids[1], 4) != 4)
        return;
    if (ids[0] == geteuid()) guest_write(t->pid, buf + uid_off, &ctx->fake_uid, 4);
    if (ids[1] == getegid()) guest_write(t->pid, buf + gid_off, &ctx->fake_gid, 4);
}

/* The kernel's struct stat as seen by the stat/fstat/newfstatat calls we patch:
 * struct stat on 64-bit ABIs, struct stat64 on 32-bit ones. */
#ifdef __LP64__
#define KSTAT struct stat
#else
#define KSTAT struct stat64
#endif
/* struct statx: stx_uid / stx_gid */
#define STATX_UID_OFF 20
#define STATX_GID_OFF 24

void emuroot_on_exit(tracee_t *t, emuroot_regs_t *regs)
{
    struct emuroot_ctx *ctx = t->ctx;
    pid_t pid = t->pid;
    long ret = (long)EMU_RET(*regs);
    if (t->logged) {
        fprintf(stderr, " = %ld\n", ret);
        t->logged = 0;
    }
    enum action action = emuroot_action_of(t->sysno);
    if (action == (enum action)-1) return;

    if (action == A_GETCWD) {
        if (ret <= 0) return;
        char cwd[PATH_LEN], guest[PATH_LEN];
        if (guest_read_str(pid, EMU_ARG1(*regs), cwd, sizeof cwd) < 0 ||
            !emuroot_host_to_guest(ctx, cwd, guest, sizeof guest))
            return;
        if (guest_write(pid, EMU_ARG1(*regs), guest, strlen(guest) + 1) == 0) {
            EMU_RET(*regs) = strlen(guest) + 1;
            regs_set(pid, regs);
        }
        return;
    }
    if (action == A_UNAME) {
        if (!ctx->has_fake_uname || ret != 0) return;
        struct utsname u;
        if (guest_read(pid, EMU_ARG1(*regs), &u, sizeof u) != (ssize_t)sizeof u) return;
        snprintf(u.sysname, sizeof u.sysname, "%s", ctx->uname_sysname);
        snprintf(u.release, sizeof u.release, "%s", ctx->uname_release);
        snprintf(u.machine, sizeof u.machine, "%s", ctx->uname_machine);
        guest_write(pid, EMU_ARG1(*regs), &u, sizeof u);
        return;
    }
    if (!ctx->has_fake_ids) return;

    creds_t *c = &t->creds;
    const unsigned long *a_ = t->args;
    long result = ret;                  /* what the guest will see */
    switch (action) {
    case A_GETUID:  result = c->uid[0]; break;
    case A_GETEUID: result = c->uid[1]; break;
    case A_GETGID:  result = c->gid[0]; break;
    case A_GETEGID: result = c->gid[1]; break;
    case A_GETRESUID:
    case A_GETRESGID: {
        const uint32_t *ids = action == A_GETRESUID ? c->uid : c->gid;
        result = 0;
        for (int i = 0; i < 3; i++) guest_write(pid, a_[i], &ids[i], sizeof ids[i]);
        break;
    }
    case A_GETGROUPS:
        result = c->ngroups;
        if (a_[0] != 0 && (long)a_[0] < c->ngroups) result = -EINVAL;
        else if (a_[0] != 0) guest_write(pid, a_[1], c->groups, (size_t)c->ngroups * sizeof c->groups[0]);
        break;
    case A_SETUID:    result = creds_setuid(c, (uint32_t)a_[0]); break;
    case A_SETGID:    result = creds_setgid(c, (uint32_t)a_[0]); break;
    case A_SETREUID:  result = creds_setreuid(c, (uint32_t)a_[0], (uint32_t)a_[1]); break;
    case A_SETREGID:  result = creds_setregid(c, (uint32_t)a_[0], (uint32_t)a_[1]); break;
    case A_SETRESUID: result = creds_setresuid(c, (uint32_t)a_[0], (uint32_t)a_[1], (uint32_t)a_[2]); break;
    case A_SETRESGID: result = creds_setresgid(c, (uint32_t)a_[0], (uint32_t)a_[1], (uint32_t)a_[2]); break;
    case A_SETFSUID:  result = creds_setfsuid(c, (uint32_t)a_[0]); break;
    case A_SETFSGID:  result = creds_setfsgid(c, (uint32_t)a_[0]); break;
    case A_SETGROUPS: {
        uint32_t list[CREDS_MAX_GROUPS];
        long n = (long)a_[0];
        if (n > 0 && n <= CREDS_MAX_GROUPS &&
            guest_read(pid, a_[1], list, (size_t)n * sizeof list[0]) != (ssize_t)(n * sizeof list[0]))
            result = -EFAULT;
        else
            result = creds_setgroups(c, list, n);
        break;
    }
    case A_CHOWN:                       /* giving files away is allowed for "root" */
        if (ret == -EPERM || ret == -EINVAL) result = 0;
        break;
    case A_STAT_2:
    case A_STAT_3:
        if (ret == 0)
            fake_owner(t, *reg_arg(regs, action == A_STAT_2 ? 2 : 3),
                       offsetof(KSTAT, st_uid), offsetof(KSTAT, st_gid));
        break;
    case A_STATX:
        if (ret == 0)
            fake_owner(t, *reg_arg(regs, 5), STATX_UID_OFF, STATX_GID_OFF);
        break;
    default:
        break;
    }
    if (result != ret) {
        EMU_RET(*regs) = (unsigned long)result;
        regs_set(pid, regs);
    }
}
