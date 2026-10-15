/* Reading and writing the traced process's memory. */
#ifndef EMUROOT_GUEST_H
#define EMUROOT_GUEST_H

#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>

static inline ssize_t guest_read(pid_t pid, unsigned long addr, void *buf, size_t len)
{
    struct iovec local = { buf, len }, remote = { (void *)addr, len };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

static inline int guest_write(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    struct iovec local = { (void *)data, len }, remote = { (void *)addr, len };
    return process_vm_writev(pid, &local, 1, &remote, 1, 0) == (ssize_t)len ? 0 : -1;
}

/* NUL-terminated string of at most size-1 bytes; 0 on success. */
static inline int guest_read_str(pid_t pid, unsigned long addr, char *buf, size_t size)
{
    ssize_t n = guest_read(pid, addr, buf, size - 1);
    if (n <= 0) return -1;
    buf[strnlen(buf, (size_t)n)] = '\0';
    return 0;
}

#endif
