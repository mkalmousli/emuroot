# emuroot

Unprivileged "fake root" for Linux and Android, written from scratch: run a
command with a redirected root filesystem and a fake identity, without root,
containers or kernel modules.

```sh
emuroot -r /opt/fakeroot -b /usr:/usr -b /bin:/bin -u 0:0 -- /bin/sh
```

| Option | Effect |
|---|---|
| `-r DIR` | Emulated root, like `chroot`: absolute paths the guest uses are rewritten under `DIR`. |
| `-b HOST:GUEST` | Overlay a real path onto the emulated root (repeatable; the longest guest prefix wins). |
| `-u UID:GID` | `get*id` report this identity; `set*id` calls appear to succeed. |
| `-n SYS:REL:MACHINE` | Fake `uname()`. |
| `-v` / `-V` / `-h` | Trace translated paths / version / help. |

## How it works

emuroot `ptrace`s the guest and every process it forks. On each path-taking
syscall (`open`, `stat`, `rename`, the whole `*at()` family, ... — see
`generator/spec/syscalls.py`) it reads the path from the guest's memory,
translates it against the root and bindings, and repoints the argument at a
rewritten copy before the kernel runs it. Identity syscalls get their results
patched on exit. The guest starts with its working directory inside the root,
so relative paths need no translation. It needs only the right to ptrace your
own children.

Where `ptrace` is unavailable, emuroot falls back to an **env-only** mode: it
`chdir`s into the root and exports `EMUROOT_ROOT`, `EMUROOT_BINDINGS` and
`EMUROOT_FAKE_IDS` for cooperating programs. It is labelled as degraded, not
silently emulated.

Not implemented yet: sockets, mounts, `*xattr`, `getgroups`/`setgroups`
faking, and `getcwd` still reports the host path.

## Building

This repo is a **generator**: `generator/` is the single source of truth and
`build.py` renders and compiles the C for one target. Nothing under `build/`
is hand-edited.

```sh
./build.py                    # native build
./build.py --cc CC --ar AR    # cross-compile with a given toolchain
./build.py --all              # every target whose toolchain is available
                              #   (+ Android NDK and musl, downloaded if needed)
./build.py --docker           # same, glibc targets built in an ephemeral image
./build.py --ndk PATH         # only Android NDK targets (--ndk-api N, default 24)
./build.py --musl             # only musl targets
./build.py --no-ndk --no-musl # skip those with --all/--docker
```

Output: `build/<target>/{bin/emuroot, lib/libemuroot.a, include/emuroot.h, src/}`.

To change something, edit exactly one place, then rebuild:

| Change | File |
|---|---|
| Author, license, version | `generator/spec/project.py` |
| Intercepted syscalls | `generator/spec/syscalls.py` |
| CPU register layouts | `generator/spec/architectures.py` |
| Cross-compilers, NDK version | `generator/spec/toolchains.py` |
| A generated C file | `generator/templates/{headers,sources,docs}/` |

Syscall numbers are never hand-copied: `generator/resolve.py` asks the target
compiler to preprocess `<asm/unistd.h>` and bakes the real numbers into `arch.h`.

### Targets

glibc (static): `x86_64` `x86` `aarch64` `armv7` `riscv64` `ppc64le` `s390x`
`loongarch64` · musl (static): the same minus `loongarch64`, suffixed `-musl` ·
Android NDK (PIE, bionic): `android-arm64` `android-armv7` `android-x86_64`
`android-x86`.

Static builds depend only on the kernel ABI, so they run on Android too.
`x86_64` is tested in CI; `armv7`, `armv7-musl`, `android-arm64` and
`android-armv7` were verified by hand on a real Android 10 phone; the rest
compile but are not yet run-tested (their register tables in
`architectures.py` are the first suspect if one misbehaves).

## Testing

Real-world tests live in `tests/cases.py`: short shell scripts that run real
tools (`cp -r`, `sed -i`, `find`, `mv`, `ln -s`, `id`, `uname`, ...) inside a
fake root and check the result. The same suite runs on the host or on any
Android device or emulator:

```sh
./test.py                 # on this machine (builds first if needed)
./test.py --adb           # on the connected Android device/emulator
./test.py --adb -s SERIAL # a specific one
./test.py -k rename       # only matching cases;  --list shows all
```

`--adb` picks `build/android-<abi>/bin/emuroot` from the device's ABI, pushes
it, and prints the Android version/API level it ran on. CI
(`.github/workflows/android-test.yml`) runs it across an emulator matrix of
API levels 24–34; add a level to the matrix to cover another version. To add a
test, add a script to `CASES` (helpers are documented at the top of the file).

## Library

```c
#include <emuroot.h>
emuroot_ctx_t *ctx = emuroot_create();
emuroot_set_root(ctx, "/opt/fakeroot");
emuroot_bind(ctx, "/usr", "/usr");
emuroot_set_fake_ids(ctx, 0, 0);
char *argv[] = { "/bin/sh", NULL };
int status = emuroot_run(ctx, argv, NULL);
emuroot_destroy(ctx);
```

Link with `-lemuroot -I build/<target>/include -L build/<target>/lib`.

## Releases and CI

Prebuilt `emuroot-<version>-<target>.tar.gz` archives with `SHA256SUMS` are on
[GitHub Releases](https://github.com/mkalmousli/emuroot/releases), cut manually
by the **Release** workflow. `build.yml` builds every target and runs the host
tests on each push; `android-test.yml` runs the Android matrix.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). Every generated file carries the
notice, derived from `generator/spec/project.py`.
