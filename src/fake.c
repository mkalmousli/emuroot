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
#include <string.h>
#include <unistd.h>

/* Where, in a stat buffer, the fields that identify a file and its owner live. */
struct stat_layout {
    size_t uid, gid, dev, ino;
    int statx;                  /* statx stores the device as major/minor halves */
};

/* Report files the way the fake root sees them: the owner recorded by a fake
 * chown (or at creation under another identity) if there is one, else the
 * fake identity for anything the real user owns. */
static void fake_owner(tracee_t *t, unsigned long buf, const struct stat_layout *l)
{
    struct emuroot_ctx *ctx = t->ctx;
    uint32_t ids[2];
    uint64_t dev = 0, ino = 0;
    if (!buf || guest_read(t->pid, buf + l->uid, &ids[0], 4) != 4 ||
        guest_read(t->pid, buf + l->gid, &ids[1], 4) != 4 ||
        guest_read(t->pid, buf + l->ino, &ino, sizeof ino) != (ssize_t)sizeof ino)
        return;
    if (l->statx) {
        uint32_t major, minor;
        if (guest_read(t->pid, buf + l->dev, &major, 4) != 4 ||
            guest_read(t->pid, buf + l->dev + 4, &minor, 4) != 4) return;
        dev = ((uint64_t)(major & ~0xfffU) << 32) | ((major & 0xfffU) << 8) |
              ((uint64_t)(minor & ~0xffU) << 12) | (minor & 0xffU);     /* glibc makedev() */
    } else if (guest_read(t->pid, buf + l->dev, &dev, sizeof dev) < 4) {
        return;
    }
    if (emuroot_owner_lookup(ctx, dev, ino, &ids[0], &ids[1])) {
        guest_write(t->pid, buf + l->uid, &ids[0], 4);
        guest_write(t->pid, buf + l->gid, &ids[1], 4);
        return;
    }
    if (ids[0] == geteuid()) guest_write(t->pid, buf + l->uid, &ctx->fake_uid, 4);
    if (ids[1] == getegid()) guest_write(t->pid, buf + l->gid, &ctx->fake_gid, 4);
}

/* The kernel's struct stat as seen by the stat/fstat/newfstatat calls we patch:
 * struct stat on 64-bit ABIs, struct stat64 on 32-bit ones. */
#ifdef __LP64__
#define KSTAT struct stat
#else
#define KSTAT struct stat64
#endif
/* struct statx: stx_uid, stx_gid, stx_ino, then stx_dev_major / stx_dev_minor */
static const struct stat_layout statx_layout = { 20, 24, 136, 32, 1 };
static const struct stat_layout stat_layout = {
    offsetof(KSTAT, st_uid), offsetof(KSTAT, st_gid), offsetof(KSTAT, st_dev), offsetof(KSTAT, st_ino), 0
};

/* readlink("/proc/PID/exe" | "cwd" | "fd/N" | ...) answers with a host path.
 * Show the guest's view instead: the root stripped off, and for /proc/.../exe
 * the program itself rather than the loader emuroot ran it through. */
static void map_proc_link(tracee_t *t, long len)
{
    int at = 0;                                     /* readlinkat(dirfd, path, buf, size) */
#ifdef __NR_readlinkat
    at = t->sysno == __NR_readlinkat;
#endif
    unsigned long buf = t->args[1 + at], size = t->args[2 + at];
    char host[PATH_LEN], guest[PATH_LEN];
    if (len >= (long)sizeof host || guest_read(t->pid, buf, host, (size_t)len) != len) return;
    host[len] = '\0';

    const char *answer;
    if (t->proc_link_exe && t->exe[0])              /* running via a loader */
        answer = t->exe;
    else if (emuroot_host_to_guest(t->ctx, t->root, host, guest, sizeof guest))
        answer = guest;
    else
        return;
    size_t n = strlen(answer) < size ? strlen(answer) : size;      /* readlink doesn't NUL-terminate */
    if (guest_write(t->pid, buf, answer, n) == 0) {
        emuroot_regs_t regs;
        if (regs_get(t->pid, &regs) == 0) {
            EMU_RET(regs) = n;
            regs_set(t->pid, &regs);
        }
    }
}

void emuroot_on_exit(tracee_t *t, emuroot_regs_t *regs)
{
    struct emuroot_ctx *ctx = t->ctx;
    pid_t pid = t->pid;
    long ret = (long)EMU_RET(*regs);
    if (t->logged) {
        fprintf(stderr, " = %ld\n", ret);
        t->logged = 0;
    }
    if (t->created[0]) {                            /* a file this call may just have made */
        struct stat st;
        if (ret >= 0 && lstat(t->created, &st) == 0)
            emuroot_owner_set(ctx, st.st_dev, st.st_ino, t->creds.fsuid, t->creds.fsgid);
        t->created[0] = '\0';
    }
    enum action action = emuroot_action_of(t->sysno);
    if (action == (enum action)-1) return;

    if (action == A_GETCWD) {
        if (ret <= 0) return;
        char cwd[PATH_LEN], guest[PATH_LEN];
        if (guest_read_str(pid, EMU_ARG1(*regs), cwd, sizeof cwd) < 0 ||
            !emuroot_host_to_guest(ctx, t->root, cwd, guest, sizeof guest))
            return;
        if (guest_write(pid, EMU_ARG1(*regs), guest, strlen(guest) + 1) == 0) {
            EMU_RET(*regs) = strlen(guest) + 1;
            regs_set(pid, regs);
        }
        return;
    }
    if (action == A_EXECVE) {                       /* the new image is running now */
        if (ret == 0) memcpy(t->exe, t->pending_exe, sizeof t->exe);
        return;
    }
    if (action == A_CHROOT && t->pending_root[0]) {
        snprintf(t->root, sizeof t->root, "%s", t->pending_root);
        t->pending_root[0] = '\0';
        EMU_RET(*regs) = 0;
        regs_set(pid, regs);
        return;
    }
    if ((action == A_MOUNT || action == A_UMOUNT) && t->mount_handled) {
        EMU_RET(*regs) = 0;
        regs_set(pid, regs);
        return;
    }
    if (action == A_READLINK) {
        if (!t->proc_link || ret <= 0) return;
        map_proc_link(t, ret);
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
    case A_CHOWN:                       /* "root" may give files away; remember to whom */
        if (t->chown_valid && (ret == 0 || ret == -EPERM || ret == -EINVAL)) {
            uint32_t uid = t->chown_uid != (uint32_t)-1 ? t->chown_uid : t->chown_cur_uid;
            uint32_t gid = t->chown_gid != (uint32_t)-1 ? t->chown_gid : t->chown_cur_gid;
            emuroot_owner_set(ctx, t->chown_dev, t->chown_ino, uid, gid);
        }
        if (ret == -EPERM || ret == -EINVAL) result = 0;
        break;
    case A_STAT_2:
    case A_STAT_3:
        if (ret == 0) fake_owner(t, a_[action == A_STAT_2 ? 1 : 2], &stat_layout);
        break;
    case A_STATX:
        if (ret == 0) fake_owner(t, a_[4], &statx_layout);
        break;
    default:
        break;
    }
    if (result != ret) {
        EMU_RET(*regs) = (unsigned long)result;
        regs_set(pid, regs);
    }
}
