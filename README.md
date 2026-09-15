# emuroot

An independent, from-scratch user-space filesystem/identity emulation
engine — a "fake root" tool in the spirit of chroot/fakeroot-style tools,
but written entirely from zero with its own design and code.

**Author:** Mohamad Almousli
**Repository:** https://github.com/mkalmousli/emuroot
**License:** GPLv3 (see `LICENSE`) — no third-party source is included
or derived from; this is an original implementation.
**Version:** 0.1.0

## What it does

`emuroot` runs a command inside an emulated root filesystem and identity,
without requiring root privileges, containers, or kernel modules:

- **Root redirection** (`-r`): like `chroot`, but unprivileged — absolute
  paths the guest resolves are rewritten under a directory you choose.
- **Path bindings** (`-b host:guest`): mount-like overlays, so parts of
  the real filesystem (e.g. `/usr`, `/dev`) remain visible inside the
  emulated root while the rest is redirected.
- **Fake identity** (`-u uid:gid`): the guest sees whatever uid/gid you
  configure from `getuid`/`getgid`/`geteuid`/`getegid`/`getresuid`/
  `getresgid`, and `setuid`/`setgid`/`setreuid`/`setregid`/`setresuid`/
  `setresgid` calls appear to succeed, without any real privilege change.
- **Fake `uname()`** (`-n sysname:release:machine`): report an emulated
  OS/kernel/arch identity to the guest.

## This repository is a generator, not a static C project

Everything you'd normally find at the top of a C project — `src/`,
`include/`, the build output — is not checked in here. Instead this
repo holds `generator/`, a small Python package that is the **single
source of truth** for emuroot's entire identity and behavior, and
`build.py`, which generates the complete C source for one target
architecture and compiles it, in one step:

```sh
./build.py              # generate + build for the current machine
```

Nothing under `build/` (gitignored) is hand-edited — it's pure,
reproducible output. If you want to change emuroot's author, license,
version, which syscalls it intercepts, which CPU architectures it
targets, or the boilerplate comment atop every generated file, there is
exactly **one place** to do it:

| Change this...                        | ...in this single file          |
|-----------------------------------------|-----------------------------------|
| Author, license, version, repo URL      | `generator/spec/project.py`      |
| Which syscalls are intercepted & how    | `generator/spec/syscalls.py`     |
| Which CPU architectures exist           | `generator/spec/architectures.py` (register layout) + `generator/spec/toolchains.py` (cross-compiler) |
| A specific generated file's C/prose     | `generator/templates/{headers,sources,docs}/*.py` |

Change one of those and run `./build.py` — every file that mentions the
author, the version number, a syscall number, or an architecture is
re-derived and stays consistent. See `generator/__init__.py` for a map
of the whole package.

## How it works

On Linux, `emuroot` uses `ptrace(2)` to trace every syscall the guest
makes. On syscall entry it looks the syscall number up in a table
(`path_syscalls[]` in the generated `src/ptrace_engine.c`, itself
generated from `generator/spec/syscalls.py:PATH_SYSCALLS`) describing
which argument register(s), if any, hold a filesystem path for that
syscall — this table covers every path-taking syscall the target
exposes: the classic single-path calls (`open`, `stat`, `chdir`,
`chmod`, `chown`, `truncate`, `mknod`, `utime(s)`, ...), the dual-path
calls (`rename`, `link`, and their `*at` forms, redirecting both paths),
the symlink-style calls (redirecting only the link location, never the
unresolved target string), and the whole `*at()` dirfd-relative family
including the newest ones (`openat2`, `statx`, `faccessat2`,
`renameat2`). Whatever path is found is read out of the tracee's memory
via `process_vm_readv`, translated against your configured
root/bindings, and — if it needs redirecting — written into a scratch
buffer in the tracee's own address space with the syscall's argument
register repointed at it, before the kernel executes the (now-rewritten)
syscall. On syscall exit, the identity syscalls have their result
patched to the configured fake identity, also table-driven
(`generator/spec/syscalls.py:IDENTITY_ACTIONS`).

Register access always goes through `PTRACE_GETREGSET`/`SETREGSET`
(`NT_PRSTATUS`) with a per-architecture struct that is deliberately just
a *prefix* of the kernel's real register set — the kernel bounds every
copy to that struct's size, so this is safe on every architecture
regardless of how large its full register set actually is. The register
layout for each CPU is a small Python table in
`generator/spec/architectures.py`, rendered into a generated `arch.h`
that has already been resolved for one specific target — no
`#ifdef`/`#elif` chain in the output.

This requires no special privileges beyond being able to `ptrace` your
own child, which is available to unprivileged users on essentially all
Linux systems and inside most containers.

