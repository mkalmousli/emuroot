"""Renders src/ptrace_engine.c - emuroot's Linux syscall-interception
backend. The path_syscalls[] table and the identity-faking logic in
handle_exit() are rendered from generator/spec/syscalls.py instead of
hand-written - add a syscall there and it appears here automatically.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from generator.spec.project import Project
from generator.banner import c_header
from generator.spec.syscalls import PATH_SYSCALLS, IDENTITY_ACTIONS

PATH = "src/ptrace_engine.c"

_HEAD = r"""
#define _GNU_SOURCE
#include "internal.h"
#include "arch.h"
#include "regs_io.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/utsname.h>

extern char **environ;

#define SCRATCH_SIZE 4096
#define SCRATCH_GAP  8192 /* distance below sp between path1 and path2 buffers */

typedef struct {
    struct emuroot_ctx *ctx;
    char cwd[EMUROOT_PATH_MAX];
    int in_syscall; /* toggles entry/exit on each PTRACE_SYSCALL stop */
    long cur_sysno;
} trace_state_t;

/* Which argument register(s) of a given syscall hold a path that the
 * kernel will resolve against the filesystem. slot is 1-6 (matching
 * EMUROOT_REG_ARGn) or 0 for "this syscall has no path in this slot". */
typedef struct {
    long sysno;
    int  path_slot1;
    int  path_slot2; /* second path, for rename/link-style syscalls; 0 if none */
} path_syscall_t;

/* GENERATED_PATH_SYSCALLS_TABLE */
#define N_PATH_SYSCALLS (sizeof(path_syscalls) / sizeof(path_syscalls[0]))

static unsigned long *arg_slot(emuroot_regs_t *r, int slot)
{
    switch (slot) {
        case 1: return (unsigned long *)&EMUROOT_REG_ARG1(*r);
        case 2: return (unsigned long *)&EMUROOT_REG_ARG2(*r);
        case 3: return (unsigned long *)&EMUROOT_REG_ARG3(*r);
        case 4: return (unsigned long *)&EMUROOT_REG_ARG4(*r);
        case 5: return (unsigned long *)&EMUROOT_REG_ARG5(*r);
        case 6: return (unsigned long *)&EMUROOT_REG_ARG6(*r);
        default: return NULL;
    }
}

static const path_syscall_t *lookup_path_syscall(long sysno)
{
    for (size_t i = 0; i < N_PATH_SYSCALLS; i++)
        if (path_syscalls[i].sysno == sysno)
            return &path_syscalls[i];
    return NULL;
}

static ssize_t vm_read_str(pid_t pid, unsigned long addr, char *buf, size_t bufsz)
{
    if (addr == 0) return -1;
    struct iovec local = { .iov_base = buf, .iov_len = bufsz - 1 };
    struct iovec remote = { .iov_base = (void *)addr, .iov_len = bufsz - 1 };
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (n < 0) return -1;
    buf[n] = '\0';
    /* trim at first NUL if the string was shorter than the whole read */
    size_t len = strnlen(buf, (size_t)n);
    buf[len] = '\0';
    return (ssize_t)len;
}

static int vm_write(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    struct iovec local = { .iov_base = (void *)data, .iov_len = len };
    struct iovec remote = { .iov_base = (void *)addr, .iov_len = len };
    ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
    return (n == (ssize_t)len) ? 0 : -1;
}

/* Redirect the register at reg_field (holding a guest path pointer) to a
 * scratch buffer below the stack containing the translated host path.
 * which selects a distinct scratch slot so two paths in the same
 * syscall (rename, link, ...) don't clobber each other. */
static int redirect_path_arg(pid_t pid, emuroot_regs_t *regs,
                              unsigned long *reg_field, const char *newpath,
                              int which)
{
    size_t len = strlen(newpath) + 1;
    if (len > SCRATCH_SIZE) return -1;
    unsigned long scratch = (unsigned long)EMUROOT_REG_SP(*regs)
                             - SCRATCH_GAP - (unsigned long)which * SCRATCH_SIZE;
    scratch &= ~0xFUL;
    if (vm_write(pid, scratch, newpath, len) != 0) return -1;
    *reg_field = scratch;
    return regs_set(pid, regs);
}

/* Translate and (if needed) redirect one path argument. Returns the
 * guest-visible path string via out_guest (for cwd tracking on chdir),
 * or NULL if this slot wasn't present / couldn't be read. */
