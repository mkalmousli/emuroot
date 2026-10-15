/* Syscall entry: redirect the paths a syscall is about to resolve (and unix
 * socket addresses, and execve) so that it acts inside the emulated root.
 * Each path argument is read from the guest, translated by path.c, and the
 * argument register repointed at a rewritten copy in the guest's own stack. */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif

#define SCRATCH_SIZE 4096       /* room for one rewritten path */
#define SCRATCH_GAP 8192        /* distance below the guest's sp */

/* Should a symlink in the last component of path 1 be followed? */
static int follow_last(tracee_t *t, emuroot_regs_t *regs, const path_syscall_t *d)
{
    unsigned long flags = d->flag_slot ? *reg_arg(regs, d->flag_slot) : 0;
    switch (d->mode) {
    case M_FOLLOW:   return 1;
    case M_NOFOLLOW: return 0;
    case M_OPEN:     return !(flags & O_NOFOLLOW) && (flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL);
    case M_AT:       return !(flags & AT_SYMLINK_NOFOLLOW);
    case M_LINKAT:   return flags & AT_SYMLINK_FOLLOW;
    case M_OPENAT2: {
        unsigned long long how_flags = 0;
        guest_read(t->pid, flags, &how_flags, sizeof how_flags);
        return !(how_flags & O_NOFOLLOW);
    }
    }
    return 1;
}

/* The dirfd slot of an *at()-style syscall's path in `slot`, or 0. */
static int dirfd_slot(long sysno, int slot)
{
    if (slot < 2) return 0;
#ifdef __NR_symlink
    if (sysno == __NR_symlink) return 0;
#endif
#ifdef __NR_inotify_add_watch
    if (sysno == __NR_inotify_add_watch) return 0;
#endif
    (void)sysno;
    return slot - 1;
}

/* Absolute guest path for a possibly relative one, using the guest's working
 * directory (or the directory fd, for *at() calls). Relative paths must be
 * translated too: they can pass through absolute symlinks or climb out of the
 * root with "..". Returns 0 if the base directory lies outside the emulated
 * namespace, in which case the kernel can be left to resolve it. */
static int absolute_guest_path(tracee_t *t, emuroot_regs_t *regs, int fd_slot,
                               const char *path, char *out, size_t size)
{
    if (path[0] == '/') {
        snprintf(out, size, "%s", path);
        return 1;
    }
    int fd = fd_slot ? (int)*reg_arg(regs, fd_slot) : AT_FDCWD;
    char link[64], base[PATH_LEN], guest_base[PATH_LEN];
    if (fd == AT_FDCWD) snprintf(link, sizeof link, "/proc/%d/cwd", (int)t->pid);
    else                snprintf(link, sizeof link, "/proc/%d/fd/%d", (int)t->pid, fd);
    ssize_t n = readlink(link, base, sizeof base - 1);
    if (n <= 0) return 0;
    base[n] = '\0';
    if (!emuroot_host_to_guest(t->ctx, base, guest_base, sizeof guest_base)) return 0;
    snprintf(out, size, "%s/%s", strcmp(guest_base, "/") == 0 ? "" : guest_base, path);
    return 1;
}

/* openat2 with RESOLVE_* flags (BENEATH, NO_XDEV, ...) is a request for strict
 * kernel-side resolution relative to a dirfd; turning its path absolute
 * would break that contract (EXDEV), so such relative paths are left alone. */
static int strict_openat2(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot)
{
#ifdef __NR_openat2
    if (sysno == __NR_openat2 && slot == 2) {
        unsigned long long how[3] = { 0, 0, 0 };            /* flags, mode, resolve */
        guest_read(t->pid, *reg_arg(regs, 3), how, sizeof how);
        return how[2] != 0;
    }
#endif
    (void)t; (void)regs; (void)sysno; (void)slot;
    return 0;
}

/* Real root ignores permission bits; a faked root cannot. Files and directories
 * the user owns but cannot access (a 0555 /usr/bin that a package manager must
 * write to, a 0000 /etc/shadow) get the owner bits root would have used. */
enum dac { DAC_NONE, DAC_PARENT_W, DAC_READ, DAC_RW };

