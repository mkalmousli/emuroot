#!/usr/bin/env python3
"""build.py - generate emuroot's C source for a target and compile it.

The generator/ package is the single source of truth (see README.md); this
script renders it into build/<target>/{bin,lib,include,src}, using the
target compiler's own headers to resolve syscall numbers.

  ./build.py                     native build
  ./build.py --cc CC --ar AR     cross-compile with a given toolchain
  ./build.py --all               every target whose toolchain is available
  ./build.py --docker            like --all, glibc targets built in Docker
  ./build.py --ndk PATH | --musl only the Android NDK / musl targets
  ./build.py --clean             remove build/

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from generator import musl, ndk
from generator.banner import license_text
from generator.resolve import detect_arch, dump_macros, resolve_syscalls
from generator.spec import GNU_ARCHES, MUSL_TARGETS, NDK_TRIPLES, PROJECT
from generator.templates.docs import readme_src
from generator.templates.headers import arch_h, emuroot_h, internal_h, regs_io_h
from generator.templates.sources import CLI_MODULES, LIB_MODULES

BUILD_DIR = ROOT / "build"
CFLAGS = ["-Wall", "-Wextra", "-O2", "-std=c11", "-D_GNU_SOURCE"]
DEFAULT_API = 24


def run(cmd, **kw):
    print("+", " ".join(map(str, cmd)))
    subprocess.run(cmd, check=True, **kw)


def generate_sources(arch_key: str, syscalls: dict, src: Path, inc: Path) -> None:
    """Writes only source (.c/.h, LICENSE, README) so src/ stays shippable."""
    (src / "cli").mkdir(parents=True, exist_ok=True)
    inc.mkdir(parents=True, exist_ok=True)
    files = {
        inc / "emuroot.h": emuroot_h.render(PROJECT),
        src / "internal.h": internal_h.render(PROJECT),
        src / "regs_io.h": regs_io_h.render(PROJECT),
        src / "arch.h": arch_h.render_one(PROJECT, arch_key, syscalls),
        src / "LICENSE": license_text(PROJECT),
        src / "README.md": readme_src.render(PROJECT, arch_key),
    }
    for mod in LIB_MODULES:
        files[src / Path(mod.PATH).name] = mod.render(PROJECT)
    for mod in CLI_MODULES:
        files[src / "cli" / Path(mod.PATH).name] = mod.render(PROJECT)
    for path, text in files.items():
        path.write_text(text)


def compile_and_link(cc: str, ar: str, work: Path, static: bool, extra_flags: list[str]) -> None:
    """Objects go to work/obj, keeping work/src clean. Static glibc/musl
    binaries need nothing from the target libc (that's why they run on
    Android too); NDK builds link dynamically (-pie) since bionic ships no libc.a."""
    src, inc = work / "src", work / "include"
    flags = [*CFLAGS, *extra_flags, f"-I{inc}", f"-I{src}"]
    for sub in ("obj/cli", "lib", "bin"):
        (work / sub).mkdir(parents=True, exist_ok=True)

    def compile_all(modules, subdir):
        objs = []
        for mod in modules:
            c_file = src / subdir / Path(mod.PATH).name
            obj = work / "obj" / subdir / c_file.with_suffix(".o").name
            run([cc, *flags, "-c", c_file, "-o", obj])
            objs.append(obj)
        return objs

    lib_objs = compile_all(LIB_MODULES, "")
    cli_objs = compile_all(CLI_MODULES, "cli")
    lib = work / "lib" / "libemuroot.a"
    run([ar, "rcs", lib, *lib_objs])
    run([cc, *flags, "-static" if static else "-pie", *cli_objs,
         f"-L{lib.parent}", "-lemuroot", "-o", work / "bin" / "emuroot"])


def build_one(cc: str, ar: str, name: str | None = None, static: bool = True,
              extra_flags: list[str] = ()) -> str:
    macros = dump_macros(cc)
    arch_key = detect_arch(macros)
    if not arch_key:
        sys.exit(f"error: '{cc}' targets an architecture not in generator/spec/architectures.py")
    syscalls = resolve_syscalls(macros)
    if not syscalls:
        sys.exit(f"error: '{cc}' gave no syscall numbers from <asm/unistd.h> - not a Linux C compiler?")

    name = name or arch_key
    work = BUILD_DIR / name
    generate_sources(arch_key, syscalls, work / "src", work / "include")
    compile_and_link(cc, ar, work, static, list(extra_flags))
    print(f"-> build/{name}/bin/emuroot  ({len(syscalls)} syscalls, {arch_key}, "
          f"{'static' if static else 'PIE/dynamic'})")
    return name


def report(title: str, built: list[str], skipped: list[str], why: str) -> None:
    print(f"\n{title}:", ", ".join(built) or "(none)")
    if skipped:
        print(f"Skipped ({why}):", ", ".join(skipped))


def build_glibc_native() -> None:
    built, skipped = [], []
    for name, info in sorted(GNU_ARCHES.items()):
        if shutil.which(info["cc"]):
            build_one(info["cc"], info["ar"], name)
            built.append(name)
        else:
            skipped.append(name)
    report("Built", built, skipped, "compiler not on PATH")


def build_glibc_docker() -> None:
    """Builds the glibc targets inside a throwaway image holding every
    cross-toolchain; the Dockerfile is piped to `docker build`, never written."""
    if not shutil.which("docker"):
        sys.exit("error: --docker requires docker")
    apt = " ".join(sorted({pkg for info in GNU_ARCHES.values() for pkg in info["apt"]}))
    dockerfile = f"""\
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends python3 build-essential {apt} \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY generator generator
COPY build.py LICENSE ./
RUN python3 build.py --all --no-ndk --no-musl

