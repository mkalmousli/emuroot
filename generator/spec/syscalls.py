"""
syscalls.py - single source of truth for which syscalls emuroot knows
about, and which of their argument registers hold a filesystem path.

Two things are derived from the two tables below:
  - generator/resolve.py resolves SYSCALL_CANDIDATES against a target
    compiler's own kernel headers, in preference order (e.g. a 32-bit
    target's real `getuid()` libc call is the `getuid32` syscall, so
    that candidate is listed first).
  - generator/templates/sources/ptrace_engine_c.py renders the compiled
    binary's `path_syscalls[]` table straight from PATH_SYSCALLS,
    instead of hand-writing 40-some `{ SC_x, ... }` entries.

Add a syscall once, here, and both pick it up.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""

# emuroot macro name -> kernel syscall name candidates, most preferred
# first. The preference order is what lets one list handle both
# "legacy 16-bit-uid" 32-bit architectures (glibc/bionic there actually
# call the *32 variant) and clean 64-bit architectures (which only ever
# had the plain name) uniformly - see gen_syscalls.py's resolve().
SYSCALL_CANDIDATES: dict[str, list[str]] = {
    "SC_open":        ["open"],
    "SC_creat":       ["creat"],
    "SC_stat":        ["stat64", "stat"],
    "SC_lstat":       ["lstat64", "lstat"],
    "SC_access":      ["access"],
    "SC_readlink":    ["readlink"],
    "SC_chdir":       ["chdir"],
    "SC_rmdir":       ["rmdir"],
    "SC_unlink":      ["unlink"],
    "SC_mkdir":       ["mkdir"],
    "SC_chmod":       ["chmod"],
    "SC_chown":       ["chown"],
    "SC_lchown":      ["lchown"],
    "SC_truncate":    ["truncate64", "truncate"],
    "SC_utime":       ["utime"],
    "SC_utimes":      ["utimes"],
    "SC_mknod":       ["mknod"],
    "SC_statfs":      ["statfs64", "statfs"],
    "SC_rename":      ["rename"],
    "SC_link":        ["link"],
    "SC_symlink":     ["symlink"],
    "SC_openat":      ["openat"],
    "SC_mkdirat":     ["mkdirat"],
    "SC_unlinkat":    ["unlinkat"],
    "SC_newfstatat":  ["fstatat64", "newfstatat"],
    "SC_readlinkat":  ["readlinkat"],
    "SC_fchmodat":    ["fchmodat"],
    "SC_fchownat":    ["fchownat"],
    "SC_utimensat":   ["utimensat"],
    "SC_faccessat":   ["faccessat"],
    "SC_faccessat2":  ["faccessat2"],
    "SC_mknodat":     ["mknodat"],
    "SC_renameat":    ["renameat", "renameat2"],
    "SC_renameat2":   ["renameat2"],
    "SC_linkat":      ["linkat"],
    "SC_symlinkat":   ["symlinkat"],
    "SC_execveat":    ["execveat"],
    "SC_statx":       ["statx"],
    "SC_openat2":     ["openat2"],
    "SC_execve":      ["execve"],
    "SC_getuid":      ["getuid32", "getuid"],
    "SC_getgid":      ["getgid32", "getgid"],
    "SC_geteuid":     ["geteuid32", "geteuid"],
    "SC_getegid":     ["getegid32", "getegid"],
    "SC_setuid":      ["setuid32", "setuid"],
    "SC_setgid":      ["setgid32", "setgid"],
    "SC_getresuid":   ["getresuid32", "getresuid"],
    "SC_getresgid":   ["getresgid32", "getresgid"],
    "SC_setresuid":   ["setresuid32", "setresuid"],
    "SC_setresgid":   ["setresgid32", "setresgid"],
    "SC_setreuid":    ["setreuid32", "setreuid"],
    "SC_setregid":    ["setregid32", "setregid"],
    "SC_getgroups":   ["getgroups32", "getgroups"],
    "SC_setgroups":   ["setgroups32", "setgroups"],
    "SC_uname":       ["uname"],
}

# emuroot macro name -> (path_arg_slot_1, path_arg_slot_2), where a slot
# is 1-6 (matching EMUROOT_REG_ARGn) or 0 for "no path in this position".
# A non-zero second slot means both arguments are real filesystem paths
# that both need redirecting (rename/link and their *at forms); for the
# symlink family only the *link location* is a real path - the target
# string is stored verbatim by the kernel, never resolved, so it must
# not be redirected.
PATH_SYSCALLS: list[tuple[str, int, int]] = [
    ("SC_open", 1, 0),
    ("SC_creat", 1, 0),
    ("SC_stat", 1, 0),
    ("SC_lstat", 1, 0),
    ("SC_access", 1, 0),
    ("SC_readlink", 1, 0),
    ("SC_chdir", 1, 0),
    ("SC_rmdir", 1, 0),
    ("SC_unlink", 1, 0),
    ("SC_mkdir", 1, 0),
    ("SC_chmod", 1, 0),
    ("SC_chown", 1, 0),
    ("SC_lchown", 1, 0),
    ("SC_truncate", 1, 0),
    ("SC_utime", 1, 0),
    ("SC_utimes", 1, 0),
    ("SC_mknod", 1, 0),
    ("SC_statfs", 1, 0),
    ("SC_execve", 1, 0),
    ("SC_rename", 1, 2),
    ("SC_link", 1, 2),
    ("SC_symlink", 2, 0),       # symlink(target, linkpath) - target is unresolved
    ("SC_openat", 2, 0),
    ("SC_mkdirat", 2, 0),
    ("SC_unlinkat", 2, 0),
    ("SC_newfstatat", 2, 0),
    ("SC_readlinkat", 2, 0),
    ("SC_fchmodat", 2, 0),
    ("SC_fchownat", 2, 0),
    ("SC_utimensat", 2, 0),
    ("SC_faccessat", 2, 0),
    ("SC_faccessat2", 2, 0),
    ("SC_mknodat", 2, 0),
    ("SC_execveat", 2, 0),
    ("SC_statx", 2, 0),
    ("SC_openat2", 2, 0),
    ("SC_renameat", 2, 4),
    ("SC_renameat2", 2, 4),
    ("SC_linkat", 2, 4),
    ("SC_symlinkat", 3, 0),     # symlinkat(target, newdirfd, linkpath)
]

# Identity syscalls faked on syscall-exit, grouped by what they do -
# files/ptrace_engine_c.py renders one `if` block per group. Each entry
# is (macro_names, action); action is one of:
#   "set_ret_uid" / "set_ret_gid" - EMUROOT_REG_SET_RET to the fake id
#   "set_ret_zero"                - pretend the call always succeeds
#   "fill_uid3" / "fill_gid3"     - write the fake id into the three
#                                   ruid/euid/suid (or gid) out-pointers
#                                   of getresuid/getresgid
IDENTITY_ACTIONS: list[tuple[list[str], str]] = [
    (["SC_getuid", "SC_geteuid"], "set_ret_uid"),
    (["SC_getgid", "SC_getegid"], "set_ret_gid"),
    (["SC_setuid", "SC_setgid", "SC_setreuid", "SC_setregid",
      "SC_setresuid", "SC_setresgid"], "set_ret_zero"),
    (["SC_getresuid"], "fill_uid3"),
    (["SC_getresgid"], "fill_gid3"),
]
