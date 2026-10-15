/* Which syscalls emuroot handles, and how. The tables are in syscalls.c; their
 * numbers come from <sys/syscall.h>, and a syscall the target kernel ABI lacks
 * is compiled out by its #ifdef. */
#ifndef EMUROOT_SYSCALLS_H
#define EMUROOT_SYSCALLS_H

#include <stddef.h>

/* How a path argument treats a symlink in its last component. */
enum follow_mode {
    M_FOLLOW,       /* always follow */
    M_NOFOLLOW,     /* never follow (lstat, unlink, rename, ...) */
    M_OPEN,         /* open flags in `flag_slot`: O_NOFOLLOW, or O_CREAT|O_EXCL */
    M_OPENAT2,      /* `flag_slot` points to a struct open_how */
    M_AT,           /* AT_SYMLINK_NOFOLLOW in `flag_slot` */
    M_LINKAT,       /* AT_SYMLINK_FOLLOW in `flag_slot`; default is not to follow */
};

/* Which argument slots (1-6) hold paths the kernel resolves. rename/link-style
 * calls have two (the second is never followed); symlink-style calls list only
 * the link location, never the target string (stored verbatim, not resolved). */
typedef struct { long sysno; int slot1, slot2; enum follow_mode mode; int flag_slot; } path_syscall_t;

extern const path_syscall_t path_syscalls[];
extern const size_t n_path_syscalls;



/* Syscalls whose behaviour is emulated or whose results are patched on exit. */
enum action {
    A_GETUID, A_GETEUID, A_GETGID, A_GETEGID,       /* the fake credentials ... */
    A_GETRESUID, A_GETRESGID, A_GETGROUPS,
    A_SETUID, A_SETGID, A_SETREUID, A_SETREGID,     /* ... and how they change */
    A_SETRESUID, A_SETRESGID, A_SETGROUPS,
    A_SETFSUID, A_SETFSGID,
    A_CHOWN,                /* chown family: EPERM becomes success */
    A_STAT_2, A_STAT_3,     /* stat family, struct stat in argument 2 / 3 */
    A_STATX,                /* statx, struct statx in argument 5 */
    A_GETCWD,               /* strip the root from the returned directory */
    A_UNAME,                /* fake uname() */
};

typedef struct { long sysno; enum action action; } action_syscall_t;

extern const action_syscall_t action_syscalls[];
extern const size_t n_action_syscalls;

/* The action for a syscall number, or -1. */
enum action emuroot_action_of(long sysno);

#endif
