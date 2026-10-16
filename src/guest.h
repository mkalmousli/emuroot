/* Reading and writing the traced process's memory. */
#ifndef EMUROOT_GUEST_H
#define EMUROOT_GUEST_H

#include <sys/types.h>
#include <sys/uio.h>

/* Bytes transferred, or -1. Uses process_vm_readv/writev and, where a sandbox
 * (seccomp, gVisor, ...) forbids those, falls back to /proc/<pid>/mem. */
ssize_t guest_read(pid_t pid, unsigned long addr, void *buf, size_t len);
int guest_write(pid_t pid, unsigned long addr, const void *data, size_t len);   /* 0 on success */

/* process_vm_readv/writev as raw syscalls: bionic only declares the wrappers from
 * Android 6 (API 23), and we support Android 5. -1 with errno ENOSYS where the
 * headers don't know the syscall. */
ssize_t emuroot_process_vm(int write_it, pid_t pid, const struct iovec *local, const struct iovec *remote);

/* NUL-terminated string of at most size-1 bytes; 0 on success. */
int guest_read_str(pid_t pid, unsigned long addr, char *buf, size_t size);

#endif
