/* Linux backend: traces the guest and every process/thread it spawns, hands
 * each syscall stop to rewrite.c (entry) or fake.c (exit), and returns the
 * guest's exit status. With a seccomp filter (seccomp.c) only the syscalls we
 * handle stop at all; otherwise every syscall does (PTRACE_SYSCALL). */
#include "tracee.h"
#include "guest.h"
#include "syscalls.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif

extern char **environ;

/* All live tracees. Pointers stay valid until the tracee is removed. */
typedef struct { tracee_t **v; size_t n, cap; } tracee_table_t;

static tracee_t *tracee_find(tracee_table_t *tab, pid_t pid)
{
    for (size_t i = 0; i < tab->n; i++)
        if (tab->v[i]->pid == pid) return tab->v[i];
    return NULL;
}

/* A forked child starts as a copy of its parent. */
static void tracee_inherit(tracee_t *child, const tracee_t *parent)
{
    child->creds = parent->creds;
    memcpy(child->exe, parent->exe, sizeof child->exe);
    memcpy(child->root, parent->root, sizeof child->root);
}

static tracee_t *tracee_add(tracee_table_t *tab, struct emuroot_ctx *ctx, pid_t pid,
                            const tracee_t *parent)
{
    if (tab->n == tab->cap) {
        size_t cap = tab->cap ? tab->cap * 2 : 64;
        tracee_t **v = realloc(tab->v, cap * sizeof *v);
        if (!v) return NULL;
        tab->v = v;
        tab->cap = cap;
    }
    tracee_t *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->pid = pid;
    t->ctx = ctx;
    snprintf(t->root, sizeof t->root, "%s", ctx->root);
    t->fresh = 1;
    if (parent) tracee_inherit(t, parent);
    else        creds_init(&t->creds, ctx->fake_uid, ctx->fake_gid);
    tab->v[tab->n++] = t;
    return t;
}

static void tracee_remove(tracee_table_t *tab, tracee_t *t)
{
    for (size_t i = 0; i < tab->n; i++)
        if (tab->v[i] == t) { tab->v[i] = tab->v[--tab->n]; break; }
    free(t);
}

/* Every syscall number emuroot handles; the seccomp filter traces exactly these. */
static size_t traced_syscalls(long *out)
{
    size_t n = 0;
    for (size_t i = 0; i < n_path_syscalls; i++)
        out[n++] = path_syscalls[i].sysno;
    for (size_t i = 0; i < n_action_syscalls; i++)
        out[n++] = action_syscalls[i].sysno;
#ifdef __NR_execve
    out[n++] = __NR_execve;
#endif
#ifdef __NR_bind
    out[n++] = __NR_bind;
#endif
#ifdef __NR_connect
    out[n++] = __NR_connect;
#endif
#ifdef __NR_sendto
    out[n++] = __NR_sendto;
#endif
    return n;
}

/* Handle one stop of a traced process. Returns the signal to deliver on resume. */
static long handle_stop(tracee_table_t *tab, tracee_t *t, int status, int seccomp_mode)
{
    pid_t pid = t->pid;
    int sig = WSTOPSIG(status), event = status >> 16;
    emuroot_regs_t regs;

    if (sig == (SIGTRAP | 0x80)) {                  /* a syscall entry or exit stop */
        if (!seccomp_mode) {
            if (regs_get(pid, &regs) == 0) {
                if (t->in_syscall) emuroot_on_exit(t, &regs);
                else               emuroot_on_entry(t, &regs);
            }
            t->in_syscall = !t->in_syscall;
        } else if (t->awaiting_exit) {              /* (no entry stop follows a seccomp stop) */
            if (regs_get(pid, &regs) == 0) emuroot_on_exit(t, &regs);
            t->awaiting_exit = 0;
        }
    } else if (event == PTRACE_EVENT_SECCOMP) {     /* entry of a syscall we handle */
        if (regs_get(pid, &regs) == 0) emuroot_on_entry(t, &regs);
        t->awaiting_exit = 1;
    } else if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK ||
               event == PTRACE_EVENT_CLONE) {
        unsigned long child = 0;
        ptrace(PTRACE_GETEVENTMSG, pid, NULL, &child);
        tracee_t *c = tracee_find(tab, (pid_t)child);
        if (c) tracee_inherit(c, t);
        else   tracee_add(tab, t->ctx, (pid_t)child, t);
    } else if (sig == SIGSTOP && t->fresh) {
        t->fresh = 0;                               /* a new child's initial stop */
    } else if (sig != SIGTRAP) {
        return sig;                                 /* deliver other signals unchanged */
    }
    return 0;
}