static int handle_one_path(trace_state_t *st, pid_t pid, emuroot_regs_t *regs,
                            long sysno, int slot, int which,
                            char *out_guest, size_t out_guest_sz)
{
    if (slot == 0) return 0;
    unsigned long *reg_field = arg_slot(regs, slot);
    unsigned long path_ptr = *reg_field;
    if (path_ptr == 0) return 0;

    char guest_path[EMUROOT_PATH_MAX];
    if (vm_read_str(pid, path_ptr, guest_path, sizeof(guest_path)) < 0)
        return 0;

    char host_path[EMUROOT_PATH_MAX];
    int rewritten = emuroot_translate_path(st->ctx, st->cwd, guest_path,
                                            host_path, sizeof(host_path));
    if (st->ctx->verbose)
        fprintf(stderr, "[emuroot] sys=%ld path=\"%s\" -> \"%s\"%s\n",
                sysno, guest_path, host_path, rewritten ? "" : " (unchanged)");

    if (rewritten > 0)
        redirect_path_arg(pid, regs, reg_field, host_path, which);

    if (out_guest) {
        strncpy(out_guest, guest_path, out_guest_sz - 1);
        out_guest[out_guest_sz - 1] = '\0';
    }
    return 1;
}

static void handle_entry(trace_state_t *st, pid_t pid, emuroot_regs_t *regs)
{
    long sysno = (long)EMUROOT_REG_SYSNO(*regs);
    st->cur_sysno = sysno;

    const path_syscall_t *desc = lookup_path_syscall(sysno);
    if (!desc) return;

    char guest_path1[EMUROOT_PATH_MAX] = {0};
    handle_one_path(st, pid, regs, sysno, desc->path_slot1, 0,
                     guest_path1, sizeof(guest_path1));
    handle_one_path(st, pid, regs, sysno, desc->path_slot2, 1, NULL, 0);

#if defined(SC_chdir)
    if (sysno == SC_chdir && guest_path1[0]) {
        /* Track cwd from the guest's perspective for relative-path resolution. */
        strncpy(st->cwd, guest_path1, sizeof(st->cwd) - 1);
        st->cwd[sizeof(st->cwd) - 1] = '\0';
    }
#endif
}

static void handle_exit(trace_state_t *st, pid_t pid, emuroot_regs_t *regs)
{
    long sysno = st->cur_sysno;
    struct emuroot_ctx *ctx = st->ctx;

    if (ctx->has_fake_ids) {
/* GENERATED_IDENTITY_ACTIONS */
    }

#if defined(SC_uname)
    if (sysno == SC_uname && ctx->has_fake_uname) {
        long ret = (long)EMUROOT_REG_RET(*regs);
        if (ret == 0) {
            struct utsname u;
            memset(&u, 0, sizeof(u));
            strncpy(u.sysname, ctx->uname_sysname, sizeof(u.sysname) - 1);
            strncpy(u.release, ctx->uname_release, sizeof(u.release) - 1);
            strncpy(u.machine, ctx->uname_machine, sizeof(u.machine) - 1);
            /* ARG1 at entry held the buffer pointer; still valid post-syscall
             * since the tracee's registers are otherwise untouched here. */
            vm_write(pid, EMUROOT_REG_ARG1(*regs), &u, sizeof(u));
        }
    }
#endif
}

