/* seccomp-bpf fast path for the ptrace engine.
 *
 * Plain PTRACE_SYSCALL stops the guest twice for every syscall, which makes
 * I/O-heavy programs several times slower. A seccomp filter instead returns
 * SECCOMP_RET_TRACE only for the syscalls emuroot handles (paths, ids, stat,
 * exec, sockets); every other syscall (read, write, mmap, futex, ...) runs at
 * full speed. Needs Linux >= 4.8, where the stop order became well defined. */
#include "internal.h"
#include "arch.h"

#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef EMU_AUDIT_ARCH
#include <linux/filter.h>
#include <linux/seccomp.h>

#define MAX_FILTERED 250    /* BPF conditional jumps reach at most 255 instructions */

int emuroot_seccomp_supported(void)
{
    struct utsname u;
    int major = 0, minor = 0;
    if (uname(&u) != 0 || sscanf(u.release, "%d.%d", &major, &minor) != 2) return 0;
    return major > 4 || (major == 4 && minor >= 8);
}

/* Installs a filter that traces exactly the syscalls in nrs[0..n). 0 on success. */
int emuroot_seccomp_trace(const long *nrs, size_t n)
{
    if (n == 0 || n > MAX_FILTERED) return -1;

    struct sock_filter prog[MAX_FILTERED + 8];
    size_t k = 0;
    /* foreign ABI (e.g. 32-bit code on a 64-bit kernel): not ours, let it run */
    prog[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    prog[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, EMU_AUDIT_ARCH, 1, 0);
    prog[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    prog[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    for (size_t i = 0; i < n; i++)       /* a match jumps over the ALLOW to the TRACE return */
        prog[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)nrs[i],
                                                 (unsigned char)(n - i), 0);
    prog[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    prog[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE);

    struct sock_fprog fprog = { .len = (unsigned short)k, .filter = prog };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return -1;
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &fprog) == 0 ? 0 : -1;
}

#else   /* an architecture whose AUDIT_ARCH value isn't known: plain tracing only */

int emuroot_seccomp_supported(void) { return 0; }
int emuroot_seccomp_trace(const long *nrs, size_t n) { (void)nrs; (void)n; return -1; }

#endif
