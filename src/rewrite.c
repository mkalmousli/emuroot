/* Syscall entry, path side: redirect the paths a syscall is about to resolve
 * (and unix socket addresses, and execve) so that it acts inside the emulated
 * root. Each path argument is read from the guest, translated by path.c, and
 * the argument register repointed at a rewritten copy in the guest's own
 * memory. */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif

#define SCRATCH_MARGIN 512      /* keeps clear of the red zone (up to 288 bytes) */

unsigned long emuroot_scratch(tracee_t *t, size_t len)
{
    unsigned long addr = (t->scratch_top - len) & ~0xFUL;
    if (addr > t->scratch_top) return 0;        /* wrapped */
    t->scratch_top = addr;
    return addr;
}

/* A rewrite that was needed but couldn't be done must not let the call through
 * unredirected (that would escape the root): make it fail with EFAULT. */
static void fail_closed(tracee_t *t, emuroot_regs_t *regs, unsigned long *reg)
{
    if (t->ctx->verbose) fprintf(stderr, "[emuroot] pid=%d: no room to rewrite a path, failing the call\n", (int)t->pid);
    *reg = 0;
    regs_set(t->pid, regs);
}

/* Should a symlink in the last component of path 1 be followed? */
int emuroot_follow_last(tracee_t *t, emuroot_regs_t *regs, const path_syscall_t *d)
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
    if (!emuroot_host_to_guest(t->ctx, t->root, base, guest_base, sizeof guest_base)) return 0;
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

static int is_readlink(long sysno)
{
#ifdef __NR_readlink
    if (sysno == __NR_readlink) return 1;
#endif
#ifdef __NR_readlinkat
    if (sysno == __NR_readlinkat) return 1;
#endif
    (void)sysno;
    return 0;
}

/* The host path that path argument `slot` names, or 0 if it can't be read. */
int emuroot_host_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot, int follow, char *host)
{
    char given[PATH_LEN], guest[2 * PATH_LEN];
    unsigned long ptr = *reg_arg(regs, slot);
    if (!ptr || guest_read_str(t->pid, ptr, given, sizeof given) < 0 || !given[0]) return 0;
    if (!absolute_guest_path(t, regs, dirfd_slot(sysno, slot), given, guest, sizeof guest)) {
        snprintf(host, PATH_LEN, "%s", given);          /* outside the emulated namespace */
        return 1;
    }
    emuroot_translate_path(t->ctx, t->root, guest, follow, host, PATH_LEN);
    return 1;
}

/* A chown is about to happen (or be refused for lack of privilege): remember
 * which file, who owns it now, and who it should belong to. `d` describes the
 * path argument; it is NULL for fchown(fd, ...). */
const path_syscall_t *emuroot_find_path_syscall(long sysno)
{
    for (size_t i = 0; i < n_path_syscalls; i++)
        if (path_syscalls[i].sysno == sysno) return &path_syscalls[i];
    return NULL;
}

/* Rewrite the path in `slot`. `which` picks a distinct scratch buffer so two
 * paths in one syscall don't clobber each other. */
