"""Targets (where scripts run) and the runner that executes a suite's cases.

A Target runs one shell script natively and returns (exit_code, output):
  LocalTarget  the machine running the tests (inside Docker, normally)
  AdbTarget    an Android device/emulator, through `adb shell`
"""
import re
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path

# Directories bound into the fake root so the system's own tools keep working.
HOST_BINDS = ["/usr", "/bin", "/lib", "/lib64", "/proc", "/dev"]
ANDROID_BINDS = ["/system", "/system_ext", "/product", "/vendor", "/odm", "/apex",
                 "/linkerconfig", "/dev", "/proc"]

# Android ABI -> build.py output directory: the NDK (bionic, PIE) build, which needs
# Android 5+, and the static musl build, which needs only the kernel and so runs on
# every Android version (and is what to use below API 21, where PIE isn't supported).
ANDROID_ABIS = {"x86_64": "android-x86_64", "x86": "android-x86",
                "arm64-v8a": "android-arm64", "armeabi-v7a": "android-armv7"}
STATIC_ABIS = {"x86_64": "x86_64-musl", "x86": "x86-musl", "arm64-v8a": "aarch64-musl",
               "armeabi-v7a": "armv5-musl", "armeabi": "armv5-musl"}
NDK_MIN_API = 21


class LocalTarget:
    binds = HOST_BINDS
    work = "/tmp/emuroot-test"

    def __init__(self, binary: Path):
        self.emuroot = str(binary.resolve())
        Path(self.work).mkdir(exist_ok=True)

    def describe(self) -> str:
        return "local host"

    def run(self, script: str, timeout: int = 900) -> tuple[int, str]:
        p = subprocess.run(["sh", "-c", script], capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr


class AdbTarget:
    binds = ANDROID_BINDS
    work = "/data/local/tmp/emuroot-test"

    def __init__(self, serial: str | None, binary: Path | None, build_dir: Path, static: bool = False):
        self.adb = ["adb", *(["-s", serial] if serial else [])]
        self.emuroot = f"{self.work}/emuroot"
        self.api = self._sh("getprop ro.build.version.sdk")
        self.release = self._sh("getprop ro.build.version.release")
        self.abi = self._sh("getprop ro.product.cpu.abi")
        self.static = static or not self.api.isdigit() or int(self.api) < NDK_MIN_API
        if binary is None:
            table = STATIC_ABIS if self.static else ANDROID_ABIS
            if self.abi not in table:
                raise SystemExit(f"error: no emuroot build for Android ABI {self.abi!r}")
            binary = build_dir / table[self.abi] / "bin" / "emuroot"
        if not binary.exists():
            raise SystemExit(f"error: {binary} not found - build it first "
                             f"({'./build.py --musl' if self.static else './build.py --ndk'})")
        self._sh(f"mkdir -p {self.work}")
        subprocess.run([*self.adb, "push", str(binary), self.emuroot], check=True, capture_output=True)
        self._sh(f"chmod 755 {self.emuroot}")

    def _sh(self, cmd: str, timeout: int = 60) -> str:
        p = subprocess.run([*self.adb, "shell", cmd], capture_output=True, text=True, timeout=timeout)
        return p.stdout.strip()

    def describe(self) -> str:
        return f"Android {self.release} (API {self.api}, {self.abi}, {'static' if self.static else 'NDK'} build)"

    def run(self, script: str, timeout: int = 900) -> tuple[int, str]:
        # Older adb versions don't propagate the exit status, so print it.
        out = self._sh(f"sh -c {shlex.quote(script)}; echo __RC=$?", timeout)
        m = re.search(r"__RC=(\d+)\s*$", out)
        return (int(m.group(1)) if m else 1), re.sub(r"__RC=\d+\s*$", "", out)


@dataclass
class Result:
    name: str
    ok: bool
    output: str


def run_cases(target, cases: dict, preamble: str, pattern: str = "", prefix: str = "") -> list[Result]:
    """Runs each case script after `preamble`; a case passes iff it exits 0."""
    results = []
    for name, body in cases.items():
        label = prefix + name
        if pattern not in label:
            continue
        try:
            rc, out = target.run(preamble + body)
        except subprocess.TimeoutExpired:
            rc, out = 1, "timed out"
        results.append(Result(label, rc == 0, out.strip()))
        print(f"  {'PASS' if rc == 0 else 'FAIL'}  {label}", flush=True)
        if rc != 0 and out.strip():
            print("        " + out.strip()[-20000:].replace("\n", "\n        "), flush=True)
    return results


# --- suite preambles: the shell prelude each suite's cases run after ---------

def basic_preamble(target, emuroot: str | None = None) -> str:
    """For tests/cases.py: $E runs emuroot over a fresh fake root with the
    host's tools bound in."""
    binds = " ".join(f"-b {d}:{d}" for d in target.binds)
    return f"""\
EMUROOT={emuroot or target.emuroot}; W={target.work}; R=$W/root
EMBED=$(dirname {emuroot or target.emuroot})/embed_test
BINDS="{binds}"; E="$EMUROOT -r $R $BINDS"
eq() {{ [ "$1" = "$2" ] || {{ echo "expected [$2], got [$1]"; exit 1; }}; }}
rm -rf {target.work}/root; mkdir -p $R/etc $R/tmp
"""


def distro_preamble(target, rootfs: str, osid: str, emuroot: str | None = None) -> str:
    """For tests/distros.py: $ER runs emuroot as fake root over a real
    distribution's root filesystem."""
    return f"""\
EMUROOT={emuroot or target.emuroot}; D={rootfs}; OSID={osid}
E0="$EMUROOT -r $D -b /proc:/proc -b /dev:/dev -b /etc/resolv.conf:/etc/resolv.conf"
ER="$E0 -u 0:0"
eq() {{ [ "$1" = "$2" ] || {{ echo "expected [$2], got [$1]"; exit 1; }}; }}
find {rootfs}/tmp {rootfs}/root -mindepth 1 -delete 2>/dev/null; true
"""


def android_preamble(target) -> str:
    """For tests/android.py. Written for the oldest shells: no `test`/`[`, no `env`.
    `E` runs emuroot over a fresh fake root with the guest's PATH at /system/bin."""
    binds = " ".join(f"-b {d}:{d}" for d in ANDROID_BINDS)
    return f"""\
EMUROOT={target.emuroot}; W={target.work}; R=$W/root
E() {{ PATH=/system/bin:/system/xbin $EMUROOT -r $R {binds} "$@"; }}
eq() {{ case "$1" in "$2") ;; *) echo "expected [$2], got [$1]"; exit 1;; esac; }}
mkdir $W 2>/dev/null; rm -r $R 2>/dev/null; mkdir $R; mkdir $R/etc; mkdir $R/tmp
"""


def probe_commands(target, commands) -> set[str]:
    """Which of `commands` the target's /system/bin (or xbin) really has. A missing
    command makes the shell say "not found"; anything else, even a usage error, means
    it exists. Uses only what the oldest shells have."""
    script = f"""\
mkdir {target.work} 2>/dev/null; mkdir {target.work}/probe 2>/dev/null; cd {target.work}/probe
PATH=/system/bin:/system/xbin; export PATH
for c in {" ".join(commands)}; do
    out=$($c --emuroot-probe </dev/null 2>&1)
    case "$out" in *"not found"*) ;; *) echo "HAVE $c";; esac
done
cd ..; rm -r probe 2>/dev/null
"""
    _, out = target.run(script)
    return {line.split()[1] for line in out.splitlines() if line.startswith("HAVE ")}
