/* Register access for a stopped tracee.
 *
 * PTRACE_GETREGSET (Linux 2.6.34) is the portable way and works with our
 * deliberately short register structs. Older kernels (Android 1.x - 2.2, and
 * old vendor kernels) only have PTRACE_GETREGS / PTRACE_SETREGS, which move the
 * kernel's whole register struct, so those go through a larger buffer and we
 * overlay our prefix on it. */
#include "arch.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>

#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS 12
#define PTRACE_SETREGS 13
#endif

enum { UNDECIDED, REGSET, GETREGS };
static int mode;
#define KERNEL_REGS_MAX 1024     /* comfortably above every user_regs_struct */

static int mode_now(void)
{
    if (!mode) {
        const char *forced = getenv("EMUROOT_REGS");            /* for testing the fallback */
        mode = forced && strcmp(forced, "getregs") == 0 ? GETREGS : REGSET;
    }
    return mode;
}

int regs_get(pid_t pid, emuroot_regs_t *regs)
{
    if (mode_now() == REGSET) {
        struct iovec iov = { regs, sizeof(*regs) };
        if (ptrace(PTRACE_GETREGSET, pid, (void *)1 /* NT_PRSTATUS */, &iov) == 0) return 0;
        if (errno != EIO && errno != EINVAL && errno != ENOSYS) return -1;   /* e.g. ESRCH: it's gone */
        mode = GETREGS;                                         /* a kernel without regsets */
    }
    unsigned char full[KERNEL_REGS_MAX];
    if (ptrace(PTRACE_GETREGS, pid, NULL, full) != 0) return -1;
    memcpy(regs, full, sizeof *regs);
    return 0;
}

int regs_set(pid_t pid, emuroot_regs_t *regs)
{
    if (mode_now() == REGSET) {
        struct iovec iov = { regs, sizeof(*regs) };
        return ptrace(PTRACE_SETREGSET, pid, (void *)1 /* NT_PRSTATUS */, &iov);
    }
    unsigned char full[KERNEL_REGS_MAX];                        /* read-modify-write the whole struct */
    if (ptrace(PTRACE_GETREGS, pid, NULL, full) != 0) return -1;
    memcpy(full, regs, sizeof *regs);
    return ptrace(PTRACE_SETREGS, pid, NULL, full);
}
