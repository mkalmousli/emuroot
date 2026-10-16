# emuroot

**Run a whole Linux distribution as "root", without being root.** emuroot is an
unprivileged fake-root engine for Linux and Android, written from scratch: give it
a directory holding a root filesystem (Ubuntu, Debian, Alpine, Fedora, an Android
system image, ...) and it runs commands *inside* it, as uid 0, with no root,
containers, user namespaces or kernel modules.

```sh
# an Ubuntu root filesystem, from `docker export`, debootstrap, ...
emuroot -r ./ubuntu -b /proc:/proc -b /dev:/dev -b /etc/resolv.conf:/etc/resolv.conf -u 0:0 -- /bin/bash

root@host:/# id -u
0
root@host:/# apt-get update && apt-get install -y gcc     # a real apt, as an ordinary user
root@host:/# useradd -m alice && su alice -c 'touch ~/f; ls -ln ~/f'
```

That works because emuroot sits between the program and the kernel (with `ptrace`)
and quietly rewrites what the program sees. It needs only the right to trace your own
child processes.

## What it emulates

| | |
|---|---|
| **Root filesystem** | Every path is resolved inside the root, *including absolute symlinks and `..`*, so nothing escapes. `getcwd` and `/proc/self/{exe,cwd,fd/N}` show guest paths. |
| **Programs from any libc** | A binary runs on its own distribution's dynamic loader (glibc, musl, bionic) and `#!` interpreters, not the host's. |
| **Identity** | `get*id` / `set*id` / `*groups` behave like a real process with those credentials (so `su`, `useradd` and apt's `_apt` sandbox work); the real credentials never change. |
| **File ownership** | `chown` "succeeds" and is remembered, `stat` shows the fake owner, files made after a `su` belong to that user (like `fakeroot`; `-s FILE` persists it). |
| **Permissions** | Root-owned `0555` directories and `0000` files are usable, the way real root ignores mode bits. (emuroot does this by adding the *owner* bits, on disk, to files you own, only when the fake root needs them.) |
| **chroot / mount** | `chroot` re-roots the process; `mount --bind`, `-t proc`, `umount` become runtime bindings. |
| **Bindings** | `-b HOST:GUEST` overlays real paths (`/proc`, `/dev`, your project, ...) onto the root. |
| **Sockets** | `bind`/`connect` on unix-socket paths are redirected too. |
| `uname` | `-n SYS:REL:MACHINE` fakes it. |

Tested end to end on real **Ubuntu 22.04/24.04, Debian 12, Alpine 3.20 (also 32-bit), Fedora 40
and BusyBox**: `apt`, `dpkg`, `apk`, `dnf`, `gcc`, Go, Node, Python venvs/pip, threads,
sockets, process storms; and against the real userland of **Android 7 to 16**
(see [Testing](#testing)).

## How it works

```
 guest process ──syscall──▶ kernel
        ▲                      │ seccomp-bpf: stop only on the syscalls we care about
        │ patched result       ▼
        │                 ┌─────────── emuroot (the tracer) ───────────┐
        └──── fake.c ◀────┤ entry: rewrite.c  rewrites the arguments   │
                          │ exit:  fake.c     patches the result       │
                          └────────────────────────────────────────────┘
```

* **Entry** (`rewrite.c`): for a path-taking syscall (`open`, `stat`, `rename`, the whole
  `*at()` family, ... — the table is `syscalls.c`), read the path out of the guest, resolve
  it component by component in the *guest's* namespace (`path.c`), and point the argument
  at a rewritten copy. `execve` is expanded through the root's own loader (`exec.c`).
* **Exit** (`fake.c`): report the fake credentials, owners, working directory and `uname`.
* **Fast path** (`seccomp.c`): a seccomp filter makes the kernel stop the guest *only* on
  those syscalls, so `read`/`write`/`mmap`/`futex` run at native speed (about 5x faster
  than tracing everything). Linux before 4.8 transparently uses full tracing instead.
* Where `ptrace` is unavailable (some sandboxes, non-Linux Unixes) emuroot degrades,
  loudly, to an **env-only** mode: it `chdir`s into the root and exports `EMUROOT_ROOT`,
  `EMUROOT_BINDINGS`, `EMUROOT_FAKE_IDS` for cooperating programs.

## Source tour

The C is plain C11, about 3,000 lines, one idea per file:

| File | What it does |
|---|---|
| [`include/emuroot.h`](include/emuroot.h) | The public library API and version. |
| [`src/main.c`](src/main.c) | The command-line front end. |
| [`src/emuroot.c`](src/emuroot.c) | Context setup, backend selection, `emuroot_run`. |
| [`src/ptrace.c`](src/ptrace.c) | The engine: tracee table, the event loop, following fork/clone. |
| [`src/rewrite.c`](src/rewrite.c) | Syscall *entry*: translate and redirect path arguments and unix-socket addresses. |
| [`src/fake.c`](src/fake.c) | Syscall *exit*: credentials, `stat` owners, `getcwd`, `/proc` links, `uname`. |
| [`src/path.c`](src/path.c) | Guest path → host path: root, bindings, symlinks, `..`; and back. |
| [`src/exec.c`](src/exec.c) | `execve` through the root's ELF loader; `#!` scripts. |
| [`src/creds.c`](src/creds.c) | Per-process fake credentials with real `set*id` semantics. |
| [`src/ownership.c`](src/ownership.c) | The fake file-ownership table (`chown`, `-s` persistence). |
| [`src/identity.c`](src/identity.c) · [`dac.c`](src/dac.c) · [`mounts.c`](src/mounts.c) | Entry-side helpers: credential and ownership bookkeeping, permission bits, `chroot`/`mount`. |
| [`src/syscalls.c`](src/syscalls.c) | **The syscall tables**: which calls take paths and how, which are faked. Add a syscall here. |
| [`src/seccomp.c`](src/seccomp.c) | The seccomp-bpf fast path. |
| [`src/arch.h`](src/arch.h) | Per-CPU register layouts (x86, x86_64, arm, aarch64, riscv64, ppc64le, s390x, loongarch64). Add an architecture here. |
| [`src/guest.c`](src/guest.c) | Reading and writing the guest's memory (`process_vm_*`, falling back to `/proc/PID/mem`). |
| [`src/envonly.c`](src/envonly.c) | The portable fallback backend. |

## Usage

```
emuroot [-r ROOT] [-b HOST:GUEST]... [-u UID:GID] [-n SYS:REL:MACHINE] [-s FILE] [-v] -- COMMAND...
```

| Option | Effect |
|---|---|
| `-r DIR` | The emulated root. |
| `-b HOST:GUEST` | Overlay a real path onto the root (repeatable; longest match wins). |
| `-u UID:GID` | Be this user: credentials, and files look owned by it. Use `0:0` for root. |
| `-n SYS:REL:MACHINE` | Fake `uname()`. |
| `-s FILE` | Keep fake file ownership in `FILE` across runs. |
| `-v` | Trace every translated path and its result. |

Usual bindings for a distro: `-b /proc:/proc -b /dev:/dev -b /etc/resolv.conf:/etc/resolv.conf`.

As a library (`libemuroot.a`):

```c
#include <emuroot.h>
emuroot_ctx_t *ctx = emuroot_create();
emuroot_set_root(ctx, "/opt/fakeroot");
emuroot_bind(ctx, "/proc", "/proc");
emuroot_set_fake_ids(ctx, 0, 0);
char *argv[] = { "/bin/sh", NULL };
int status = emuroot_run(ctx, argv, NULL);
emuroot_destroy(ctx);
```

## Building

```sh
make                          # native build -> build/native/{bin,lib}
./build.py --all              # every target whose toolchain is available
                              #   (+ Android NDK and musl, downloaded if needed)
./build.py --docker           # same, glibc targets built in an ephemeral image
./build.py --ndk [PATH]       # only the Android NDK targets (bionic, PIE)
./build.py --musl             # only the musl targets
```

Targets: **glibc** (static) `x86_64 x86 aarch64 armv7 riscv64 ppc64le s390x loongarch64` ·
**musl** (static) the same minus loongarch64 · **Android NDK** (PIE) `android-arm64 android-armv7
android-x86_64 android-x86`. On other Unixes `make` builds the portable env-only backend
(`PTRACE=0`). Static builds need only the kernel ABI, so they run on Android too.

## Testing

Everything runs in **Docker** by default, as an unprivileged user (and again as root):

```sh
./test.py                  # about 30 s: basic cases, fallbacks, and 7 real root filesystems
./test.py --net            # + real package installs (apt/apk/dnf), Go, gcc
./test.py --android        # the real userland of Android 7 .. 16 (API 24-36)
./test.py --android 28 34  # chosen versions
./test.py --cross          # every architecture builds
./test.py --adb            # a connected Android device/emulator (real Android kernel)
./test.py -k chown         # only cases matching a name;  --list shows them all
```

| Suite | Run by | What |
|---|---|---|
| `tests/cases.py` | `./test.py` | Behaviour of each feature with host tools; run in seccomp mode, full-tracing mode, and with `/proc/PID/mem` memory access. |
| `tests/distros.py` | `./test.py` | The same expectations against Ubuntu, Debian, Alpine, Fedora, BusyBox and a 32-bit Alpine, plus `dpkg`, `chroot`, `su`, `useradd`, threads, sockets; with `--net`, `apt`/`apk`/`dnf` installs, `gcc`, Go. |
| `tests/android.py` | `--android`, `--adb` | The NDK-built (bionic) binary with each Android version's own `mksh` and `toybox`. |

`--android` downloads Google's SDK system images, unpacks them in a container
(`tools/android_image.py`, no mounting or root) and runs each version's real userland on the
host kernel. `--adb` covers what that cannot, a real Android kernel and SELinux; CI runs it on
emulators (`.github/workflows/android-test.yml`). Tests are shell scripts: add one to a
`CASES` dict.

## Limits worth knowing

* **Not a security boundary.** Escapes are closed on purpose (`..`, symlinks, `/proc` links),
  but a determined guest can still reach, e.g., `/proc/PID/root`. Use containers for isolation.
* **Permission checks are real.** The fake root cannot read files that *other real users* own,
  and device nodes can't be created (`mknod` fails).
* **A sandbox's seccomp profile wins.** Docker's default profile refuses `mount` before emuroot
  sees it; run with `EMUROOT_NO_SECCOMP=1` (full tracing) to have it emulated.
* **32-bit and 64-bit don't mix:** a 32-bit emuroot traces 32-bit guests only (and vice versa).
* **Mount API:** classic `mount(2)` is emulated; the newer `fsopen`/`fsmount` API (util-linux
  2.39+) is not, so that `mount` binary fails.
* **Android:** SELinux may deny `ptrace` to app processes; from `adb shell` it works. emuroot
  detects this and falls back to env-only mode.
* Cross-architecture emulation (an ARM guest on x86) is out of scope.

Environment switches for debugging: `EMUROOT_NO_SECCOMP=1` (trace every syscall),
`EMUROOT_GUEST_MEM=procmem|peek` (read the guest through `/proc/PID/mem` or `PTRACE_PEEK`), `EMUROOT_BACKEND=envonly`.

## Releases and license

Prebuilt `emuroot-<version>-<target>.tar.gz` archives with `SHA256SUMS` are on
[GitHub Releases](https://github.com/mkalmousli/emuroot/releases), cut by the **Release**
workflow. GPL-3.0-or-later, see [LICENSE](LICENSE).
