/* chroot, mount and umount, emulated for a privileged fake root.
 *
 *   chroot  changes the emulated root of that process (and its children)
 *   mount   --bind and proc/sysfs/dev mounts become runtime bindings;
 *           anything else is accepted and ignored
 *   umount  drops the binding
 *
 * The real call is always made harmless first (see neutralize()). */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef MS_BIND
#define MS_BIND 4096
#endif
#ifndef MS_REMOUNT
#define MS_REMOUNT 32
#endif
#ifndef MS_MOVE
#define MS_MOVE 8192
#endif

/* Makes the real call harmless: the kernel is handed a NULL/"/" argument so it
 * changes nothing (a real chroot("/") is a no-op; the rest just fail with EFAULT),
 * and the exit handler (fake.c) reports success. */
static void neutralize(tracee_t *t, emuroot_regs_t *regs, int slot, int to_root)
{
    unsigned long *reg = reg_arg(regs, slot);
    unsigned long slash = to_root ? emuroot_scratch(t, 2) : 0;
    if (to_root && (!slash || guest_write(t->pid, slash, "/", 2) != 0)) slash = 0;
    *reg = slash;
    regs_set(t->pid, regs);
}

static int guest_dir_exists(const char *host)
{
    struct stat st;
    return stat(host, &st) == 0 && S_ISDIR(st.st_mode);
}

static void emulate_chroot(tracee_t *t, emuroot_regs_t *regs, long sysno, const path_syscall_t *d)
{
    char host[PATH_LEN], canon[PATH_LEN];
    if (!emuroot_host_path_arg(t, regs, sysno, d->slot1, 1, host) || !guest_dir_exists(host)) return;
    snprintf(t->pending_root, sizeof t->pending_root, "%s", realpath(host, canon) ? canon : host);
    neutralize(t, regs, d->slot1, 1);
}

/* mount(source, target, fstype, flags, data): bind mounts, and the pseudo
 * filesystems the host can provide, become runtime bindings; the rest are
 * accepted and ignored (a tmpfs is just the directory that is already there). */
static void emulate_mount(tracee_t *t, emuroot_regs_t *regs, long sysno)
{
    char guest_target[PATH_LEN], host_target[PATH_LEN], host_source[PATH_LEN], fstype[64] = "";
    unsigned long flags = *reg_arg(regs, 4);
    if (!emuroot_guest_path_arg(t, regs, sysno, 2, guest_target, host_target)) return;

    const char *host = NULL;
    if (!(flags & (MS_REMOUNT | MS_MOVE))) {
        if (flags & MS_BIND) {
            if (emuroot_host_path_arg(t, regs, sysno, 1, 1, host_source)) host = host_source;
        } else if (*reg_arg(regs, 3) && guest_read_str(t->pid, *reg_arg(regs, 3), fstype, sizeof fstype) == 0) {
            if (!strcmp(fstype, "proc")) host = "/proc";
            else if (!strcmp(fstype, "sysfs")) host = "/sys";
            else if (!strcmp(fstype, "devtmpfs")) host = "/dev";
            else if (!strcmp(fstype, "devpts")) host = "/dev/pts";
        }
    }
    if (host) {
        char canon[PATH_LEN];
        if (emuroot_bind(t->ctx, realpath(host, canon) ? canon : host, guest_target) == EMUROOT_OK) {
            binding_t *b = &t->ctx->bindings[t->ctx->nbindings - 1];
            snprintf(b->mount, sizeof b->mount, "%s", host_target);
            b->mount_len = strlen(b->mount);
        }
    }
    if (t->ctx->verbose)
        fprintf(stderr, "[emuroot] pid=%d mount at \"%s\"%s%s\n", (int)t->pid, guest_target,
                host ? " from " : " (accepted, nothing to do)", host ? host : "");
    t->mount_handled = 1;
    neutralize(t, regs, 2, 0);
}

static void emulate_umount(tracee_t *t, emuroot_regs_t *regs, long sysno)
{
    char guest_target[PATH_LEN], host_target[PATH_LEN];
    if (!emuroot_guest_path_arg(t, regs, sysno, 1, guest_target, host_target)) return;
    emuroot_unbind(t->ctx, guest_target);
    t->mount_handled = 1;
    neutralize(t, regs, 1, 0);
}

/* Returns 1 if the call was emulated (nothing more to rewrite for it). */
int emuroot_emulate_mounts(tracee_t *t, emuroot_regs_t *regs, long sysno, enum action action)
{
    if (t->creds.uid[1] != 0) return 0;                 /* only a root may do these */
    switch (action) {
    case A_CHROOT:
        emulate_chroot(t, regs, sysno, emuroot_find_path_syscall(sysno));
        return t->pending_root[0] != '\0';
    case A_MOUNT:  emulate_mount(t, regs, sysno);  return 1;
    case A_UMOUNT: emulate_umount(t, regs, sysno); return 1;
    default:       return 0;
    }
}
