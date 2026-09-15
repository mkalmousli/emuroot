"""
toolchains.py - single source of truth for which architectures emuroot
cross-builds for, and which toolchain builds each one.

Used by build.py:
  - GNU_ARCHES drives `--all` (build every architecture whose compiler is
    found on PATH) and `--docker` (installs every listed apt package in
    an ephemeral image, then runs `--all` inside it) - these link
    statically against glibc.
  - MUSL_TARGETS drives the musl builds (`--musl`, and automatically as
    part of `--all`/`--docker` unless `--no-musl`): each is a
    self-contained, pre-built cross-toolchain downloaded from musl.cc
    (see generator/musl.py) rather than an apt package, since musl
    cross-toolchains for most of these architectures aren't in Ubuntu's
    repos. musl fully supports static linking (unlike glibc, where
    static linking is a supported-but-discouraged edge case) so these
    builds are, if anything, on steadier ground than the glibc ones.
  - NDK_VERSION is the "correct" Android NDK release build.py looks for
    locally (see generator/ndk.py) before downloading it.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""

# The Android NDK release build.py auto-detects/downloads for the
# android-* targets. Bump this to move to a newer NDK; generator/ndk.py
# matches by major revision number (the leading digits), so a locally
# installed r27a/r27b/etc. still counts as "the same" NDK and won't
# trigger a redundant download.
NDK_VERSION = "r28b"

# dist-name -> (cc, ar, [apt packages needed to get that cc/ar + its glibc])
GNU_ARCHES: dict[str, dict[str, object]] = {
    "x86_64":      dict(cc="x86_64-linux-gnu-gcc",         ar="x86_64-linux-gnu-ar",
                         apt=["gcc"]),
    "x86":         dict(cc="i686-linux-gnu-gcc",           ar="i686-linux-gnu-ar",
                         apt=["gcc-i686-linux-gnu", "libc6-dev-i386-cross"]),
    "aarch64":     dict(cc="aarch64-linux-gnu-gcc",        ar="aarch64-linux-gnu-ar",
                         apt=["gcc-aarch64-linux-gnu", "libc6-dev-arm64-cross"]),
    "armv7":       dict(cc="arm-linux-gnueabihf-gcc",      ar="arm-linux-gnueabihf-ar",
                         apt=["gcc-arm-linux-gnueabihf", "libc6-dev-armhf-cross"]),
    "riscv64":     dict(cc="riscv64-linux-gnu-gcc",        ar="riscv64-linux-gnu-ar",
                         apt=["gcc-riscv64-linux-gnu", "libc6-dev-riscv64-cross"]),
    "ppc64le":     dict(cc="powerpc64le-linux-gnu-gcc",    ar="powerpc64le-linux-gnu-ar",
                         apt=["gcc-powerpc64le-linux-gnu", "libc6-dev-ppc64el-cross"]),
    "s390x":       dict(cc="s390x-linux-gnu-gcc",          ar="s390x-linux-gnu-ar",
                         apt=["gcc-s390x-linux-gnu", "libc6-dev-s390x-cross"]),
    "loongarch64": dict(cc="loongarch64-linux-gnu-gcc-13", ar="loongarch64-linux-gnu-ar",
                         apt=["gcc-13-loongarch64-linux-gnu", "libc6-dev-loong64-cross"]),
}

# dist-name -> musl.cc cross-toolchain triple (downloaded as
# https://musl.cc/<triple>-cross.tgz - a self-contained toolchain with
# its own gcc/ar/sysroot, no apt package needed). loongarch64 has no
# musl.cc build available, so it's glibc-only.
MUSL_TARGETS: dict[str, str] = {
    "x86_64":  "x86_64-linux-musl",
    "x86":     "i686-linux-musl",
    "aarch64": "aarch64-linux-musl",
    "armv7":   "arm-linux-musleabihf",
    "riscv64": "riscv64-linux-musl",
    "ppc64le": "powerpc64le-linux-musl",
    "s390x":   "s390x-linux-musl",
}
