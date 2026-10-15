/* The syscall tables (see syscalls.h). */
#include "syscalls.h"
#include <sys/syscall.h>

const path_syscall_t path_syscalls[] = {
#ifdef __NR_open
    { __NR_open, 1, 0, M_OPEN, 2 },
#endif
#ifdef __NR_creat
    { __NR_creat, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_stat
    { __NR_stat, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_stat64
    { __NR_stat64, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_lstat
    { __NR_lstat, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_lstat64
    { __NR_lstat64, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_access
    { __NR_access, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_readlink
    { __NR_readlink, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_chdir
    { __NR_chdir, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_rmdir
    { __NR_rmdir, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_unlink
    { __NR_unlink, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_mkdir
    { __NR_mkdir, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_chmod
    { __NR_chmod, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_chown
    { __NR_chown, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_chown32
    { __NR_chown32, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_lchown
    { __NR_lchown, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_lchown32
    { __NR_lchown32, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_truncate
    { __NR_truncate, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_truncate64
    { __NR_truncate64, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_utime
    { __NR_utime, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_utimes
    { __NR_utimes, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_mknod
    { __NR_mknod, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_statfs
    { __NR_statfs, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_statfs64
    { __NR_statfs64, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_getxattr
    { __NR_getxattr, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_lgetxattr
    { __NR_lgetxattr, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_setxattr
    { __NR_setxattr, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_lsetxattr
    { __NR_lsetxattr, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_listxattr
    { __NR_listxattr, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_llistxattr
    { __NR_llistxattr, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_removexattr
    { __NR_removexattr, 1, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_lremovexattr
    { __NR_lremovexattr, 1, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_rename
    { __NR_rename, 1, 2, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_link
    { __NR_link, 1, 2, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_symlink
    { __NR_symlink, 2, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_openat
    { __NR_openat, 2, 0, M_OPEN, 3 },
#endif
#ifdef __NR_openat2
    { __NR_openat2, 2, 0, M_OPENAT2, 3 },
#endif
#ifdef __NR_mkdirat
    { __NR_mkdirat, 2, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_unlinkat
    { __NR_unlinkat, 2, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_newfstatat
    { __NR_newfstatat, 2, 0, M_AT, 4 },
#endif
#ifdef __NR_fstatat64
    { __NR_fstatat64, 2, 0, M_AT, 4 },
#endif
#ifdef __NR_statx
    { __NR_statx, 2, 0, M_AT, 3 },
#endif
#ifdef __NR_readlinkat
    { __NR_readlinkat, 2, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_fchmodat
    { __NR_fchmodat, 2, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_fchmodat2
    { __NR_fchmodat2, 2, 0, M_AT, 4 },
#endif
#ifdef __NR_fchownat
    { __NR_fchownat, 2, 0, M_AT, 5 },
#endif
#ifdef __NR_utimensat
    { __NR_utimensat, 2, 0, M_AT, 4 },
#endif
#ifdef __NR_futimesat
    { __NR_futimesat, 2, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_faccessat
    { __NR_faccessat, 2, 0, M_FOLLOW, 0 },
#endif
#ifdef __NR_faccessat2
    { __NR_faccessat2, 2, 0, M_AT, 4 },
#endif
#ifdef __NR_mknodat
    { __NR_mknodat, 2, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_renameat
    { __NR_renameat, 2, 4, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_renameat2
    { __NR_renameat2, 2, 4, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_linkat
    { __NR_linkat, 2, 4, M_LINKAT, 5 },
#endif
#ifdef __NR_symlinkat
    { __NR_symlinkat, 3, 0, M_NOFOLLOW, 0 },
#endif
#ifdef __NR_inotify_add_watch
    { __NR_inotify_add_watch, 2, 0, M_FOLLOW, 0 },
#endif
};
const size_t n_path_syscalls = sizeof path_syscalls / sizeof path_syscalls[0];

const action_syscall_t action_syscalls[] = {
#ifdef __NR_getuid
    { __NR_getuid, A_GETUID },
#endif
#ifdef __NR_getuid32
    { __NR_getuid32, A_GETUID },
#endif
#ifdef __NR_geteuid
    { __NR_geteuid, A_GETEUID },
#endif
#ifdef __NR_geteuid32
    { __NR_geteuid32, A_GETEUID },
#endif
#ifdef __NR_getgid
    { __NR_getgid, A_GETGID },
#endif
#ifdef __NR_getgid32
    { __NR_getgid32, A_GETGID },
#endif
#ifdef __NR_getegid
    { __NR_getegid, A_GETEGID },
#endif
#ifdef __NR_getegid32
    { __NR_getegid32, A_GETEGID },
#endif
#ifdef __NR_getresuid
    { __NR_getresuid, A_GETRESUID },
#endif
#ifdef __NR_getresuid32
    { __NR_getresuid32, A_GETRESUID },
#endif
#ifdef __NR_getresgid
    { __NR_getresgid, A_GETRESGID },
#endif
#ifdef __NR_getresgid32
    { __NR_getresgid32, A_GETRESGID },
#endif
#ifdef __NR_getgroups
    { __NR_getgroups, A_GETGROUPS },
#endif
#ifdef __NR_getgroups32
    { __NR_getgroups32, A_GETGROUPS },
#endif
#ifdef __NR_setuid
    { __NR_setuid, A_SETUID },
#endif
#ifdef __NR_setuid32
    { __NR_setuid32, A_SETUID },
#endif
#ifdef __NR_setgid
    { __NR_setgid, A_SETGID },
#endif
#ifdef __NR_setgid32
    { __NR_setgid32, A_SETGID },
#endif
#ifdef __NR_setreuid
    { __NR_setreuid, A_SETREUID },
#endif
#ifdef __NR_setreuid32
    { __NR_setreuid32, A_SETREUID },
#endif
#ifdef __NR_setregid
    { __NR_setregid, A_SETREGID },
#endif
#ifdef __NR_setregid32
    { __NR_setregid32, A_SETREGID },
#endif
#ifdef __NR_setresuid
    { __NR_setresuid, A_SETRESUID },
#endif
#ifdef __NR_setresuid32
    { __NR_setresuid32, A_SETRESUID },
#endif
#ifdef __NR_setresgid
    { __NR_setresgid, A_SETRESGID },
#endif
#ifdef __NR_setresgid32
    { __NR_setresgid32, A_SETRESGID },
#endif
#ifdef __NR_setgroups
    { __NR_setgroups, A_SETGROUPS },
#endif
#ifdef __NR_setgroups32
    { __NR_setgroups32, A_SETGROUPS },
#endif
#ifdef __NR_setfsuid
    { __NR_setfsuid, A_SETFSUID },
#endif
#ifdef __NR_setfsuid32
    { __NR_setfsuid32, A_SETFSUID },
#endif
#ifdef __NR_setfsgid
    { __NR_setfsgid, A_SETFSGID },
#endif
#ifdef __NR_setfsgid32
    { __NR_setfsgid32, A_SETFSGID },
#endif
#ifdef __NR_chown
    { __NR_chown, A_CHOWN },
#endif
#ifdef __NR_chown32
    { __NR_chown32, A_CHOWN },
#endif
#ifdef __NR_lchown
    { __NR_lchown, A_CHOWN },
#endif
#ifdef __NR_lchown32
    { __NR_lchown32, A_CHOWN },
#endif
#ifdef __NR_fchown
    { __NR_fchown, A_CHOWN },
#endif
#ifdef __NR_fchown32
    { __NR_fchown32, A_CHOWN },
#endif
#ifdef __NR_fchownat
    { __NR_fchownat, A_CHOWN },
#endif
#if defined(__NR_stat) && defined(__LP64__)
    { __NR_stat, A_STAT_2 },
#endif
#if defined(__NR_lstat) && defined(__LP64__)
    { __NR_lstat, A_STAT_2 },
#endif
#if defined(__NR_fstat) && defined(__LP64__)
    { __NR_fstat, A_STAT_2 },
#endif
#if defined(__NR_newfstatat) && defined(__LP64__)
    { __NR_newfstatat, A_STAT_3 },
#endif
#ifdef __NR_stat64
    { __NR_stat64, A_STAT_2 },
#endif
#ifdef __NR_lstat64
    { __NR_lstat64, A_STAT_2 },
#endif
#ifdef __NR_fstat64
    { __NR_fstat64, A_STAT_2 },
#endif
#ifdef __NR_fstatat64
    { __NR_fstatat64, A_STAT_3 },
#endif
#ifdef __NR_statx
    { __NR_statx, A_STATX },
#endif
#ifdef __NR_getcwd
    { __NR_getcwd, A_GETCWD },
#endif
#ifdef __NR_uname
    { __NR_uname, A_UNAME },
#endif
};
const size_t n_action_syscalls = sizeof action_syscalls / sizeof action_syscalls[0];

enum action emuroot_action_of(long sysno)
{
    for (size_t i = 0; i < n_action_syscalls; i++)
        if (action_syscalls[i].sysno == sysno) return action_syscalls[i].action;
    return (enum action)-1;
}
