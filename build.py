#!/usr/bin/env python3
"""build.py - build emuroot for one or many targets (drives the Makefile).

  ./build.py                     native build         -> build/native
  ./build.py --cc CC --ar AR     one cross-toolchain  (--name N sets build/N)
  ./build.py --all               every target whose toolchain is available:
                                 glibc (compilers on PATH), Android NDK, musl
  ./build.py --docker            like --all, glibc targets built in Docker
  ./build.py --ndk [PATH]        only the Android NDK targets
  ./build.py --musl              only the musl targets
  ./build.py --clean
"""
import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
import toolchains  # noqa: E402

BUILD = ROOT / "build"
ANDROID_API = 24

# target -> (compiler, archiver, apt packages providing them)
GLIBC = {
    "x86_64":      ("x86_64-linux-gnu-gcc",         "x86_64-linux-gnu-ar",      ["gcc"]),
    "x86":         ("i686-linux-gnu-gcc",           "i686-linux-gnu-ar",        ["gcc-i686-linux-gnu", "libc6-dev-i386-cross"]),
    "aarch64":     ("aarch64-linux-gnu-gcc",        "aarch64-linux-gnu-ar",     ["gcc-aarch64-linux-gnu", "libc6-dev-arm64-cross"]),
    "armv7":       ("arm-linux-gnueabihf-gcc",      "arm-linux-gnueabihf-ar",   ["gcc-arm-linux-gnueabihf", "libc6-dev-armhf-cross"]),
    "riscv64":     ("riscv64-linux-gnu-gcc",        "riscv64-linux-gnu-ar",     ["gcc-riscv64-linux-gnu", "libc6-dev-riscv64-cross"]),
    "ppc64le":     ("powerpc64le-linux-gnu-gcc",    "powerpc64le-linux-gnu-ar", ["gcc-powerpc64le-linux-gnu", "libc6-dev-ppc64el-cross"]),
    "s390x":       ("s390x-linux-gnu-gcc",          "s390x-linux-gnu-ar",       ["gcc-s390x-linux-gnu", "libc6-dev-s390x-cross"]),
    "loongarch64": ("loongarch64-linux-gnu-gcc-13", "loongarch64-linux-gnu-ar", ["gcc-13-loongarch64-linux-gnu", "libc6-dev-loong64-cross"]),
}
# target -> musl.cc triple (no loongarch64 toolchain is published). armv5 is soft-float
# EABI (Android's "armeabi"): the one 32-bit ARM binary that runs on every ARM Android.
MUSL = {
    "x86_64": "x86_64-linux-musl", "x86": "i686-linux-musl", "aarch64": "aarch64-linux-musl",
    "armv7": "arm-linux-musleabihf", "armv5": "arm-linux-musleabi", "riscv64": "riscv64-linux-musl",
    "ppc64le": "powerpc64le-linux-musl", "s390x": "s390x-linux-musl",
}
# target -> NDK clang triple
NDK = {
    "android-arm64": "aarch64-linux-android", "android-armv7": "armv7a-linux-androideabi",
    "android-x86_64": "x86_64-linux-android", "android-x86": "i686-linux-android",
}


def make(name: str, cc, ar, static: bool = True, extra_cflags: str = "") -> None:
    cmd = ["make", "-C", str(ROOT), f"OUT=build/{name}", f"CC={cc}", f"AR={ar}",
           f"STATIC={int(static)}", f"EXTRA_CFLAGS={extra_cflags}"]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"-> build/{name}/bin/emuroot")


def build_glibc() -> None:
    for name, (cc, ar, _) in sorted(GLIBC.items()):
        if shutil.which(cc):
            make(name, cc, ar)
        else:
            print(f"skipping {name}: {cc} not on PATH")


def build_glibc_docker() -> None:
    """Build the glibc targets in a throwaway image holding every cross-toolchain."""
    if not shutil.which("docker"):
        sys.exit("error: --docker requires docker")
    apt = " ".join(sorted({p for *_, pkgs in GLIBC.values() for p in pkgs}))
    dockerfile = f"""\
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends python3 build-essential {apt} \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN python3 build.py --glibc-only

FROM scratch
COPY --from=builder /src/build /
"""
    BUILD.mkdir(exist_ok=True)
    subprocess.run(["docker", "build", "--output", f"type=local,dest={BUILD}", "-f", "-", "."],
                   cwd=ROOT, input=dockerfile, text=True, check=True)


def build_ndk(ndk_root: Path) -> None:
    host = {"linux": "linux-x86_64", "darwin": "darwin-x86_64", "windows": "windows-x86_64"}[
        platform.system().lower()]
    bin_dir = ndk_root / "toolchains" / "llvm" / "prebuilt" / host / "bin"
    for name, triple in NDK.items():
        # bionic ships no libc.a, so these are dynamic PIE executables
        make(name, bin_dir / f"{triple}{ANDROID_API}-clang", bin_dir / "llvm-ar",
             static=False, extra_cflags="-fPIE")


def build_musl(keep_going: bool = False) -> None:
    """keep_going: skip a target whose toolchain can't be fetched, instead of
    failing (musl.cc is a single third-party host)."""
    for name, triple in MUSL.items():
        try:
            bin_dir = toolchains.find_musl(triple)
        except RuntimeError as e:
            if not keep_going:
                raise
            print(f"warning: skipping {name}-musl - {e}")
            continue
        try:
            make(f"{name}-musl", bin_dir / f"{triple}-gcc", bin_dir / f"{triple}-ar")
        except subprocess.CalledProcessError:
            if name in ("x86", "x86_64", "armv5", "armv7", "aarch64"):    # the ones Android uses
                raise
            print(f"warning: {name}-musl failed to build; skipping it")


def best_effort(label: str, fn) -> None:
    """NDK/musl toolchains come from third-party hosts; under --all a failed
    download shouldn't sink the whole build."""
    try:
        fn()
    except (RuntimeError, subprocess.CalledProcessError, OSError) as e:
        print(f"warning: skipping {label} targets - {e}")


def main() -> None:
    p = argparse.ArgumentParser(description="Build emuroot.")
    p.add_argument("--cc", default="cc")
    p.add_argument("--ar", default="ar")
    p.add_argument("--name", default="native", help="build/<name> for a single build")
    p.add_argument("--all", action="store_true", help="glibc + Android NDK + musl")
    p.add_argument("--docker", action="store_true", help="--all with glibc built in Docker")
    p.add_argument("--glibc-only", action="store_true", help=argparse.SUPPRESS)
    p.add_argument("--no-ndk", action="store_true")
    p.add_argument("--no-musl", action="store_true")
    p.add_argument("--ndk", nargs="?", const="", metavar="PATH",
                   help="only the NDK targets; PATH defaults to a found/downloaded NDK")
    p.add_argument("--musl", action="store_true", help="only the musl targets")
    p.add_argument("--clean", action="store_true")
    a = p.parse_args()

    if a.clean:
        shutil.rmtree(BUILD, ignore_errors=True)
    elif a.ndk is not None:
        build_ndk(Path(a.ndk) if a.ndk else toolchains.find_ndk())
    elif a.musl:
        build_musl()
    elif a.glibc_only:
        build_glibc()
    elif a.all or a.docker:
        (build_glibc_docker if a.docker else build_glibc)()
        if not a.no_ndk:
            best_effort("Android NDK", lambda: build_ndk(toolchains.find_ndk()))
        if not a.no_musl:
            build_musl(keep_going=True)
    else:
        make(a.name, a.cc, a.ar)


if __name__ == "__main__":
    main()
