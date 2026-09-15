#!/usr/bin/env python3
"""
build.py - generates emuroot's C source for a target architecture and
compiles it, in one step.

This repository's generator/ package is the single source of truth for
emuroot's identity (author/license/version), which syscalls it
intercepts and how, and each architecture's register layout. There is
no intermediate "generated project" checked in anywhere and no
build-time code generator shipped as a separate tool file: build.py
*is* the generator, and it compiles what it generates immediately.

For the target compiler (the host's `cc` by default, or one you name),
build.py asks that compiler for its own real predefined macros and its
real <asm/unistd.h> syscall numbers (generator/resolve.py), uses those
to pick the matching register-layout entry in
generator/spec/architectures.py and to bake the resolved syscall numbers
straight into a generated arch.h, renders the rest of the C sources from
generator/templates/, and compiles + links a static library and binary.

Usage:
  ./build.py                      build for the current machine
  ./build.py --cc CC --ar AR      cross-compile with a specific toolchain
  ./build.py --arch-name NAME     override the build/<name> output directory
  ./build.py --all                cross-build every glibc architecture in
                                   generator/spec/toolchains.py:GNU_ARCHES
                                   whose compiler is found on PATH (best-effort),
                                   *and* the Android NDK and musl targets
                                   (see below)
  ./build.py --docker             like --all, but the glibc architectures build
                                   inside an ephemeral Docker image with every
                                   cross-toolchain installed (no Dockerfile is
                                   ever written to disk - its content is piped
                                   to `docker build -f -`); the NDK and musl
                                   targets still build directly on the host
  ./build.py --no-ndk             with --all/--docker, skip the Android NDK targets
  ./build.py --no-musl            with --all/--docker, skip the musl targets
  ./build.py --ndk PATH           build *only* the Android NDK targets, with the
                                   NDK at PATH instead of auto-detecting/downloading
  ./build.py --ndk-api N          Android API level to target (default: 24)
  ./build.py --musl               build *only* the musl targets (see
                                   generator/spec/toolchains.py:MUSL_TARGETS),
                                   auto-detecting/downloading each toolchain
  ./build.py --clean              remove build/

Building the Android NDK targets needs an Android NDK. build.py finds one
on its own (generator/ndk.py): it looks for a local install matching
generator/spec/toolchains.py:NDK_VERSION (checking $ANDROID_NDK_HOME,
$ANDROID_SDK_ROOT/ndk/*, ~/Android/Sdk/ndk/*, and the GitHub Actions
Ubuntu image's default path, in that order), and downloads it straight
from Google into ~/.cache/emuroot/ndk/ if nothing local matches.

Building the musl targets needs a musl cross-toolchain per architecture.
build.py finds one on its own too (generator/musl.py): a toolchain already
on PATH or previously cached wins, otherwise it downloads the matching
self-contained toolchain from musl.cc into ~/.cache/emuroot/musl/.

Output lands in build/<arch>/{bin/emuroot,lib/libemuroot.a,include/emuroot.h},
with musl targets suffixed "-musl" (e.g. build/x86_64-musl/) alongside the
glibc build/x86_64/.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from generator.spec import PROJECT, GNU_ARCHES, MUSL_TARGETS
from generator.resolve import dump_macros, resolve_syscalls, detect_arch
from generator.banner import license_text
from generator import ndk as ndk_toolchain
from generator import musl as musl_toolchain
from generator.templates.headers import emuroot_h, internal_h, regs_io_h, arch_h
from generator.templates.sources import LIB_MODULES as LIB_C_MODULES, CLI_MODULES as CLI_C_MODULES
from generator.templates.docs import readme_src

BUILD_DIR = ROOT / "build"
CFLAGS = ["-Wall", "-Wextra", "-O2", "-std=c11", "-D_GNU_SOURCE"]


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def generate_sources(arch_key: str, syscalls: dict, src_dir: Path, inc_dir: Path) -> None:
    """Writes only source: .c/.h, LICENSE, README.md - never build objects,
    so src_dir stays a clean, inspectable/shippable source tree."""
    src_dir.mkdir(parents=True, exist_ok=True)
    (src_dir / "cli").mkdir(exist_ok=True)
    inc_dir.mkdir(parents=True, exist_ok=True)

    (inc_dir / "emuroot.h").write_text(emuroot_h.render(PROJECT))
    (src_dir / "internal.h").write_text(internal_h.render(PROJECT))
    (src_dir / "regs_io.h").write_text(regs_io_h.render(PROJECT))
    (src_dir / "arch.h").write_text(arch_h.render_one(PROJECT, arch_key, syscalls))
    for mod in LIB_C_MODULES:
        (src_dir / Path(mod.PATH).name).write_text(mod.render(PROJECT))
    for mod in CLI_C_MODULES:
        (src_dir / "cli" / Path(mod.PATH).name).write_text(mod.render(PROJECT))

    (src_dir / "LICENSE").write_text(license_text(PROJECT))
    (src_dir / "README.md").write_text(readme_src.render(PROJECT, arch_key))


def compile_and_link(cc: str, ar: str, src_dir: Path, inc_dir: Path, work: Path,
                      static: bool = True, extra_flags: list[str] = ()) -> Path:
    """Compiles src_dir's sources into work/obj (kept separate from
    src_dir so the source tree never accumulates build objects), then
    links the library and binary into work/lib and work/bin.

    static=True (the default, used for every glibc cross-toolchain
    target) produces a fully self-contained binary with no runtime
    dependency on the target's libc at all - this is what let emuroot's
    glibc builds run correctly on Android over its Linux kernel, without
    needing an Android-specific build (see README). static=False is used
    for Android NDK builds: bionic has not shipped a static libc.a since
    NDK r23, so those link dynamically against the device's libc.so
    instead, with extra_flags carrying the -fPIE/-pie Android requires."""
    flags = [*CFLAGS, *extra_flags, f"-I{inc_dir}", f"-I{src_dir}"]
    obj_dir = work / "obj"
    (obj_dir / "cli").mkdir(parents=True, exist_ok=True)

    lib_objs, cli_objs = [], []
    for mod in LIB_C_MODULES:
        src = src_dir / Path(mod.PATH).name
        obj = obj_dir / src.with_suffix(".o").name
        run([cc, *flags, "-c", str(src), "-o", str(obj)])
        lib_objs.append(obj)
    for mod in CLI_C_MODULES:
        src = src_dir / "cli" / Path(mod.PATH).name
        obj = obj_dir / "cli" / src.with_suffix(".o").name
        run([cc, *flags, "-c", str(src), "-o", str(obj)])
        cli_objs.append(obj)

    lib_dir = work / "lib"
    bin_dir = work / "bin"
    lib_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)

    static_lib = lib_dir / "libemuroot.a"
    run([ar, "rcs", str(static_lib), *[str(o) for o in lib_objs]])

    binary = bin_dir / "emuroot"
    link_flag = "-static" if static else "-pie"
    run([cc, *flags, link_flag, *[str(o) for o in cli_objs],
         f"-L{lib_dir}", "-lemuroot", "-o", str(binary)])
    return binary


def build_one(cc: str, ar: str, arch_name: str | None = None,
              static: bool = True, extra_flags: list[str] = ()) -> str:
    macros = dump_macros(cc)
    arch_key = detect_arch(macros)
    if not arch_key:
        sys.exit(f"error: '{cc}' targets an architecture emuroot doesn't recognize "
                  f"(no matching entry in generator/spec/architectures.py:ARCHES)")
    syscalls = resolve_syscalls(macros)
    if not syscalls:
        sys.exit(f"error: '{cc}' produced no usable syscall numbers from <asm/unistd.h> "
                  f"- is this a working Linux C cross-compiler?")

    out_name = arch_name or arch_key
    work = BUILD_DIR / out_name
    src_dir, inc_dir = work / "src", work / "include"

    generate_sources(arch_key, syscalls, src_dir, inc_dir)
    compile_and_link(cc, ar, src_dir, inc_dir, work, static=static, extra_flags=extra_flags)
    kind = "static" if static else "PIE/dynamic"
    print(f"-> build/{out_name}/bin/emuroot  ({len(syscalls)} syscalls resolved for {arch_key}, {kind})")
    return out_name


def build_all_native(with_ndk: bool = True, ndk_api: int = 24, with_musl: bool = True) -> None:
    """The NDK/musl targets build best-effort here: both depend on a
    third-party download (Google's NDK host, musl.cc) that can be slow,
    rate-limited, or briefly unreachable from a given network (this has
    happened in CI - see generator/fetch.py), and a transient outage on
    either one shouldn't take down the whole build. Explicit `--ndk`/
    `--musl` invocations stay strict, since there the caller asked for
    exactly that and needs to know if it didn't happen."""
    built, skipped = [], []
    for name, info in sorted(GNU_ARCHES.items()):
        if shutil.which(info["cc"]) is None:
            skipped.append(name)
            continue
        build_one(info["cc"], info["ar"], arch_name=name)
        built.append(name)
    print("\nBuilt:", ", ".join(built) or "(none)")
    if skipped:
        print("Skipped (compiler not on PATH):", ", ".join(skipped))
    if with_ndk:
        build_all_ndk_auto(ndk_api, best_effort=True)
    if with_musl:
        build_all_musl(best_effort=True)


def build_all_via_docker(with_ndk: bool = True, ndk_api: int = 24, with_musl: bool = True) -> None:
    """The glibc architectures build inside Docker (each cross-toolchain
    is only ever installed in the throwaway image). The Android NDK and
    musl targets build directly on the host afterwards instead: both are
    big, self-contained toolchains with no need for container isolation,
    and building them on the host lets their local caches
    (~/.cache/emuroot/{ndk,musl}/) actually persist between runs."""
    if shutil.which("docker") is None:
        sys.exit("error: --docker requires docker to be installed")

    apt_pkgs = sorted({pkg for info in GNU_ARCHES.values() for pkg in info["apt"]})
    dockerfile = f"""\
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \\
        python3 build-essential {" ".join(apt_pkgs)} \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY generator generator
COPY build.py build.py
COPY LICENSE LICENSE
RUN python3 build.py --all --no-ndk --no-musl

FROM scratch AS export
COPY --from=builder /src/build /
"""
    BUILD_DIR.mkdir(exist_ok=True)
    run(["docker", "build", "--output", f"type=local,dest={BUILD_DIR}", "-f", "-", "."],
        cwd=ROOT, input=dockerfile, text=True)
    print("\nBuilt architectures:")
    for arch in sorted(GNU_ARCHES):
        binary = BUILD_DIR / arch / "bin" / "emuroot"
        print(f"  {'OK' if binary.exists() else 'MISSING':7} {arch}")

    if with_ndk:
        build_all_ndk_auto(ndk_api, best_effort=True)
    if with_musl:
        build_all_musl(best_effort=True)


# Every ABI the Android NDK ships a toolchain for. Its unified clang
# names each target's compiler <triple><api-level>-clang under
# toolchains/llvm/prebuilt/<host-tag>/bin/, e.g. aarch64-linux-android24-clang
# - one clang binary per (arch, API level), all sharing the same llvm-ar.
NDK_TRIPLES = {
    "android-arm64": "aarch64-linux-android",
    "android-armv7": "armv7a-linux-androideabi",
    "android-x86_64": "x86_64-linux-android",
    "android-x86": "i686-linux-android",
}


def ndk_host_tag() -> str:
    import platform as host_platform
    tag = {"linux": "linux-x86_64", "darwin": "darwin-x86_64",
           "windows": "windows-x86_64"}.get(host_platform.system().lower())
    if not tag:
        sys.exit(f"error: unsupported host OS for Android NDK builds: {host_platform.system()}")
    return tag


def build_all_ndk(ndk_root: str, api: int) -> None:
    """Builds emuroot for every arch in NDK_TRIPLES using the Android NDK
    at ndk_root, dynamically linked (PIE) against bionic - a second,
    independent validation path alongside the statically-linked glibc
    builds, for maximum confidence emuroot runs correctly on real Android
    devices regardless of which libc it ends up linked against."""
    bin_dir = Path(ndk_root) / "toolchains" / "llvm" / "prebuilt" / ndk_host_tag() / "bin"
    ar = bin_dir / "llvm-ar"
    if not ar.exists():
        sys.exit(f"error: llvm-ar not found under {bin_dir} - is --ndk pointing at a valid NDK root?")

    built = []
    for arch_name, triple in NDK_TRIPLES.items():
        cc = bin_dir / f"{triple}{api}-clang"
        if not cc.exists():
            sys.exit(f"error: {cc} not found - is API level {api} supported by this NDK?")
        build_one(str(cc), str(ar), arch_name=arch_name, static=False, extra_flags=["-fPIE"])
        built.append(arch_name)
    print("\nBuilt (Android NDK, API", f"{api}):", ", ".join(built))


def build_all_ndk_auto(api: int = 24, best_effort: bool = False) -> None:
    """Finds (or downloads) the pinned Android NDK and builds the NDK
    targets with it - what --all/--docker call automatically unless
    --no-ndk is given."""
    if best_effort:
        try:
            ndk_root = ndk_toolchain.find_or_fetch()
        except Exception as e:
            print(f"warning: skipping Android NDK targets - {e}")
            return
    else:
        ndk_root = ndk_toolchain.find_or_fetch()
    build_all_ndk(str(ndk_root), api)


def build_all_musl(best_effort: bool = False) -> None:
    """Builds emuroot for every arch in MUSL_TARGETS, each with its own
    musl.cc cross-toolchain (found locally or downloaded on demand - see
    generator/musl.py). musl fully supports static linking, so these
    build exactly like the glibc targets (static=True, the default) -
    just against a different libc, output to build/<arch>-musl/.

    best_effort=True (what --all/--docker use) skips an architecture
    whose toolchain can't be found/downloaded instead of aborting the
    whole build - musl.cc is a single third-party host with no uptime
    guarantee, and today's ~7-way redundancy elsewhere (glibc + NDK)
    means one flaky download shouldn't block everything else."""
    built, skipped = [], []
    for arch_name, triple in MUSL_TARGETS.items():
        out_name = f"{arch_name}-musl"
        if best_effort:
            try:
                bin_dir = musl_toolchain.find_or_fetch(arch_name)
            except Exception as e:
                print(f"warning: skipping {out_name} - {e}")
                skipped.append(out_name)
                continue
        else:
            bin_dir = musl_toolchain.find_or_fetch(arch_name)
        cc, ar = bin_dir / f"{triple}-gcc", bin_dir / f"{triple}-ar"
        build_one(str(cc), str(ar), arch_name=out_name)
        built.append(out_name)
    print("\nBuilt (musl):", ", ".join(built) or "(none)")
    if skipped:
        print("Skipped (toolchain unavailable):", ", ".join(skipped))


def main():
    p = argparse.ArgumentParser(description="Generate and build emuroot.")
    p.add_argument("--cc", default="cc", help="C compiler to use (default: cc)")
    p.add_argument("--ar", default="ar", help="archiver to use (default: ar)")
    p.add_argument("--arch-name", default=None,
                    help="override the build/<arch-name> output directory")
    p.add_argument("--all", action="store_true",
                    help="build every glibc architecture in GNU_ARCHES whose compiler is "
                         "on PATH, plus the Android NDK and musl targets "
                         "(see --no-ndk/--no-musl)")
    p.add_argument("--docker", action="store_true",
                    help="like --all, but the glibc architectures build inside an "
                         "ephemeral Docker image with every cross-toolchain installed "
                         "(no Dockerfile written to disk); NDK/musl targets still build "
                         "on the host (see --no-ndk/--no-musl)")
    p.add_argument("--no-ndk", action="store_true",
                    help="with --all/--docker, skip the Android NDK targets")
    p.add_argument("--no-musl", action="store_true",
                    help="with --all/--docker, skip the musl targets")
    p.add_argument("--ndk", metavar="PATH", default=None,
                    help="build *only* the Android NDK targets, with the NDK at this "
                         "path instead of auto-detecting/downloading one")
    p.add_argument("--ndk-api", type=int, default=24,
                    help="Android API level to target for NDK builds (default: 24)")
    p.add_argument("--musl", action="store_true",
                    help="build *only* the musl targets (see "
                         "generator/spec/toolchains.py:MUSL_TARGETS), auto-detecting/"
                         "downloading each toolchain")
    p.add_argument("--clean", action="store_true", help="remove build/")
    args = p.parse_args()

    if args.clean:
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        print("cleaned build/")
        return

    try:
        if args.ndk:
            build_all_ndk(args.ndk, args.ndk_api)
        elif args.musl:
            build_all_musl()
        elif args.docker:
            build_all_via_docker(with_ndk=not args.no_ndk, ndk_api=args.ndk_api, with_musl=not args.no_musl)
        elif args.all:
            build_all_native(with_ndk=not args.no_ndk, ndk_api=args.ndk_api, with_musl=not args.no_musl)
        else:
            build_one(args.cc, args.ar, args.arch_name)
    except RuntimeError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