static enum dac dac_for(long sysno, emuroot_regs_t *regs, int which)
{
    unsigned long flags = 0;
    switch (which) {
    case 0:
        switch (sysno) {
#ifdef __NR_open
        case __NR_open: flags = *reg_arg(regs, 2); goto open_like;
#endif
#ifdef __NR_openat
        case __NR_openat: flags = *reg_arg(regs, 3); goto open_like;
#endif
#ifdef __NR_openat2
        case __NR_openat2: return DAC_READ;     /* flags live in a struct; reading is the common need */
#endif
#ifdef __NR_creat
        case __NR_creat: return DAC_PARENT_W;
#endif
#ifdef __NR_truncate
        case __NR_truncate: return DAC_RW;
#endif
#ifdef __NR_truncate64
        case __NR_truncate64: return DAC_RW;
#endif
#ifdef __NR_mkdir
        case __NR_mkdir: return DAC_PARENT_W;
#endif
#ifdef __NR_mkdirat
        case __NR_mkdirat: return DAC_PARENT_W;
#endif
#ifdef __NR_mknod
        case __NR_mknod: return DAC_PARENT_W;
#endif
#ifdef __NR_mknodat
        case __NR_mknodat: return DAC_PARENT_W;
#endif
#ifdef __NR_symlink
        case __NR_symlink: return DAC_PARENT_W;
#endif
#ifdef __NR_symlinkat
        case __NR_symlinkat: return DAC_PARENT_W;
#endif
#ifdef __NR_unlink
        case __NR_unlink: return DAC_PARENT_W;
#endif
#ifdef __NR_unlinkat
        case __NR_unlinkat: return DAC_PARENT_W;
#endif
#ifdef __NR_rmdir
        case __NR_rmdir: return DAC_PARENT_W;
#endif
#ifdef __NR_rename
        case __NR_rename: return DAC_PARENT_W;
#endif
#ifdef __NR_renameat
        case __NR_renameat: return DAC_PARENT_W;
#endif
#ifdef __NR_renameat2
        case __NR_renameat2: return DAC_PARENT_W;
#endif
        default: return DAC_NONE;
        }
    default:                            /* second path: the destination of rename/link, or a symlink's location */
        switch (sysno) {
#ifdef __NR_rename
        case __NR_rename:
#endif
#ifdef __NR_renameat
        case __NR_renameat:
#endif
#ifdef __NR_renameat2
        case __NR_renameat2:
#endif
#ifdef __NR_link
        case __NR_link:
#endif
#ifdef __NR_linkat
        case __NR_linkat:
#endif
            return DAC_PARENT_W;
        default: return DAC_NONE;
        }
    }
open_like:
    if (flags & O_CREAT) return DAC_PARENT_W;
    return ((flags & O_ACCMODE) != O_RDONLY || (flags & O_TRUNC)) ? DAC_RW : DAC_READ;
}

static void grant_owner(const char *path, mode_t bits)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_uid != geteuid()) return;
    if ((st.st_mode & bits) != bits) chmod(path, st.st_mode | bits);
}

static void ensure_dac(const char *host, enum dac need)
{
    if (need == DAC_NONE || geteuid() == 0) return;
    if (need == DAC_PARENT_W) {
        char dir[PATH_LEN];
        snprintf(dir, sizeof dir, "%s", host);
        char *slash = strrchr(dir, '/');
        if (!slash) return;
        if (slash == dir) slash[1] = '\0'; else *slash = '\0';
        grant_owner(dir, 0300);                 /* the parent must be writable and searchable */
    } else {
        struct stat st;
        mode_t bits = need == DAC_RW ? 0600 : 0400;
        if (stat(host, &st) == 0 && S_ISDIR(st.st_mode)) bits |= 0100;
        grant_owner(host, bits);
    }
}

/* Rewrite the path in `slot`. `which` picks a distinct scratch buffer so two
 * paths in one syscall don't clobber each other. */
