/* Syscall entry, identity side: credential changes the fake root asks for are
 * neutralized (creds.c emulates them), and chown / file creation are noted so
 * the exit handler (fake.c) can record the new owner in the ownership table. */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Credential changes are emulated (creds.c) and must not also happen for real,
 * e.g. when emuroot itself runs as root: turn them into no-ops. */
static void neutralize_id_change(tracee_t *t, emuroot_regs_t *regs, enum action a)
{
    const unsigned long none = (unsigned int)-1;
    switch (a) {
    case A_SETUID:    *reg_arg(regs, 1) = geteuid(); break;
    case A_SETGID:    *reg_arg(regs, 1) = getegid(); break;
    case A_SETREUID:
    case A_SETREGID:  *reg_arg(regs, 1) = *reg_arg(regs, 2) = none; break;
    case A_SETRESUID:
    case A_SETRESGID: *reg_arg(regs, 1) = *reg_arg(regs, 2) = *reg_arg(regs, 3) = none; break;
    case A_SETFSUID:
    case A_SETFSGID:  *reg_arg(regs, 1) = none; break;
    default: return;
    }
    regs_set(t->pid, regs);
}

static void note_chown(tracee_t *t, emuroot_regs_t *regs, long sysno, const path_syscall_t *d)
{
    struct stat st;
    char host[PATH_LEN];
    int uid_arg = 2, ok;                        /* chown(path, uid, gid), fchown(fd, uid, gid) */
#ifdef __NR_fchownat
    if (sysno == __NR_fchownat) uid_arg = 3;    /* fchownat(dirfd, path, uid, gid, flags) */
#endif
    int follow = d ? emuroot_follow_last(t, regs, d) : 1;
    if (d && emuroot_host_path_arg(t, regs, sysno, d->slot1, follow, host)) {
        ok = (follow ? stat(host, &st) : lstat(host, &st)) == 0;
    } else {                    /* fchown(fd, ...), or fchownat(fd, "", ..., AT_EMPTY_PATH) */
        char link[64];
        snprintf(link, sizeof link, "/proc/%d/fd/%d", (int)t->pid, (int)*reg_arg(regs, 1));
        ok = stat(link, &st) == 0;
    }
    if (!ok) return;
    t->chown_valid = 1;
    t->chown_dev = st.st_dev;
    t->chown_ino = st.st_ino;
    t->chown_cur_uid = st.st_uid == geteuid() ? t->ctx->fake_uid : st.st_uid;
    t->chown_cur_gid = st.st_gid == getegid() ? t->ctx->fake_gid : st.st_gid;
    emuroot_owner_lookup(t->ctx, t->chown_dev, t->chown_ino, &t->chown_cur_uid, &t->chown_cur_gid);
    t->chown_uid = (uint32_t)*reg_arg(regs, uid_arg);
    t->chown_gid = (uint32_t)*reg_arg(regs, uid_arg + 1);
}

static int creates_file(emuroot_regs_t *regs, long sysno)
{
    switch (sysno) {
#ifdef __NR_open
    case __NR_open: return *reg_arg(regs, 2) & O_CREAT;
#endif
#ifdef __NR_openat
    case __NR_openat: return *reg_arg(regs, 3) & O_CREAT;
#endif
#ifdef __NR_creat
    case __NR_creat:
#endif
#ifdef __NR_mkdir
    case __NR_mkdir:
#endif
#ifdef __NR_mkdirat
    case __NR_mkdirat:
#endif
#ifdef __NR_mknod
    case __NR_mknod:
#endif
#ifdef __NR_mknodat
    case __NR_mknodat:
#endif
#ifdef __NR_symlink
    case __NR_symlink:
#endif
#ifdef __NR_symlinkat
    case __NR_symlinkat:
#endif
        return 1;
    default:
        return 0;
    }
}

/* Is this call about to create the file at path 1, with the guest running as
 * someone other than the identity the files would otherwise be reported as? */
void emuroot_note_creation(tracee_t *t, emuroot_regs_t *regs, long sysno, const path_syscall_t *d)
{
    if (!t->ctx->has_fake_ids || (t->creds.fsuid == t->ctx->fake_uid && t->creds.fsgid == t->ctx->fake_gid))
        return;
    if (!creates_file(regs, sysno))
        return;
    struct stat st;
    if (emuroot_host_path_arg(t, regs, sysno, d->slot1, 0, t->created) && lstat(t->created, &st) != 0)
        return;                                         /* new file: remember to record its owner */
    t->created[0] = '\0';
}

void emuroot_identity_entry(tracee_t *t, emuroot_regs_t *regs, long sysno, enum action action)
{
    neutralize_id_change(t, regs, action);
    if (action == A_CHOWN) note_chown(t, regs, sysno, emuroot_find_path_syscall(sysno));
}