### Syscall numbers are resolved from real kernel headers, not hand-copied

Numeric syscall numbers differ per architecture, and for a handful of
syscalls also depend on whether the target uses the legacy 16-bit-uid
ABI (e.g. 32-bit x86/ARM actually call `getresuid32`, not `getresuid` —
hand-transcribing the wrong one is an easy, silent mistake this
project's own history has an example of). Instead, `build.py` asks the
*actual compiler about to build emuroot* to preprocess `<asm/unistd.h>`
and reads the real numbers straight out of its output
(`generator/resolve.py`), picking whichever candidate (`getuid32` vs
`getuid`, `stat64` vs `stat`, ...) that target's real headers define,
and bakes them directly into the generated `arch.h` — nothing is cached
or hand-maintained between builds.

### Supported targets

| target          | CPU / libc                              | notes |
|------------------|-------------------------------------------|-------|
| `x86_64`         | 64-bit x86, glibc, statically linked       | verified on real hardware |
| `x86`            | 32-bit x86 (i686), glibc, static           | compiles; not execution-tested |
| `aarch64`        | 64-bit ARM, glibc, static                  | compiles; not execution-tested |
| `armv7`          | 32-bit ARM/EABI, glibc, static              | **verified on a real Android phone** |
| `riscv64`        | 64-bit RISC-V, glibc, static                | compiles; not execution-tested |
| `ppc64le`        | 64-bit PowerPC LE, glibc, static             | compiles; not execution-tested |
| `s390x`          | IBM Z / s390x, glibc, static                  | compiles; not execution-tested |
| `loongarch64`    | LoongArch64, glibc, static                     | compiles; not execution-tested |
| `android-arm64`  | 64-bit ARM, Android NDK, dynamic/PIE, bionic   | **verified on a real Android phone** |
| `android-armv7`  | 32-bit ARM, Android NDK, dynamic/PIE, bionic   | **verified on a real Android phone** |
| `android-x86_64` | 64-bit x86, Android NDK, dynamic/PIE, bionic   | compiles; not execution-tested |
| `android-x86`    | 32-bit x86, Android NDK, dynamic/PIE, bionic   | compiles; not execution-tested |

Architectures without hardware/emulators available to this project have
only been verified to produce a correctly-tagged ELF that compiles
cleanly; their *register-offset* tables in
`generator/spec/architectures.py` (hand-written, since they don't come
from kernel headers the way syscall numbers do) have not been exercised
at runtime. If you run one and find a bug, that file's `ARCHES` table is
the only thing that should need a fix — syscall numbers regenerate
correctly by construction.

There is no attempt at cross-architecture emulation (running, say, an
ARM guest binary while tracing from an x86_64 host) — that is a distinct
instruction-emulation problem, not filesystem/identity emulation, and is
out of scope for this engine.

### Two independent ways emuroot reaches Android

The `armv7`/`aarch64`/... glibc targets link **statically**, which means
they carry their own copy of glibc and depend on nothing but the Linux
kernel syscall ABI — this is *why* a plain Linux cross-build already
runs correctly on Android (Android's kernel is Linux; ptrace is a kernel
feature, not a libc one). The `android-arm64`/`android-armv7` targets,
built with the real Android NDK, instead link dynamically against the
device's actual `libc.so` (bionic) as a normal PIE executable via
`/system/bin/linker`. Having both gives two independent, real-hardware-
verified confirmations that emuroot works correctly on Android: one that
never touches bionic at all, and one that's a completely standard
Android executable.

### Platforms without ptrace

Not every target (some hardened sandboxes, some non-Linux Unixes this
code is ported to) exposes `ptrace`/`process_vm_*`. `emuroot` probes for
this at runtime (`emuroot_probe_backend()`) and falls back to an
**env-only** backend: it `chdir`s into the configured root and exports
`EMUROOT_ROOT` / `EMUROOT_BINDINGS` / `EMUROOT_FAKE_IDS` so a cooperating
guest can honor them voluntarily. This is a deliberate, honestly-labeled
degradation — full syscall virtualization is fundamentally a ptrace-class
feature, and there is no way to fake it silently and correctly on a
platform that lacks the primitive.

## Building

```sh
./build.py                       # build for the current machine
./build.py --cc CC --ar AR       # cross-compile with a specific toolchain
./build.py --all                 # every glibc architecture whose compiler is on PATH,
                                  # + the Android NDK targets (auto-detected/downloaded)
./build.py --docker              # every glibc architecture, via an ephemeral Docker
                                  # image with all cross-toolchains installed (no
                                  # Dockerfile is ever written to disk), + Android NDK
./build.py --no-ndk              # combine with --all/--docker to skip the NDK targets
./build.py --ndk PATH            # build *only* android-arm64 + android-armv7, with the
                                  # Android NDK at PATH instead of auto-detect/download
./build.py --clean               # remove build/
```

`--all`/`--docker` build the Android NDK targets automatically: `build.py`
looks for a local NDK matching the pinned version
(`generator/spec/toolchains.py:NDK_VERSION`) in the usual places
($ANDROID_NDK_HOME, $ANDROID_SDK_ROOT/ndk/\*, ~/Android/Sdk/ndk/\*, ...),
and downloads it straight from Google into `~/.cache/emuroot/ndk/` if
nothing local matches (see `generator/ndk.py`).

Output lands in `build/<target>/{bin/emuroot,lib/libemuroot.a,include/emuroot.h,src/}`.
`src/` is a clean, inspectable source tree — no build objects — with its
own `LICENSE` and `README.md`.

## Using the library

```c
#include <emuroot.h>

emuroot_ctx_t *ctx = emuroot_create();
emuroot_set_root(ctx, "/opt/my-fake-root");
emuroot_bind(ctx, "/usr", "/usr");
emuroot_set_fake_ids(ctx, 0, 0);

char *argv[] = { "/bin/sh", NULL };
int status = emuroot_run(ctx, argv, NULL);

emuroot_destroy(ctx);
```

Link with `-lemuroot` (from `build/<target>/lib/libemuroot.a`) and
`-I build/<target>/include`.

## Using the CLI

```sh
emuroot -r /opt/my-fake-root -b /usr:/usr -b /bin:/bin -u 0:0 -- /bin/sh
```

## Releases

Prebuilt binaries, static libraries, headers, and generated source for
every target above are published from
[GitHub Releases](https://github.com/mkalmousli/emuroot/releases), each
as a `emuroot-<version>-<target>.tar.gz` with an accompanying
`SHA256SUMS`. Releases are cut manually via the **Release** GitHub
Actions workflow (`.github/workflows/release.yml`); every push also runs
the **Build** workflow (`.github/workflows/build.yml`) as CI.

## Status / roadmap

This is an early, functional core, not a drop-in chroot replacement yet.
Implemented: the path-translation engine; a table-driven ptrace
syscall-interception loop covering every path-taking syscall exposed by
each target's kernel headers (single-path, dual-path, `*at()`, and the
newest variants like `openat2`/`statx`/`renameat2`/`faccessat2`) across
all targets above; fake `getuid`/`geteuid`/`getgid`/`getegid`/
`getresuid`/`getresgid`, faked `setuid`/`setgid`/`setreuid`/`setregid`/
`setresuid`/`setresgid` (always "succeed"), fake `uname()`; the env-only
fallback for platforms without ptrace; and the generator/build system
described above.

Verified on real hardware, on a Moto G7 Play (Android 10 / API 29): both
the statically-linked `armv7` glibc build and the dynamically-linked,
bionic-native `android-armv7` NDK build correctly redirected
`/etc/hostname` into a fake root (including through `mv`/`rename` and
`ln -s`/`readlink`, both dual- and single-path syscalls) while passing
through `/system`, `/vendor`, `/apex`, `/dev`, `/proc`, and faked `id`'s
reported uid/gid to `0(root)` via the `getresuid32` path real Android
`id` actually uses.

Not yet implemented: sockets, mount-related calls, `*xattr`, `ioctl`
path variants, `getgroups`/`setgroups` faking, and a permanent
(mmap-backed) scratch page for argument rewriting instead of the current
below-stack scratch heuristic.

## Generator project layout

```
build.py                          generates C source for one target and compiles it
generator/
  __init__.py                     package overview
  banner.py                       license text + C file-header comment renderer
  resolve.py                      compiler introspection (syscall numbers, arch detection)
  ndk.py                          finds/downloads the Android NDK for --all/--docker
  spec/                           plain data - nothing here runs a subprocess or renders C
    project.py                    author, license, version, repo URL, summary
    architectures.py              per-CPU register layout + arch auto-detection
    syscalls.py                   which syscalls, which args are paths, identity faking
    toolchains.py                 cross-compiler + apt packages per glibc architecture, pinned NDK_VERSION
  templates/                      one render() per generated file, grouped by kind
    headers/                      emuroot_h, internal_h, regs_io_h, arch_h
    sources/                      bindings_c, path_c, platform_c, emuroot_api_c,
                                   ptrace_engine_c, envonly_engine_c, main_c
    docs/                         readme_src (the per-build src/README.md)
.github/workflows/
  build.yml                       CI: builds on every push/PR
  release.yml                     manually-triggered: builds + publishes a GitHub Release
```