static void rewrite_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno,
                             int slot, int follow, int which)
{
    if (!slot || (!t->root[0] && !t->ctx->nbindings)) return;
    unsigned long *reg = reg_arg(regs, slot);
    char given[PATH_LEN], guest[2 * PATH_LEN], host[PATH_LEN];
    if (*reg == 0) return;
    if (guest_read_str(t->pid, *reg, given, sizeof given) < 0) {
        if (errno != EFAULT) fail_closed(t, regs, reg);     /* can't see the path: don't let it through */
        return;                                 /* (a bad pointer: the kernel will report it) */
    }
    if (!given[0]) return;                      /* "" (AT_EMPTY_PATH): nothing to do */
    if (given[0] != '/' && strict_openat2(t, regs, sysno, slot))
        return;
    if (!absolute_guest_path(t, regs, dirfd_slot(sysno, slot), given, guest, sizeof guest))
        return;

    if (strncmp(guest, "/proc/", 6) == 0 && which == 0 && is_readlink(sysno)) {
        size_t n = strlen(guest);
        t->proc_link = 1;
        t->proc_link_exe = n >= 4 && strcmp(guest + n - 4, "/exe") == 0;
    }
    int rewritten = emuroot_translate_path(t->ctx, t->root, guest, follow, host, sizeof host);
    if (t->ctx->has_fake_ids && t->creds.uid[1] == 0)
        emuroot_grant_access(sysno, regs, which, host);
    if (t->ctx->verbose) {
        fprintf(stderr, "[emuroot] pid=%d sys=%ld \"%s\" -> \"%s\"%s", (int)t->pid, sysno, given,
                host, rewritten ? "" : " (unchanged)");
        t->logged = 1;
    }
    if (!rewritten) return;

    unsigned long scratch = emuroot_scratch(t, strlen(host) + 1);
    if (scratch && guest_write(t->pid, scratch, host, strlen(host) + 1) == 0) {
        *reg = scratch;
        regs_set(t->pid, regs);
    } else {
        fail_closed(t, regs, reg);
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
    if (!emuroot_translate_path(t->ctx, t->root, guest, 1, host, sizeof host)) return;
    if (strlen(host) >= sizeof sun.sun_path) {
        if (t->ctx->verbose)
            fprintf(stderr, "[emuroot] socket path too long to relocate: %s\n", host);
        return;
    }
    memset(sun.sun_path, 0, sizeof sun.sun_path);
    strcpy(sun.sun_path, host);
    unsigned long scratch = emuroot_scratch(t, sizeof sun);
    if (scratch && guest_write(t->pid, scratch, &sun, sizeof sun) == 0) {
        *addr_reg = scratch;
        *len_reg = offsetof(struct sockaddr_un, sun_path) + strlen(host) + 1;
        regs_set(t->pid, regs);
    } else {
        fail_closed(t, regs, addr_reg);
    }
}

/* The guest path (symlinks resolved) that path argument `slot` names. */
int emuroot_guest_path_arg(tracee_t *t, emuroot_regs_t *regs, long sysno, int slot, char *guest_out, char *host)
{
    char given[PATH_LEN], abs[2 * PATH_LEN];
    unsigned long ptr = *reg_arg(regs, slot);
    if (!ptr || guest_read_str(t->pid, ptr, given, sizeof given) < 0 || !given[0]) return 0;
    if (!absolute_guest_path(t, regs, dirfd_slot(sysno, slot), given, abs, sizeof abs)) return 0;
    emuroot_translate_path_ex(t->ctx, t->root, abs, 1, host, PATH_LEN, guest_out, PATH_LEN);
    return 1;
}

void emuroot_on_entry(tracee_t *t, emuroot_regs_t *regs)
{
    long sysno = (long)EMU_SYSNO(*regs);
    t->sysno = sysno;
    t->scratch_top = (EMU_SP(*regs) - SCRATCH_MARGIN) & ~0xFUL;
    t->proc_link = t->proc_link_exe = 0;
    t->pending_exe[0] = '\0';
    t->chown_valid = 0;
    t->created[0] = '\0';
    t->pending_root[0] = '\0';
    t->mount_handled = 0;
    for (int i = 0; i < 6; i++) t->args[i] = *reg_arg(regs, i + 1);
    if (t->ctx->has_fake_ids) {
        enum action action = emuroot_action_of(sysno);
        emuroot_identity_entry(t, regs, sysno, action);
        if (emuroot_emulate_mounts(t, regs, sysno, action)) return;
    }

#ifdef __NR_execve
    if (sysno == __NR_execve && emuroot_rewrite_exec(t, regs, 1, 2) != 0)
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
            emuroot_note_creation(t, regs, sysno, d);
            rewrite_path_arg(t, regs, sysno, d->slot1, emuroot_follow_last(t, regs, d), 0);
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