static void rewrite_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno,
                             int slot, int follow, int which)
{
    if (!slot || (!t->ctx->root[0] && !t->ctx->nbindings)) return;
    unsigned long *reg = reg_arg(regs, slot);
    char given[PATH_LEN], guest[2 * PATH_LEN], host[PATH_LEN];
    if (*reg == 0 || guest_read_str(t->pid, *reg, given, sizeof given) < 0 || !given[0])
        return;                                 /* NULL or "" (AT_EMPTY_PATH): nothing to do */
    if (given[0] != '/' && strict_openat2(t, regs, sysno, slot))
        return;
    if (!absolute_guest_path(t, regs, dirfd_slot(sysno, slot), given, guest, sizeof guest))
        return;

    int rewritten = emuroot_translate_path(t->ctx, guest, follow, host, sizeof host);
    if (t->ctx->has_fake_ids && t->creds.uid[1] == 0)
        ensure_dac(host, dac_for(sysno, regs, which));
    if (t->ctx->verbose) {
        fprintf(stderr, "[emuroot] pid=%d sys=%ld \"%s\" -> \"%s\"%s", (int)t->pid, sysno, given,
                host, rewritten ? "" : " (unchanged)");
        t->logged = 1;
    }
    if (!rewritten) return;

    unsigned long scratch = (EMU_SP(*regs) - SCRATCH_GAP - (unsigned long)which * SCRATCH_SIZE) & ~0xFUL;
    if (guest_write(t->pid, scratch, host, strlen(host) + 1) == 0) {
        *reg = scratch;
        regs_set(t->pid, regs);
    }
}

/* bind/connect/sendto on a unix socket carry a path inside a sockaddr_un. */
static void rewrite_sockaddr(tracee_t *t, emuroot_regs_t *regs, int addr_slot, int len_slot)
{
    unsigned long *addr_reg = reg_arg(regs, addr_slot), *len_reg = reg_arg(regs, len_slot);
    struct sockaddr_un sun;
    size_t len = *len_reg < sizeof sun ? *len_reg : sizeof sun;
    memset(&sun, 0, sizeof sun);
    if (!*addr_reg || len <= offsetof(struct sockaddr_un, sun_path) ||
        guest_read(t->pid, *addr_reg, &sun, len) != (ssize_t)len ||
        sun.sun_family != AF_UNIX || sun.sun_path[0] != '/')   /* abstract sockets are not files */
        return;

    char guest[PATH_LEN], host[PATH_LEN];
    snprintf(guest, sizeof guest, "%.*s", (int)sizeof sun.sun_path, sun.sun_path);
    if (!emuroot_translate_path(t->ctx, guest, 1, host, sizeof host)) return;
    if (strlen(host) >= sizeof sun.sun_path) {
        if (t->ctx->verbose)
            fprintf(stderr, "[emuroot] socket path too long to relocate: %s\n", host);
        return;
    }
    memset(sun.sun_path, 0, sizeof sun.sun_path);
    strcpy(sun.sun_path, host);
    unsigned long scratch = (EMU_SP(*regs) - SCRATCH_GAP) & ~0xFUL;
    if (guest_write(t->pid, scratch, &sun, sizeof sun) == 0) {
        *addr_reg = scratch;
        *len_reg = offsetof(struct sockaddr_un, sun_path) + strlen(host) + 1;
        regs_set(t->pid, regs);
    }
}

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

void emuroot_on_entry(tracee_t *t, emuroot_regs_t *regs)
{
    long sysno = (long)EMU_SYSNO(*regs);
    t->sysno = sysno;
    for (int i = 0; i < 6; i++) t->args[i] = *reg_arg(regs, i + 1);
    if (t->ctx->has_fake_ids) neutralize_id_change(t, regs, emuroot_action_of(sysno));

#ifdef __NR_execve
    if (sysno == __NR_execve && emuroot_rewrite_exec(t->ctx, t->pid, regs, 1, 2))
        return;
#endif
#ifdef __NR_bind
    if (sysno == __NR_bind) {
        rewrite_sockaddr(t, regs, 2, 3);
        return;
    }
#endif
#ifdef __NR_connect
    if (sysno == __NR_connect) {
        rewrite_sockaddr(t, regs, 2, 3);
        return;
    }
#endif
#ifdef __NR_sendto
    if (sysno == __NR_sendto) {
        rewrite_sockaddr(t, regs, 5, 6);
        return;
    }
#endif
    for (size_t i = 0; i < n_path_syscalls; i++) {
        const path_syscall_t *d = &path_syscalls[i];
        if (d->sysno == sysno) {
            rewrite_path_arg(t, regs, sysno, d->slot1, follow_last(t, regs, d), 0);
            rewrite_path_arg(t, regs, sysno, d->slot2, 0, 1);
            return;
        }
    }
#ifdef __NR_execve
    if (sysno == __NR_execve)
        rewrite_path_arg(t, regs, sysno, 1, 1, 0);      /* exec.c declined: plain path */
#endif
#ifdef __NR_execveat
    if (sysno == __NR_execveat)
        rewrite_path_arg(t, regs, sysno, 2, 1, 0);
#endif
}
