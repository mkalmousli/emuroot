#!/usr/bin/env python3
"""test.py - emuroot's test runner. Everything runs inside Docker by default.

  ./test.py                  build the Docker test image and run the full suite:
                             basic cases, the env-only fallback, and real Ubuntu /
                             Debian / Alpine / Fedora / BusyBox (and 32-bit Alpine)
                             root filesystems, as an unprivileged user; then the
                             basic cases once more as root
  ./test.py --quick          only the basic cases and the env-only fallback
  ./test.py --net            also run the network cases (apt/apk/dnf installs)
  ./test.py --android [API]  run against the real userland of Android versions
                             (default: a spread from Android 7 to 16; or name API
                             levels, e.g. --android 28 34). Needs the NDK build.
  ./test.py --cross          check that every architecture builds (in Docker)
  ./test.py --adb [-s SER]   run on a connected Android device/emulator via adb
  ./test.py -k NAME          only cases whose name contains NAME (all modes)
  ./test.py --list           list the cases

  ./test.py --local ...      run in this environment instead of Docker (this is
                             what runs inside the container; needs a built binary)

Docker needs `--cap-add SYS_PTRACE` (added for you): its default seccomp profile
only allows process_vm_readv/writev, which emuroot uses, with that capability.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

from tests.android import ALL_COMMANDS, CASES as ANDROID_CASES, applicable
from tests.cases import CASES, ENVONLY_CASES
from tests.distros import CASES as DISTRO_CASES, DISTROS, NET_CASES
from tests.harness import (AdbTarget, LocalTarget, android_preamble, basic_preamble,
                           distro_preamble, probe_commands, run_cases)

BUILD = ROOT / "build"
IMAGE = "emuroot-test"
EXTRACT_IMAGE = "emuroot-android-extract"
DOCKERFILE = "tests/docker/Dockerfile"
# Android 7.0 .. 16: the userlands differ most around 8 (Treble), 10 (bionic in
# an APEX), 11/12 (new linker namespaces) and 14+.
DEFAULT_ANDROID_APIS = [10, 15, 16, 19, 24, 26, 28, 29, 30, 31, 33, 34, 36]
ANDROID_BINARY = "android-x86_64"       # the NDK build for the emulator-style x86_64 images
LEGACY_API = 21                          # below this: 32-bit x86 images, and the static x86 build
LEGACY_BINARY = "x86-musl"              # (static, so it needs nothing from the old Android)


def android_binary(api: int) -> str:
    return LEGACY_BINARY if api < LEGACY_API else ANDROID_BINARY


# --- running in Docker -------------------------------------------------------

def docker(*args, quiet: bool = False, **kw) -> int:
    if not quiet:
        print("+ docker", " ".join(args), flush=True)
    return subprocess.run(["docker", *args], cwd=ROOT, **kw).returncode


def require_docker() -> None:
    if subprocess.run(["docker", "info"], capture_output=True).returncode != 0:
        sys.exit("error: Docker isn't available (is the daemon running?). "
                 "Use --local to run without it.")


def build_image(tag: str, dockerfile: str) -> None:
    print(f"building {tag} (cached after the first time)...", flush=True)
    build = subprocess.run(["docker", "build", "--progress=plain", "-t", tag, "-f", dockerfile, "."],
                           cwd=ROOT, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout[-6000:], build.stderr[-6000:])
        sys.exit("error: docker build failed")


def run_in_docker(a) -> int:
    require_docker()
    build_image(IMAGE, DOCKERFILE)
    passthrough = [x for flag, x in ((a.quick, "--quick"), (a.net, "--net")) if flag]
    passthrough += ["-k", a.k] if a.k else []
    run = ["run", "--rm", "--cap-add", "SYS_PTRACE"]
    rc = docker(*run, "--user", "2000", IMAGE, *passthrough)
    if not a.quick and not a.k:
        print("\n=== again as root (real chown etc. succeed) ===", flush=True)
        rc |= docker(*run, "--user", "0", IMAGE, "--quick")
    return rc


def run_cross(a) -> int:
    """Build every architecture in an ephemeral cross-toolchain image."""
    require_docker()
    return subprocess.run([sys.executable, str(ROOT / "build.py"), "--docker", "--no-ndk"]).returncode


def android_tree(api: int) -> Path:
    """The unpacked userland of an Android version, cached on the host. Unpacking
    happens in a throwaway container so the host needs no e2fsprogs/erofs tools."""
    import toolchains                   # host side only (not in the test image)
    cache = toolchains.CACHE / "android"
    tree = cache / "trees" / f"api{api}"
    if (tree / "system" / "bin" / "sh").exists():
        return tree
    archive = toolchains.find_android_image(api, "x86" if api < LEGACY_API else "x86_64")
    (cache / "trees").mkdir(parents=True, exist_ok=True)
    print(f"unpacking Android API {api} ...", flush=True)
    rc = docker("run", "--rm", "-v", f"{cache}:/cache", EXTRACT_IMAGE,
                f"/cache/zips/{archive.name}", f"/cache/trees/api{api}", quiet=True)
    if rc != 0:
        sys.exit(f"error: couldn't unpack {archive.name}")
    archive.unlink()                    # the tree is enough; zips are large
    return tree


def run_android(a) -> int:
    require_docker()
    apis = a.android or DEFAULT_ANDROID_APIS
    for name, flag in ((ANDROID_BINARY, "--ndk"), (LEGACY_BINARY, "--musl")):
        if any(android_binary(api) == name for api in apis) and not (BUILD / name / "bin" / "emuroot").exists():
            subprocess.run([sys.executable, str(ROOT / "build.py"), flag], check=True)
    build_image(IMAGE, DOCKERFILE)
    build_image(EXTRACT_IMAGE, "tests/docker/Dockerfile.android")

    failed = []
    for api in apis:
        tree = android_tree(api)
        name = android_binary(api)
        print(f"\n=== Android API {api} ===", flush=True)
        # The image's /system and /apex go at their real paths, like on a device:
        # the NDK build's ELF interpreter is /system/bin/linker64.
        mounts = ["-v", f"{tree}/system:/system:ro", "-v", f"{tree}/apex:/apex:ro",
                  "-v", f"{BUILD / name}:/opt/emuroot/build/{name}:ro"]
        args = ["run", "--rm", "--cap-add", "SYS_PTRACE", "--user", "2000", *mounts, IMAGE,
                "--android-suite", f"--android-api={api}", *(["-k", a.k] if a.k else [])]
        if docker(*args, quiet=True) != 0:
            failed.append(api)
    print("\nAndroid:", f"FAILED on API {failed}" if failed else "all versions passed")
    return 1 if failed else 0


# --- running here --------------------------------------------------------------

def run_local(a) -> int:
    if a.list:
        names = [*CASES, *ENVONLY_CASES, *[f"<distro>/{n}" for n in DISTRO_CASES], *NET_CASES,
                 *[f"<android>/{n}" for n in ANDROID_CASES]]
        print("\n".join(names))
        return 0

    if a.android_suite:
        name = android_binary(int(a.android_api)) if str(a.android_api).isdigit() else ANDROID_BINARY
        binary = a.binary or BUILD / name / "bin" / "emuroot"
        target = LocalTarget(binary)
        print(f"emuroot tests on Android API {a.android_api} userland ({name} build)")
        return summarize(run_android_cases(target, a.k))

    if a.adb:
        target = AdbTarget(a.serial, a.binary, BUILD, static=a.static)
        print(f"emuroot tests on {target.describe()}")
        results = run_android_cases(target, a.k)
        if any(not r.ok for r in results):
            diagnose_android(target)
        return summarize(results)
    else:
        binary = a.binary or BUILD / "native" / "bin" / "emuroot"
        if not binary.exists():
            subprocess.run(["make", "-C", str(ROOT)], check=True)
        target = LocalTarget(binary)
    print(f"emuroot tests on {target.describe()}")

    results = []
    print("\n[basic]")
    results += run_cases(target, CASES, basic_preamble(target), a.k)
    if not a.adb:
        # kernels before 4.8 can't use the seccomp fast path: test the fallback too
        print("\n[basic, every syscall traced]  (EMUROOT_NO_SECCOMP=1)")
        results += run_cases(target, CASES, "export EMUROOT_NO_SECCOMP=1\n" + basic_preamble(target),
                             a.k, prefix="no-seccomp/")

    if not a.adb:
        for method, label in (("procmem", "/proc/PID/mem"), ("peek", "PTRACE_PEEK/POKE")):
            print(f"\n[basic, guest memory via {label}]  (EMUROOT_GUEST_MEM={method})")
            results += run_cases(target, CASES, f"export EMUROOT_GUEST_MEM={method}\n" + basic_preamble(target),
                                 a.k, prefix=f"{method}/")
        print("\n[basic, emulating an ancient kernel]  (no seccomp, GETREGS, no EXITKILL, PEEK/POKE memory)")
        results += run_cases(target, CASES, "export EMUROOT_NO_SECCOMP=1 EMUROOT_REGS=getregs "
                             "EMUROOT_NO_EXITKILL=1 EMUROOT_GUEST_MEM=peek\n" + basic_preamble(target),
                             a.k, prefix="ancient/")
        portable = BUILD / "portable" / "bin" / "emuroot"
        print("\n[env-only fallback]  (EMUROOT_BACKEND=envonly)")
        results += run_cases(target, ENVONLY_CASES,
                             "export EMUROOT_BACKEND=envonly\n" + basic_preamble(target), a.k)
        if portable.exists():
            print("\n[env-only fallback]  (portable build, no ptrace code)")
            results += run_cases(target, ENVONLY_CASES, basic_preamble(target, str(portable)), a.k,
                                 prefix="portable/")

        if not a.quick and Path(a.rootfs).is_dir():
            wanted = [(name, spec) for name, spec in DISTROS.items() if (Path(a.rootfs) / name).is_dir()]
            for name, (osid, tools, build) in wanted:
                print(f"\n[{name}]")
                cases = {n: s for n, (needs, s) in DISTRO_CASES.items() if needs <= tools}
                if a.net:
                    cases |= {n: s for n, (needs, s) in NET_CASES.items() if needs <= tools}
                results += run_cases(target, cases,
                                     distro_preamble(target, f"{a.rootfs}/{name}", osid,
                                                     str(BUILD / build / "bin" / "emuroot")),
                                     a.k, prefix=f"{name}/")
    return summarize(results)


def run_android_cases(target, pattern: str):
    """Runs the Android cases the target's userland can support."""
    present = probe_commands(target, ALL_COMMANDS)
    cases = applicable(present)
    skipped = [n for n in ANDROID_CASES if n not in cases]
    print(f"  userland has: {' '.join(sorted(present)) or '(nothing extra)'}")
    if skipped:
        print(f"  skipping (a command is missing): {', '.join(skipped)}")
    return run_cases(target, cases, android_preamble(target), pattern)


