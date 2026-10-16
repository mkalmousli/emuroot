/* Tests the embedding API (emuroot_spawn / wait / terminate) the way an app
 * uses it: a host program that has children of its own, pipes for the guest's
 * stdio, signals in odd states. Run through test.py; each mode exits 0 on success.
 *
 *   embed_test ROOT MODE
 */
#include <emuroot.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static emuroot_ctx_t *make_ctx(const char *root)
{
    emuroot_ctx_t *ctx = emuroot_create();
    emuroot_set_root(ctx, root);
    for (const char *d[] = { "/usr", "/bin", "/lib", "/lib64", "/proc", "/dev", NULL }, **p = d; *p; p++)
        emuroot_bind(ctx, *p, *p);
    emuroot_set_fake_ids(ctx, 0, 0);
    return ctx;
}

static void read_all(int fd, char *buf, size_t size)
{
    size_t n = 0;
    ssize_t got;
    while (n + 1 < size && (got = read(fd, buf + n, size - 1 - n)) > 0) n += (size_t)got;
    buf[n] = '\0';
}

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s (errno %d)\n", __FILE__, __LINE__, #cond, errno); return 1; } } while (0)

/* A guest's stdout/stderr come back through pipes; its exit status through wait. */
static int mode_stdio(const char *root)
{
    emuroot_ctx_t *ctx = make_ctx(root);
    int out[2], err[2];
    CHECK(pipe(out) == 0 && pipe(err) == 0);
    emuroot_spawn_opts_t opts = { -1, out[1], err[1], 1 };
    char *argv[] = { "/bin/sh", "-c", "echo to-stdout; echo to-stderr >&2; id -u; exit 5", NULL };
    pid_t pid = emuroot_spawn(ctx, argv, NULL, &opts);
    CHECK(pid > 0);
    close(out[1]); close(err[1]);
    char o[256], e[256];
    read_all(out[0], o, sizeof o);
    read_all(err[0], e, sizeof e);
    CHECK(strcmp(o, "to-stdout\n0\n") == 0);
    CHECK(strcmp(e, "to-stderr\n") == 0);
    CHECK(emuroot_wait(pid) == 5);
    emuroot_destroy(ctx);
    return 0;
}

/* The host's own children must be left alone: the supervisor's waitpid(-1) is its own. */
static int mode_other_children(const char *root)
{
    pid_t mine = fork();                                /* a child of the host, unrelated to emuroot */
    if (mine == 0) { usleep(300000); _exit(42); }
    emuroot_ctx_t *ctx = make_ctx(root);
    char *argv[] = { "/bin/sh", "-c", "sleep 1; exit 3", NULL };
    pid_t pid = emuroot_spawn(ctx, argv, NULL, NULL);
    CHECK(pid > 0);
    int st;
    CHECK(waitpid(mine, &st, 0) == mine && WIFEXITED(st) && WEXITSTATUS(st) == 42);   /* still ours to reap */
    CHECK(emuroot_wait(pid) == 3);
    return 0;
}

static int mode_terminate(const char *root)
{
    emuroot_ctx_t *ctx = make_ctx(root);
    char *argv[] = { "/bin/sh", "-c", "sleep 77", NULL };
    pid_t pid = emuroot_spawn(ctx, argv, NULL, NULL);
    CHECK(pid > 0);
    usleep(700000);
    CHECK(emuroot_terminate(pid) == 0);
    CHECK(emuroot_wait(pid) == 128 + SIGTERM);
    return 0;
}

/* Hosts such as a JVM may ignore SIGCHLD or block signals; the guest must still work. */
static int mode_hostile_signals(const char *root)
{
    signal(SIGCHLD, SIG_DFL);
    sigset_t all;
    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, NULL);                 /* inherited by a forked child */
    emuroot_ctx_t *ctx = make_ctx(root);
    int out[2];
    CHECK(pipe(out) == 0);
    emuroot_spawn_opts_t opts = { -1, out[1], -1, 1 };
    char *argv[] = { "/bin/sh", "-c", "sleep 1 & wait; echo children-reaped", NULL };
    pid_t pid = emuroot_spawn(ctx, argv, NULL, &opts);
    CHECK(pid > 0);
    close(out[1]);
    char o[128];
    read_all(out[0], o, sizeof o);
    CHECK(strcmp(o, "children-reaped\n") == 0);
    CHECK(emuroot_wait(pid) == 0);
    return 0;
}

/* The guest must not inherit the host's descriptors. */
static int mode_fd_hygiene(const char *root)
{
    int secret = open("/etc/hostname", O_RDONLY);
    CHECK(secret >= 3);
    emuroot_ctx_t *ctx = make_ctx(root);
    int out[2];
    CHECK(pipe(out) == 0);
    emuroot_spawn_opts_t opts = { -1, out[1], -1, 1 };
    char cmd[128];
    snprintf(cmd, sizeof cmd, "if (: <&%d) 2>/dev/null; then echo leaked; else echo closed; fi", secret);
    char *argv[] = { "/bin/sh", "-c", cmd, NULL };
    pid_t pid = emuroot_spawn(ctx, argv, NULL, &opts);
    CHECK(pid > 0);
    close(out[1]);
    char o[64];
    read_all(out[0], o, sizeof o);
    CHECK(strcmp(o, "closed\n") == 0);
    CHECK(emuroot_wait(pid) == 0);
    return 0;
}

/* The host goes away (is killed): the supervisor and the guest must go too.
 * Prints the guest's marker so the test script can check nothing is left. */
static int mode_orphan(const char *root)
{
    emuroot_ctx_t *ctx = make_ctx(root);
    char *argv[] = { "/bin/sh", "-c", "exec sleep 78", NULL };
    CHECK(emuroot_spawn(ctx, argv, NULL, NULL) > 0);
    printf("started\n");
    fflush(stdout);
    pause();                                            /* wait to be killed */
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: embed_test ROOT MODE\n"); return 2; }
    static const struct { const char *name; int (*fn)(const char *); } modes[] = {
        { "stdio", mode_stdio }, { "other-children", mode_other_children },
        { "terminate", mode_terminate }, { "hostile-signals", mode_hostile_signals },
        { "fd-hygiene", mode_fd_hygiene }, { "orphan", mode_orphan },
    };
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++)
        if (strcmp(argv[2], modes[i].name) == 0) return modes[i].fn(argv[1]);
    fprintf(stderr, "unknown mode %s\n", argv[2]);
    return 2;
}
