/* Running the guest under a supervisor process (see emuroot.h). */
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

/* Closes everything but stdin/out/err so the guest doesn't inherit the host
 * program's descriptors (sockets, files, a JVM's internals). */
static void close_inherited_fds(void)
{
#ifdef __NR_close_range
    if (syscall(__NR_close_range, 3u, ~0u, 0u) == 0) return;       /* Linux 5.9+ */
#endif
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536) max = 65536;
    for (int fd = 3; fd < max; fd++) close(fd);
}

/* The host program may have blocked or ignored signals (a JVM does); the
 * supervisor needs the defaults, in particular SIGCHLD to see its children die. */
static void reset_signals(void)
{
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    for (int sig = 1; sig < 32; sig++)
        if (sig != SIGKILL && sig != SIGSTOP) signal(sig, SIG_DFL);
}

pid_t emuroot_spawn(emuroot_ctx_t *ctx, char *const argv[], char *const envp[],
                    const emuroot_spawn_opts_t *opts)
{
    if (!ctx || !argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    int in = opts ? opts->stdin_fd : -1, out = opts ? opts->stdout_fd : -1, err = opts ? opts->stderr_fd : -1;
    int die_with_parent = opts ? opts->die_with_parent : 1;

    pid_t pid = fork();
    if (pid != 0) return pid;

    /* --- the supervisor --- */
#ifdef __linux__
    if (die_with_parent) prctl(PR_SET_PDEATHSIG, SIGKILL);
#else
    (void)die_with_parent;
#endif
    setsid();                                   /* its own session: not hit by the host's job control */
    reset_signals();
    if (in >= 0 && dup2(in, 0) < 0) _exit(127);
    if (out >= 0 && dup2(out, 1) < 0) _exit(127);
    if (err >= 0 && dup2(err, 2) < 0) _exit(127);
    close_inherited_fds();
    int rc = emuroot_run(ctx, argv, envp);
    _exit(rc < 0 ? 127 : rc & 0xff);
}

int emuroot_wait(pid_t supervisor)
{
    int status;
    while (waitpid(supervisor, &status, 0) < 0)
        if (errno != EINTR) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}

int emuroot_terminate(pid_t supervisor)
{
    return kill(supervisor, SIGTERM);           /* the engine kills its guests when told to stop */
}