FROM scratch AS export
COPY --from=builder /src/build /
"""
    BUILD_DIR.mkdir(exist_ok=True)
    run(["docker", "build", "--output", f"type=local,dest={BUILD_DIR}", "-f", "-", "."],
        cwd=ROOT, input=dockerfile, text=True)


def build_ndk(ndk_root: Path, api: int) -> None:
    host = {"linux": "linux-x86_64", "darwin": "darwin-x86_64", "windows": "windows-x86_64"}.get(
        platform.system().lower())
    if not host:
        sys.exit(f"error: unsupported host OS for the Android NDK: {platform.system()}")
    bin_dir = ndk_root / "toolchains" / "llvm" / "prebuilt" / host / "bin"
    if not (bin_dir / "llvm-ar").exists():
        sys.exit(f"error: no NDK toolchain under {bin_dir} - is --ndk a valid NDK root?")
    for name, triple in NDK_TRIPLES.items():
        cc = bin_dir / f"{triple}{api}-clang"
        if not cc.exists():
            sys.exit(f"error: {cc} not found - is API {api} supported by this NDK?")
        build_one(str(cc), str(bin_dir / "llvm-ar"), name, static=False, extra_flags=["-fPIE"])
    print(f"\nBuilt (Android NDK, API {api}):", ", ".join(NDK_TRIPLES))


def build_musl(best_effort: bool = False) -> None:
    built, skipped = [], []
    for arch, triple in MUSL_TARGETS.items():
        name = f"{arch}-musl"
        try:
            bin_dir = musl.find_or_fetch(arch)
        except Exception as e:
            if not best_effort:
                raise
            print(f"warning: skipping {name} - {e}")
            skipped.append(name)
            continue
        build_one(str(bin_dir / f"{triple}-gcc"), str(bin_dir / f"{triple}-ar"), name)
        built.append(name)
    report("Built (musl)", built, skipped, "toolchain unavailable")


def build_downloaded_targets(api: int, with_ndk: bool, with_musl: bool) -> None:
    """NDK and musl toolchains come from third-party hosts that can be
    flaky, so under --all/--docker a failed download skips instead of failing."""
    if with_ndk:
        try:
            build_ndk(ndk.find_or_fetch(), api)
        except Exception as e:
            print(f"warning: skipping Android NDK targets - {e}")
    if with_musl:
        build_musl(best_effort=True)


def main() -> None:
    p = argparse.ArgumentParser(description="Generate and build emuroot.")
    p.add_argument("--cc", default="cc", help="C compiler (default: cc)")
    p.add_argument("--ar", default="ar", help="archiver (default: ar)")
    p.add_argument("--arch-name", help="output directory name under build/")
    p.add_argument("--all", action="store_true",
                   help="every glibc target whose compiler is on PATH, plus NDK and musl")
    p.add_argument("--docker", action="store_true",
                   help="like --all, with the glibc targets built inside Docker")
    p.add_argument("--no-ndk", action="store_true", help="with --all/--docker: skip NDK targets")
    p.add_argument("--no-musl", action="store_true", help="with --all/--docker: skip musl targets")
    p.add_argument("--ndk", metavar="PATH", help="build only the NDK targets, with the NDK at PATH")
    p.add_argument("--ndk-api", type=int, default=DEFAULT_API, help=f"Android API level (default: {DEFAULT_API})")
    p.add_argument("--musl", action="store_true", help="build only the musl targets")
    p.add_argument("--clean", action="store_true", help="remove build/")
    args = p.parse_args()

    try:
        if args.clean:
            shutil.rmtree(BUILD_DIR, ignore_errors=True)
        elif args.ndk:
            build_ndk(Path(args.ndk), args.ndk_api)
        elif args.musl:
            build_musl()
        elif args.all or args.docker:
            (build_glibc_docker if args.docker else build_glibc_native)()
            build_downloaded_targets(args.ndk_api, not args.no_ndk, not args.no_musl)
        else:
            build_one(args.cc, args.ar, args.arch_name)
    except RuntimeError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