int emuroot_run_ptrace(struct emuroot_ctx *ctx, char *const argv[], char *const envp[])
{
    /* The kernel reports canonical paths (getcwd, /proc/pid/cwd), so match them. */
    char canon[PATH_LEN];
    if (ctx->root[0] && realpath(ctx->root, canon)) snprintf(ctx->root, sizeof ctx->root, "%s", canon);
    for (size_t i = 0; i < ctx->nbindings; i++)
        if (realpath(ctx->bindings[i].host, canon))
            snprintf(ctx->bindings[i].host, sizeof ctx->bindings[i].host, "%s", canon);
    if (strcmp(ctx->root, "/") == 0) ctx->root[0] = '\0';
    for (size_t i = 0; i < ctx->nbindings; i++) {       /* where each binding is attached */
        binding_t *b = &ctx->bindings[i];
        snprintf(b->mount, sizeof b->mount, "%s%s", ctx->root, strcmp(b->guest, "/") == 0 ? "" : b->guest);
        b->mount_len = strlen(b->mount);
    }

    if (ctx->ids_file[0]) emuroot_owners_load(ctx, ctx->ids_file);
    long traced[512];
    size_t ntraced = traced_syscalls(traced);
    int want_seccomp = emuroot_seccomp_supported() && !getenv("EMUROOT_NO_SECCOMP");
    int pipefd[2];
    if (pipe(pipefd) != 0) return EMUROOT_ERR_SYSTEM;

    pid_t root_pid = fork();
    if (root_pid < 0) return EMUROOT_ERR_SYSTEM;

    if (root_pid == 0) {
        close(pipefd[0]);
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) _exit(127);
        /* Start inside the root so relative paths resolve there untranslated. */
        if (ctx->root[0] && chdir(ctx->root) < 0) {
            perror("emuroot: chdir to root");
            _exit(127);
        }
        char installed = want_seccomp && emuroot_seccomp_trace(traced, ntraced) == 0 ? 'Y' : 'N';
        if (write(pipefd[1], &installed, 1) != 1) _exit(127);
        close(pipefd[1]);
        raise(SIGSTOP);
        if (envp) environ = (char **)envp;      /* so execvp's PATH search sees it */
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    int status;
    waitpid(root_pid, &status, 0);              /* the initial SIGSTOP */
    char answer = 'N';
    if (read(pipefd[0], &answer, 1) != 1) answer = 'N';
    close(pipefd[0]);
    int seccomp_mode = answer == 'Y';
    if (ctx->verbose)
        fprintf(stderr, "[emuroot] tracing: %s\n", seccomp_mode ? "seccomp fast path" : "every syscall");

    long options = PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL | PTRACE_O_TRACEFORK |
                   PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE;
    if (seccomp_mode) options |= PTRACE_O_TRACESECCOMP;
    ptrace(PTRACE_SETOPTIONS, root_pid, 0, (void *)options);

    tracee_table_t tracees = { 0 };
    tracee_t *first = tracee_add(&tracees, ctx, root_pid, NULL);
    if (!first) return EMUROOT_ERR_SYSTEM;
    first->fresh = 0;
    ptrace(seccomp_mode ? PTRACE_CONT : PTRACE_SYSCALL, root_pid, NULL, NULL);

    int rc = EMUROOT_ERR_SYSTEM;
    for (;;) {
        pid_t w = waitpid(-1, &status, __WALL);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        tracee_t *t = tracee_find(&tracees, w);
        if (!t) t = tracee_add(&tracees, ctx, w, NULL); /* child seen before its parent's fork event */
        if (!t) continue;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (w == root_pid) {
                rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                break;
            }
            tracee_remove(&tracees, t);
            continue;
        }
        if (!WIFSTOPPED(status)) continue;

        long forward = handle_stop(&tracees, t, status, seccomp_mode);
        /* In seccomp mode the guest runs freely, except between a handled
         * syscall's entry and exit, where we need the exit stop. */
        ptrace(seccomp_mode && !t->awaiting_exit ? PTRACE_CONT : PTRACE_SYSCALL, w, NULL, (void *)forward);
    }

    while (tracees.n) tracee_remove(&tracees, tracees.v[0]);
    free(tracees.v);
    if (ctx->ids_file[0]) emuroot_owners_save(ctx, ctx->ids_file);
    return rc;
}
