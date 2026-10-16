/* What this kernel and sandbox let emuroot do: tries each mechanism the engine
 * can use (in a throwaway traced child) and reports. The engine adapts to this
 * by itself at run time; this is for people, via `emuroot --doctor` or
 * emuroot_diagnose() in an app. */
#include "internal.h"
#include "arch.h"
#include "guest.h"

#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

static volatile long probe_value = 0x5a5a1234;      /* what the probe reads out of the child */

static int succeeds(int ok) { return ok ? 1 : 0; }

int emuroot_diagnose(char *buf, size_t len)
{
    char out[2048];
    size_t n = 0;
#define SAY(...) (n += (size_t)snprintf(out + n, sizeof out - n, __VA_ARGS__))
#define ROW(name, ok, note) SAY("  %-28s %s%s%s\n", name, (ok) ? "yes" : "no ", (note)[0] ? "  - " : "", note)

    struct utsname u;
    uname(&u);
    SAY("emuroot %d.%d.%d on %s, Linux %s\n", EMUROOT_VERSION_MAJOR, EMUROOT_VERSION_MINOR,
        EMUROOT_VERSION_PATCH, EMU_ARCH_NAME, u.release);

    int traced = 0, regset = 0, getregs = 0, vm = 0, procmem = 0, peek = 0, follow = 0, exitkill = 0;
    pid_t pid = fork();
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) _exit(1);
        raise(SIGSTOP);
        _exit(0);
    }
    int status;
    if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFSTOPPED(status)) {
        traced = 1;
        emuroot_regs_t regs;
        struct iovec iov = { &regs, sizeof regs };
        regset = succeeds(ptrace(PTRACE_GETREGSET, pid, (void *)1, &iov) == 0);
        unsigned char full[1024];
        getregs = succeeds(ptrace(12 /* PTRACE_GETREGS */, pid, NULL, full) == 0);

        long got = 0;
        struct iovec local = { &got, sizeof got }, remote = { (void *)&probe_value, sizeof got };
        vm = succeeds(emuroot_process_vm(0, pid, &local, &remote) == (ssize_t)sizeof got && got == probe_value);
        char path[64];
        snprintf(path, sizeof path, "/proc/%d/mem", (int)pid);
        int fd = open(path, O_RDONLY);
        got = 0;
        procmem = succeeds(fd >= 0 && pread(fd, &got, sizeof got, (off_t)(uintptr_t)&probe_value) == (ssize_t)sizeof got && got == probe_value);
        if (fd >= 0) close(fd);
        errno = 0;
        peek = succeeds(ptrace(PTRACE_PEEKDATA, pid, (void *)&probe_value, NULL) == probe_value && errno == 0);

        long base = PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE;
        follow = succeeds(ptrace(PTRACE_SETOPTIONS, pid, 0, (void *)base) == 0);
        exitkill = succeeds(ptrace(PTRACE_SETOPTIONS, pid, 0, (void *)(base | PTRACE_O_EXITKILL)) == 0);
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
    }

    int seccomp = 0;
    pid = fork();
    if (pid == 0) _exit(emuroot_seccomp_installable() ? 0 : 1);
    if (pid > 0 && waitpid(pid, &status, 0) == pid) seccomp = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    ROW("trace a child (ptrace)", traced, traced ? "" : "full emulation unavailable; env-only fallback is used");
    ROW("follow fork/clone", follow, "");
    ROW("registers: GETREGSET", regset, regset ? "" : getregs ? "older kernel; PTRACE_GETREGS is used" : "");
    ROW("registers: GETREGS", getregs, "");
    ROW("guest memory: process_vm", vm, vm ? "" : "denied here; the next method is used");
    ROW("guest memory: /proc/PID/mem", procmem, "");
    ROW("guest memory: PEEK/POKE", peek, "always allowed for a tracer");
    ROW("kill guests if we die", exitkill, exitkill ? "" : "kernel before 3.8; emuroot does it itself");
    ROW("seccomp fast path", seccomp && emuroot_seccomp_supported(),
        seccomp ? (emuroot_seccomp_supported() ? "" : "kernel before 4.8; every syscall is traced") : "not permitted here; every syscall is traced");
    int usable = traced && follow && (regset || getregs) && (vm || procmem || peek);
    SAY("=> %s\n", usable ? "full emulation available" : "degraded: env-only fallback");
    snprintf(buf, len, "%s", out);
    return usable ? 0 : 1;
}
