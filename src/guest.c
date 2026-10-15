/* Access to the traced process's memory (see guest.h). */
#include "guest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

static int use_proc_mem = -1;   /* 1 once process_vm_* proved forbidden; -1 = not yet decided */

/* EMUROOT_NO_PROCESS_VM=1 forces the fallback (for testing it). */
static int should_use_proc_mem(void)
{
    if (use_proc_mem < 0) use_proc_mem = getenv("EMUROOT_NO_PROCESS_VM") != NULL;
    return use_proc_mem;
}

/* One transfer through /proc/<pid>/mem; works for a ptrace tracer in any sandbox
 * that lets it attach at all. */
static ssize_t proc_mem(pid_t pid, unsigned long addr, void *buf, size_t len, int write_it)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/mem", (int)pid);
    int fd = open(path, write_it ? O_WRONLY : O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = write_it ? pwrite(fd, buf, len, (off_t)addr) : pread(fd, buf, len, (off_t)addr);
    close(fd);
    return n;
}

ssize_t guest_read(pid_t pid, unsigned long addr, void *buf, size_t len)
{
    if (!should_use_proc_mem()) {
        struct iovec local = { buf, len }, remote = { (void *)addr, len };
        ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
        if (n >= 0 || (errno != EPERM && errno != ENOSYS)) return n;
        use_proc_mem = 1;
    }
    return proc_mem(pid, addr, buf, len, 0);
}

int guest_write(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    if (!should_use_proc_mem()) {
        struct iovec local = { (void *)data, len }, remote = { (void *)addr, len };
        ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
        if (n >= 0 || (errno != EPERM && errno != ENOSYS)) return n == (ssize_t)len ? 0 : -1;
        use_proc_mem = 1;
    }
    return proc_mem(pid, addr, (void *)data, len, 1) == (ssize_t)len ? 0 : -1;
}

int guest_read_str(pid_t pid, unsigned long addr, char *buf, size_t size)
{
    ssize_t n = guest_read(pid, addr, buf, size - 1);
    if (n <= 0) return -1;
    buf[strnlen(buf, (size_t)n)] = '\0';
    return 0;
}
