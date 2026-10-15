"""Targets (where tests run) and the runner that executes tests/cases.py on them.

A Target runs one shell script natively and returns (exit_code, output):
  LocalTarget  the machine running the tests
  AdbTarget    an Android device/emulator, through `adb shell`
"""
import re
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path

from tests.cases import CASES

# Directories bound into the fake root so the system's own tools keep working.
HOST_BINDS = ["/usr", "/bin", "/lib", "/lib64", "/proc", "/dev"]
ANDROID_BINDS = ["/system", "/system_ext", "/product", "/vendor", "/odm", "/apex",
                "/linkerconfig", "/dev", "/proc"]

PREAMBLE = """\
EMUROOT={emuroot}; W={work}; R=$W/root
BINDS="{binds}"; E="$EMUROOT -r $R $BINDS"
eq() {{ [ "$1" = "$2" ] || {{ echo "expected [$2], got [$1]"; exit 1; }}; }}
rm -rf $W/root; mkdir -p $R/etc $R/tmp
"""

# Android ABI -> build.py output directory.
ANDROID_ABIS = {"x86_64": "android-x86_64", "x86": "android-x86",
                "arm64-v8a": "android-arm64", "armeabi-v7a": "android-armv7"}


class LocalTarget:
    binds = HOST_BINDS

    def __init__(self, binary: Path):
        self.binary, self.work = binary, Path("/tmp/emuroot-test")
        self.work.mkdir(exist_ok=True)
        self.emuroot = str(binary.resolve())

    def describe(self) -> str:
        return "local host"

    def run(self, script: str) -> tuple[int, str]:
        p = subprocess.run(["sh", "-c", script], capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout + p.stderr


class AdbTarget:
    binds = ANDROID_BINDS
    work = "/data/local/tmp/emuroot-test"

    def __init__(self, serial: str | None, binary: Path | None, build_dir: Path):
        self.adb = ["adb", *(["-s", serial] if serial else [])]
        self.emuroot = f"{self.work}/emuroot"
        self.api = self._sh("getprop ro.build.version.sdk")
        self.release = self._sh("getprop ro.build.version.release")
        self.abi = self._sh("getprop ro.product.cpu.abi")
        if binary is None:
            if self.abi not in ANDROID_ABIS:
                raise SystemExit(f"error: no emuroot build for Android ABI {self.abi!r}")
            binary = build_dir / ANDROID_ABIS[self.abi] / "bin" / "emuroot"
        if not binary.exists():
            raise SystemExit(f"error: {binary} not found - build it first "
                             f"(./build.py --ndk)")
        self._sh(f"mkdir -p {self.work}")
        subprocess.run([*self.adb, "push", str(binary), self.emuroot], check=True,
                       capture_output=True)
        self._sh(f"chmod 755 {self.emuroot}")

    def _sh(self, cmd: str) -> str:
        p = subprocess.run([*self.adb, "shell", cmd], capture_output=True, text=True, timeout=60)
        return p.stdout.strip()

    def describe(self) -> str:
        return f"Android {self.release} (API {self.api}, {self.abi})"

    def run(self, script: str) -> tuple[int, str]:
        # Older adb versions don't propagate the exit status, so print it.
        out = self._sh(f"sh -c {shlex.quote(script)}; echo __RC=$?")
        m = re.search(r"__RC=(\d+)\s*$", out)
        return (int(m.group(1)) if m else 1), re.sub(r"__RC=\d+\s*$", "", out)


@dataclass
class Result:
    name: str
    ok: bool
    output: str


def run_cases(target, pattern: str = "") -> list[Result]:
    preamble = PREAMBLE.format(emuroot=target.emuroot, work=target.work,
                               binds=" ".join(f"-b {d}:{d}" for d in target.binds))
    results = []
    for name, body in CASES.items():
        if pattern not in name:
            continue
        try:
            rc, out = target.run(preamble + body)
        except subprocess.TimeoutExpired:
            rc, out = 1, "timed out"
        results.append(Result(name, rc == 0, out.strip()))
        print(f"  {'PASS' if rc == 0 else 'FAIL'}  {name}")
        if rc != 0 and out.strip():
            print("        " + out.strip().replace("\n", "\n        "))
    return results
