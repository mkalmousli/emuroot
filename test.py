#!/usr/bin/env python3
"""test.py - run emuroot's real-world tests (tests/cases.py) against a built binary.

  ./test.py                       on this machine (uses build/native/bin/emuroot, building it if absent)
  ./test.py --adb                 on the connected Android device/emulator (the only one, or -s SERIAL);
                                  picks build/android-<abi>/bin/emuroot from the device's ABI
  ./test.py --binary PATH         test a specific binary
  ./test.py -k rename             only cases whose name contains "rename"
  ./test.py --list                list case names

Run it once per Android version (real devices or emulator images) to
cover the whole matrix; .github/workflows/android-test.yml does this in CI.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from tests.cases import CASES
from tests.harness import AdbTarget, LocalTarget, run_cases

BUILD = ROOT / "build"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--adb", action="store_true", help="run on an Android device/emulator")
    p.add_argument("-s", "--serial", help="adb device serial")
    p.add_argument("--binary", type=Path, help="emuroot binary to test")
    p.add_argument("-k", default="", metavar="SUBSTR", help="only run matching cases")
    p.add_argument("--list", action="store_true")
    args = p.parse_args()

    if args.list:
        print("\n".join(CASES))
        return 0

    if args.adb:
        target = AdbTarget(args.serial, args.binary, BUILD)
    else:
        binary = args.binary or BUILD / "native" / "bin" / "emuroot"
        if not binary.exists():
            subprocess.run(["make", "-C", str(ROOT)], check=True)
        target = LocalTarget(binary)

    print(f"emuroot tests on {target.describe()}")
    results = run_cases(target, args.k)
    failed = [r.name for r in results if not r.ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed"
          + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