def diagnose_android(target) -> None:
    """On a device, what to look at first when path redirection doesn't work."""
    script = f"""
        echo "kernel: $(uname -r)  selinux: $(getenforce 2>&1)  user: $(id)"
        W={target.work}/diag; mkdir -p $W/r/etc; echo fake > $W/r/etc/hostname
        for tool in cat toybox; do ls -Z /system/bin/$tool 2>&1; done
        echo "--- cat /etc/hostname, traced (want: fake)"
        env PATH=/system/bin {target.emuroot} -v -r $W/r -b /system:/system -b /dev:/dev -b /proc:/proc -- cat /etc/hostname 2>&1 | tail -15
        echo "--- denials"; dmesg 2>/dev/null | grep -i avc | tail -5
    """
    print("\n[diagnostics]")
    print(target.run(script)[1])


def summarize(results) -> int:
    failed = [r.name for r in results if not r.ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed"
          + (f"; FAILED: {', '.join(failed)}" if failed else ""))
    return 1 if failed or not results else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--local", action="store_true", help="run here instead of in Docker")
    p.add_argument("--adb", action="store_true", help="run on an Android device/emulator via adb")
    p.add_argument("-s", "--serial", help="adb device serial")
    p.add_argument("--android", nargs="*", type=int, metavar="API",
                   help="run against Android versions' real userlands (Docker)")
    p.add_argument("--static", action="store_true", help="with --adb: test the static build even on API 21+")
    p.add_argument("--cross", action="store_true", help="check that every architecture builds (Docker)")
    p.add_argument("--quick", action="store_true", help="skip the distro root filesystems")
    p.add_argument("--net", action="store_true", help="include cases that need the network")
    p.add_argument("--binary", type=Path, help="emuroot binary to test (--local/--adb)")
    p.add_argument("--rootfs", default="/rootfs", help="directory holding distro root filesystems (--local)")
    p.add_argument("-k", default="", metavar="SUBSTR", help="only run matching cases")
    p.add_argument("--list", action="store_true", help="list case names")
    p.add_argument("--android-suite", action="store_true", help=argparse.SUPPRESS)   # inside the container
    p.add_argument("--android-api", default="?", help=argparse.SUPPRESS)
    a = p.parse_args()

    if a.cross:
        return run_cross(a)
    if a.android is not None:
        return run_android(a)
    if a.local or a.adb or a.list or a.android_suite:
        return run_local(a)
    return run_in_docker(a)


if __name__ == "__main__":
    sys.exit(main())
