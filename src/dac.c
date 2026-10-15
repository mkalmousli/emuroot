#include "tracee.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* Permission bits for the fake root (see tracee.h: emuroot_grant_access).
 *
 * Real root ignores permission bits; a faked root cannot. Files and directories
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

void emuroot_grant_access(long sysno, emuroot_regs_t *regs, int which, const char *host)
{
    ensure_dac(host, dac_for(sysno, regs, which));
}
