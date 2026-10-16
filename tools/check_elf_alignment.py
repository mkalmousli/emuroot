#!/usr/bin/env python3
"""Checks that every native library in an AAR / APK / ELF file is built the way
Google Play requires for Android 15+ devices with 16 KB memory pages.

  check_elf_alignment.py emuroot-release.aar [more.apk ...]

For each 64-bit ELF shared object (arm64-v8a, x86_64) every PT_LOAD segment must be
aligned to at least 16384 bytes. 32-bit libraries are only reported: devices with
16 KB pages are 64-bit only and never load them. Also checks that the ABIs include
a 64-bit one whenever there is a 32-bit one (Google Play requires 64-bit support).
Exit status 1 on any problem.
"""
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

PAGE = 16384
PT_LOAD = 1


def load_alignments(data: bytes) -> tuple[bool, list[int]]:
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    is64, little = data[4] == 2, data[5] == 1
    e = "<" if little else ">"
    if is64:
        phoff, = struct.unpack_from(e + "Q", data, 0x20)
        phentsize, phnum = struct.unpack_from(e + "HH", data, 0x36)
    else:
        phoff, = struct.unpack_from(e + "I", data, 0x1C)
        phentsize, phnum = struct.unpack_from(e + "HH", data, 0x2A)
    aligns = []
    for i in range(phnum):
        off = phoff + i * phentsize
        if struct.unpack_from(e + "I", data, off)[0] != PT_LOAD:
            continue
        # p_align: 64-bit at +0x30 (Q), 32-bit at +0x1C (I)
        aligns.append(struct.unpack_from(e + ("Q" if is64 else "I"), data, off + (0x30 if is64 else 0x1C))[0])
    return is64, aligns


def check(path: Path) -> int:
    problems = 0
    abis = set()
    with zipfile.ZipFile(path) as zf:
        for info in zf.infolist():
            parts = info.filename.split("/")
            if not info.filename.endswith(".so") or "lib" not in parts[:2] and "jni" not in parts[:2]:
                continue
            abis.add(parts[-2])
            try:
                is64, aligns = load_alignments(zf.read(info))
            except ValueError as err:
                print(f"  ?? {info.filename}: {err}")
                continue
            ok = bool(aligns) and all(a >= PAGE for a in aligns)
            verdict = "ok " if ok else ("BAD" if is64 else "n/a")      # 32-bit: not required
            print(f"  {verdict} {info.filename}: segment alignment {sorted(set(aligns))}")
            problems += is64 and not ok
    if abis & {"armeabi-v7a", "x86"} and not abis & {"arm64-v8a", "x86_64"}:
        print("  BAD: 32-bit ABIs without 64-bit ones (Google Play requires 64-bit)")
        problems += 1
    print(f"  ABIs: {', '.join(sorted(abis)) or '(none)'}")
    return problems


def main() -> int:
    total = 0
    for name in sys.argv[1:] or [sys.exit(__doc__)]:
        print(name)
        p = Path(name)
        if p.suffix in (".aar", ".apk", ".zip"):
            total += check(p)
        else:
            is64, aligns = load_alignments(p.read_bytes())
            ok = all(a >= PAGE for a in aligns)
            print(f"  {'ok ' if ok else ('BAD' if is64 else 'n/a')} segment alignment {sorted(set(aligns))}")
            total += is64 and not ok
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