int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[])
{
    pid_t pid = fork();
    if (pid < 0)
        return EMUROOT_ERR_SYSTEM;

    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
            _exit(127);
        raise(SIGSTOP);
        if (envp)
            environ = envp; /* so execvp's PATH search sees the right env */
        execvp(argv[0], argv);
        _exit(127);
    }

    ctx->child_pid = pid;
    int status;
    waitpid(pid, &status, 0); /* consume the initial SIGSTOP */

    ptrace(PTRACE_SETOPTIONS, pid, 0,
           (void *)(long)(PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL));

    trace_state_t st;
    memset(&st, 0, sizeof(st));
    st.ctx = ctx;
    if (!getcwd(st.cwd, sizeof(st.cwd)))
        strncpy(st.cwd, "/", sizeof(st.cwd));

    ptrace(PTRACE_SYSCALL, pid, NULL, NULL);

    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);

        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == (SIGTRAP | 0x80)) {
                emuroot_regs_t regs;
                if (regs_get(pid, &regs) == 0) {
                    if (!st.in_syscall)
                        handle_entry(&st, pid, &regs);
                    else
                        handle_exit(&st, pid, &regs);
                }
                st.in_syscall = !st.in_syscall;
                ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
            } else if (sig == SIGTRAP) {
                ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
            } else {
                /* forward any other signal to the tracee unmodified */
                ptrace(PTRACE_SYSCALL, pid, NULL, (void *)(long)sig);
            }
        }
    }

    return EMUROOT_ERR_SYSTEM;
}
"""


def _render_path_syscalls_table() -> str:
    lines = [
        "/* Built once from src/arch.h's SC_* defines - only the syscalls that",
        " * actually exist for the host architecture appear here, everything else",
        " * is compiled out. Two-path entries (rename/link and their *at forms)",
        " * redirect both paths; symlink-style entries only redirect the",
        " * link-location argument, never the (unresolved) target string.",
        " * Generated from generator/spec/syscalls.py:PATH_SYSCALLS. */",
        "static const path_syscall_t path_syscalls[] = {",
    ]
    for macro, slot1, slot2 in PATH_SYSCALLS:
        lines.append(f"#if defined({macro})")
        lines.append(f"    {{ {macro}, {slot1}, {slot2} }},")
        lines.append("#endif")
    lines.append("};")
    return "\n".join(lines)


_ACTION_TEMPLATES = {
    "set_ret_uid": (
        "        if ({cond}) {{\n"
        "            EMUROOT_REG_SET_RET(*regs, ctx->fake_uid);\n"
        "            regs_set(pid, regs);\n"
        "        }}"
    ),
    "set_ret_gid": (
        "        if ({cond}) {{\n"
        "            EMUROOT_REG_SET_RET(*regs, ctx->fake_gid);\n"
        "            regs_set(pid, regs);\n"
        "        }}"
    ),
    "set_ret_zero": (
        "        if ({cond}) {{\n"
        "            /* Pretend privileged id changes always succeed. */\n"
        "            EMUROOT_REG_SET_RET(*regs, 0);\n"
        "            regs_set(pid, regs);\n"
        "        }}"
    ),
    "fill_uid3": (
        "        if ({cond} && (long)EMUROOT_REG_RET(*regs) == 0) {{\n"
        "            unsigned int uid = ctx->fake_uid;\n"
        "            vm_write(pid, EMUROOT_REG_ARG1(*regs), &uid, sizeof(uid));\n"
        "            vm_write(pid, EMUROOT_REG_ARG2(*regs), &uid, sizeof(uid));\n"
        "            vm_write(pid, EMUROOT_REG_ARG3(*regs), &uid, sizeof(uid));\n"
        "        }}"
    ),
    "fill_gid3": (
        "        if ({cond} && (long)EMUROOT_REG_RET(*regs) == 0) {{\n"
        "            unsigned int gid = ctx->fake_gid;\n"
        "            vm_write(pid, EMUROOT_REG_ARG1(*regs), &gid, sizeof(gid));\n"
        "            vm_write(pid, EMUROOT_REG_ARG2(*regs), &gid, sizeof(gid));\n"
        "            vm_write(pid, EMUROOT_REG_ARG3(*regs), &gid, sizeof(gid));\n"
        "        }}"
    ),
}


def _render_identity_actions() -> str:
    blocks = []
    for macros, action in IDENTITY_ACTIONS:
        guard = " || ".join(f"defined({m})" for m in macros)
        cond = " || ".join(f"sysno == {m}" for m in macros)
        body = _ACTION_TEMPLATES[action].format(cond=cond)
        blocks.append(f"#if {guard}\n{body}\n#endif")
    return "\n".join(blocks)


def render(p: Project) -> str:
    banner = c_header(p, "ptrace_engine.c - emuroot's Linux syscall-interception backend.", (
        "Independent implementation: forks a tracee, traces every syscall with\n"
        "PTRACE_SYSCALL, and on syscall-entry looks the syscall number up in a\n"
        "table (path_syscalls[] below, generated from generator/spec/syscalls.py)\n"
        "describing which argument registers, if any, hold filesystem paths for\n"
        "that syscall. Every path found is read out of the tracee's memory via\n"
        "process_vm_readv, translated against the active root/bindings, and - if\n"
        "it needs redirecting - written into a scratch buffer in the tracee's\n"
        "own address space with the syscall's argument register repointed at\n"
        "it, before the kernel executes the (now-rewritten) syscall. On syscall\n"
        "exit, a handful of identity syscalls (getuid/getresuid/uname/...) have\n"
        "their result patched to the configured fake identity.\n"
        "\n"
        "The table covers every path-taking syscall emuroot's target kernel\n"
        "headers expose for the host's architecture (see src/arch.h), including\n"
        "both the classic single-path calls (open, stat, chdir, ...) and the\n"
        "dirfd-relative *at() family (openat, fchownat, renameat2, ...), so\n"
        "this backend does not depend on the traced program using any specific\n"
        "subset of the filesystem API - whatever syscall it makes, if it takes\n"
        "a path, emuroot sees and can redirect it."
    ))
    body = _HEAD.replace("/* GENERATED_PATH_SYSCALLS_TABLE */", _render_path_syscalls_table())
    body = body.replace("/* GENERATED_IDENTITY_ACTIONS */", _render_identity_actions())
    return banner + "\n" + body
