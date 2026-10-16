/* Access to the traced process's memory (see guest.h).
 *
 * Three ways, tried in turn, because sandboxes disagree about which they allow:
 *   1. process_vm_readv/writev - fastest, but seccomp profiles may forbid the
 *      syscalls, and SELinux can refuse it for a process in another domain
 *      (Android 7 does, once a program changes domain);
 *   2. /proc/PID/mem - needs the same ptrace-access check;
 *   3. PTRACE_PEEKDATA/POKEDATA - slow (a word at a time), but a tracer is always
 *      allowed it: the check was made once, when it attached.
 * A failure that is not "this method is not permitted" (a bad address, EFAULT)
 * is reported as it is, without trying another method. */
#include "guest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <unistd.h>

enum method { PROCESS_VM, PROC_MEM, PEEK_POKE };

static int method = -1;         /* the method currently in use; -1 = not decided yet */

/* EMUROOT_GUEST_MEM=procmem|peek starts with a later method (for testing them);
 * EMUROOT_NO_PROCESS_VM is the older spelling of procmem. */
static enum method current(void)
{
    if (method < 0) {
        const char *want = getenv("EMUROOT_GUEST_MEM");
        method = PROCESS_VM;
        if (getenv("EMUROOT_NO_PROCESS_VM") || (want && strcmp(want, "procmem") == 0)) method = PROC_MEM;
        if (want && strcmp(want, "peek") == 0) method = PEEK_POKE;
    }
    return (enum method)method;
}

/* Errors that mean "not this way", as opposed to "no such memory". */
static int denied(int err) { return err != EFAULT && err != ESRCH && err != EIO && err != EINVAL; }

static ssize_t proc_mem(pid_t pid, unsigned long addr, void *buf, size_t len, int write_it)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/mem", (int)pid);
    int fd = open(path, write_it ? O_WRONLY : O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = write_it ? pwrite(fd, buf, len, (off_t)addr) : pread(fd, buf, len, (off_t)addr);
    int saved = errno;
    close(fd);
    errno = saved;
    return n;
}

/* Word-at-a-time access; stops at the first word that can't be accessed. */
static ssize_t peek_poke(pid_t pid, unsigned long addr, void *buf, size_t len, int write_it)
{
    const size_t w = sizeof(long);
    size_t done = 0;
    while (done < len) {
        unsigned long at = (addr + done) & ~(unsigned long)(w - 1);
        size_t off = (addr + done) - at, chunk = w - off < len - done ? w - off : len - done;
        union { long word; unsigned char bytes[sizeof(long)]; } cell;
        errno = 0;
        cell.word = ptrace(PTRACE_PEEKDATA, pid, (void *)at, NULL);
        if (errno) break;
        if (write_it) {
            memcpy(cell.bytes + off, (unsigned char *)buf + done, chunk);
            if (ptrace(PTRACE_POKEDATA, pid, (void *)at, (void *)cell.word) != 0) break;
        } else {
            memcpy((unsigned char *)buf + done, cell.bytes + off, chunk);
        }
        done += chunk;
    }
    return done == 0 && len > 0 ? -1 : (ssize_t)done;
}

static ssize_t transfer(pid_t pid, unsigned long addr, void *buf, size_t len, int write_it)
{
    for (;;) {
        ssize_t n;
        switch (current()) {
        case PROCESS_VM: {
            struct iovec local = { buf, len }, remote = { (void *)addr, len };
            n = write_it ? process_vm_writev(pid, &local, 1, &remote, 1, 0)
                         : process_vm_readv(pid, &local, 1, &remote, 1, 0);
            break;
        }
        case PROC_MEM:  n = proc_mem(pid, addr, buf, len, write_it); break;
        default:        n = peek_poke(pid, addr, buf, len, write_it); break;
        }
        if (n >= 0 || !denied(errno) || current() == PEEK_POKE) return n;
        method = current() + 1;                         /* not permitted: use the next way */
    }
}

ssize_t guest_read(pid_t pid, unsigned long addr, void *buf, size_t len)
{
    return transfer(pid, addr, buf, len, 0);
}

int guest_write(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    return transfer(pid, addr, (void *)data, len, 1) == (ssize_t)len ? 0 : -1;
}

int guest_read_str(pid_t pid, unsigned long addr, char *buf, size_t size)
{
    /* In small chunks until the NUL: most paths are short, and the PEEK method is slow. */
    size_t have = 0;
    while (have + 1 < size) {
        size_t want = size - 1 - have < 128 ? size - 1 - have : 128;
        ssize_t n = guest_read(pid, addr + have, buf + have, want);
        if (n <= 0) {
            if (have == 0) return -1;
            break;                                  /* ran into unmapped memory after some text */
        }
        char *nul = memchr(buf + have, '\0', (size_t)n);
        have += (size_t)n;
        if (nul) { buf[have] = '\0'; return 0; }
        if ((size_t)n < want) break;
    }
    buf[have] = '\0';
    return have ? 0 : -1;
}
