/* emuroot - user-space fake-root / filesystem-identity emulation. GPL-3.0-or-later, see LICENSE. */
#ifndef EMUROOT_H
#define EMUROOT_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMUROOT_VERSION_MAJOR 0
#define EMUROOT_VERSION_MINOR 1
#define EMUROOT_VERSION_PATCH 0

typedef struct emuroot_ctx emuroot_ctx_t;

typedef enum {
    EMUROOT_OK = 0,
    EMUROOT_ERR_NOMEM = -1,
    EMUROOT_ERR_INVAL = -2,
    EMUROOT_ERR_UNSUPPORTED = -3,   /* platform lacks required primitives */
    EMUROOT_ERR_SYSTEM = -4,        /* see errno */
    EMUROOT_ERR_NOT_FOUND = -5,
} emuroot_status_t;

/* Backend actually selected at runtime for this platform. */
typedef enum {
    EMUROOT_BACKEND_NONE = 0,
    EMUROOT_BACKEND_PTRACE,   /* full syscall interception (Linux) */
    EMUROOT_BACKEND_ENVONLY,  /* best-effort: env + chdir, no interception */
} emuroot_backend_t;

/* Create / destroy a session context. */
emuroot_ctx_t *emuroot_create(void);
void emuroot_destroy(emuroot_ctx_t *ctx);

/* Report which backend this platform/build will actually use. */
emuroot_backend_t emuroot_probe_backend(void);
const char *emuroot_backend_name(emuroot_backend_t backend);

/*
 * Set the emulated filesystem root. Any absolute path the guest process
 * resolves that is not covered by a more specific binding is rewritten
 * to be relative to new_root, exactly like chroot(2) but without needing
 * privileges.
 */
int emuroot_set_root(emuroot_ctx_t *ctx, const char *new_root);

/*
 * Bind a host path onto a guest (emulated) path, e.g.
 *   emuroot_bind(ctx, "/dev", "/dev")
 *   emuroot_bind(ctx, "/home/user/project", "/root/project")
 * Later bindings take precedence over earlier, more general ones.
 */
int emuroot_bind(emuroot_ctx_t *ctx, const char *host_path, const char *guest_path);

/* Fake the uid/gid reported to the guest process (does not grant privilege). */
int emuroot_set_fake_ids(emuroot_ctx_t *ctx, unsigned int uid, unsigned int gid);

/* Fake uname() machine/sysname/release strings reported to the guest. */
int emuroot_set_fake_uname(emuroot_ctx_t *ctx, const char *sysname,
                            const char *release, const char *machine);

/*
 * Remember fake file ownership (chown, files created under a switched
 * identity) in `path`: loaded when the guest starts, saved when it exits, so a
 * later run sees the same owners. Without this the table lives for one run.
 * Keyed by device and inode, so it holds while the files stay where they are.
 */
int emuroot_set_ids_file(emuroot_ctx_t *ctx, const char *path);

/* Enable/disable verbose syscall tracing to stderr (debugging). */
int emuroot_set_verbose(emuroot_ctx_t *ctx, int enable);

/*
 * Run argv[0] inside the emulated environment, replacing bindings/root as
 * configured. envp may be NULL to inherit the current environment.
 * Blocks until the guest exits; returns its exit status (>=0) or a
 * negative emuroot_status_t on failure to even start it.
 */
int emuroot_run(emuroot_ctx_t *ctx, char *const argv[], char *const envp[]);

/*
 * Embedding in a larger program (an Android app, a server): run the guest under
 * a separate *supervisor process* instead of in the calling one.
 *
 * emuroot_run() traces from the calling process with waitpid(-1), which would
 * also swallow the exit status of every other child the host program has, so a
 * program with children of its own (a JVM, a server) must use emuroot_spawn().
 * It forks a supervisor that runs the guest and exits with the guest's status.
 */
typedef struct {
    int stdin_fd, stdout_fd, stderr_fd;  /* become the guest's 0, 1, 2; -1 = inherit */
    int die_with_parent;                 /* 1 (default when `opts` is NULL): the guest is killed
                                            if the calling *thread* exits - so spawn from a thread
                                            that lives as long as the guest should */
} emuroot_spawn_opts_t;

/* Returns the supervisor's pid, or -1 (errno set). The caller owns the fds it
 * passed in and should close its copies of the child's ends afterwards. */
pid_t emuroot_spawn(emuroot_ctx_t *ctx, char *const argv[], char *const envp[],
                    const emuroot_spawn_opts_t *opts);
/* Waits for a supervisor; returns the guest's exit status (128+N if killed by
 * signal N), or -1 (errno set). */
int emuroot_wait(pid_t supervisor);
/* Asks the supervisor to stop: the guest and everything it started are killed. */
int emuroot_terminate(pid_t supervisor);

/*
 * Write a human-readable report of what this kernel/sandbox lets emuroot use
 * (ptrace, register access, guest-memory access, seccomp fast path, ...) into
 * buf. Returns 0 if full emulation is available, 1 if emuroot would degrade.
 * Handy to show in an app's "about" screen or a bug report.
 */
int emuroot_diagnose(char *buf, size_t len);

const char *emuroot_strerror(int status);
void emuroot_version_string(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMUROOT_H */
