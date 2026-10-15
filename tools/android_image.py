#!/usr/bin/env python3
"""Unpack an Android SDK system image into a directory, without mounting.

  android_image.py x86_64-30_r11.zip OUT/      # an already downloaded image
  android_image.py --api 30 OUT/               # fetch Google's x86_64 image for API 30 first

OUT/ gets the image's file tree: system/ (and, from Android 10 on, apex/ with
the com.android.runtime APEX that holds bionic). Because Android is Linux,
those binaries run on any Linux kernel, so they make a faithful guest for
testing emuroot against each Android version's real userland (bionic libc,
linker, toybox, mksh) with no emulator, KVM or root needed.

Handles the layouts seen from API 21 to 36: a bare or GPT-wrapped ext4 image,
a dynamic-partition `super` image (parsed here), and EROFS. Needs e2fsprogs
(debugfs) and, for EROFS, erofs-utils (fsck.erofs).
"""
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

SDK_REPO = "https://dl.google.com/android/repository/sys-img/android/"

SECTOR = 512
EXT4_MAGIC = (1080, b"\x53\xef")
EROFS_MAGIC = (1024, b"\xe2\xe1\xf5\xe0")
LP_GEOMETRY_OFFSET = 4096
LP_METADATA_OFFSET = 4096 * 3           # reserved + 2 geometry copies
LP_HEADER_MAGIC = 0x414C5030            # "ALP0"


def fs_kind(f, offset: int) -> str | None:
    for name, (at, magic) in (("ext4", EXT4_MAGIC), ("erofs", EROFS_MAGIC)):
        f.seek(offset + at)
        if f.read(len(magic)) == magic:
            return name
    return None


def gpt_partitions(f) -> list[tuple[str, int]]:
    """[(name, byte offset)] from a GPT, or [] if there isn't one."""
    f.seek(SECTOR)
    hdr = f.read(92)
    if hdr[:8] != b"EFI PART":
        return []
    entries_lba, count, size = struct.unpack("<Q", hdr[72:80])[0], *struct.unpack("<II", hdr[80:88])
    parts = []
    for i in range(count):
        f.seek(entries_lba * SECTOR + i * size)
        e = f.read(size)
        if e[:16] == b"\0" * 16:
            continue
        first = struct.unpack("<Q", e[32:40])[0]
        name = e[56:128].decode("utf-16le").rstrip("\0")
        parts.append((name, first * SECTOR))
    return parts


def lp_partitions(f, super_offset: int) -> dict[str, int]:
    """Logical partitions of a dynamic-partition `super` image -> byte offset
    (only single-extent partitions, which is what the SDK images use)."""
    f.seek(super_offset + LP_METADATA_OFFSET)
    h = f.read(128)
    magic, _major, _minor, header_size = struct.unpack("<IHHI", h[:12])
    if magic != LP_HEADER_MAGIC:
        return {}
    # header: ... tables_size@44, partitions{offset,count,entry}@80, extents@92
    p_off, p_cnt, p_sz = struct.unpack("<III", h[80:92])
    e_off, e_cnt, e_sz = struct.unpack("<III", h[92:104])
    f.seek(super_offset + LP_METADATA_OFFSET + header_size)
    tables = f.read(max(p_off + p_cnt * p_sz, e_off + e_cnt * e_sz))
    result = {}
    for i in range(p_cnt):
        ent = tables[p_off + i * p_sz: p_off + (i + 1) * p_sz]
        name = ent[:36].split(b"\0")[0].decode()
        first_extent, num_extents = struct.unpack("<II", ent[40:48])
        if num_extents != 1:
            continue
        ext = tables[e_off + first_extent * e_sz: e_off + (first_extent + 1) * e_sz]
        _sectors, target_type, target_data = struct.unpack("<QIQ", ext[:20])
        if target_type == 0:                    # linear
            result[name] = super_offset + target_data * SECTOR
    return result


def find_system(img: Path) -> tuple[int, str]:
    """(byte offset, fs kind) of the system filesystem inside the image."""
    with open(img, "rb") as f:
        gpt = gpt_partitions(f)
        logical = {}
        for name, off in gpt:
            if name == "super":
                logical = lp_partitions(f, off)
        candidates = [logical.get("system"), logical.get("system_a")]
        candidates += [off for name, off in gpt if name.startswith("system")]
        candidates += [0] + [off for name, off in gpt if name != "userdata"]
        for off in candidates:
            kind = fs_kind(f, off) if off is not None else None
            if kind:
                return off, kind
    raise SystemExit(f"error: no ext4/erofs system partition found in {img}")


def extract_fs(img: Path, offset: int, kind: str, out: Path, subdir: str = "/") -> None:
    out.mkdir(parents=True, exist_ok=True)
    if kind == "ext4":
        subprocess.run(["debugfs", "-R", f"rdump {subdir} {out}", f"{img}?offset={offset}"],
                       check=True, capture_output=True)
    else:
        with tempfile.NamedTemporaryFile(dir=img.parent, suffix=".erofs") as tmp, open(img, "rb") as f:
            f.seek(offset)
            shutil.copyfileobj(f, tmp, 1 << 24)
            tmp.flush()
            subprocess.run(["fsck.erofs", f"--extract={out}", tmp.name], check=True, capture_output=True)


def extract_runtime_apex(root: Path, out: Path) -> bool:
    """Android 10+: bionic lives in the com.android.runtime APEX (a zip holding
    an ext4/EROFS payload); the device mounts it at /apex/com.android.runtime."""
    for flat in sorted((root / "system" / "apex").glob("com.android.runtime*")):
        if flat.is_dir():                       # Android 10 images ship APEXes already unpacked
            shutil.copytree(flat, out / "apex" / "com.android.runtime", symlinks=True)
            return True
    for name in ("com.android.runtime.apex", "com.android.runtime.release.apex",
                 "com.android.runtime.capex"):
        apex = next(iter(root.glob(f"**/apex/{name}")), None)
        if not apex:
            continue
        with tempfile.TemporaryDirectory(dir=out.parent) as tmp:
            with zipfile.ZipFile(apex) as zf:
                names = zf.namelist()
                if "apex_payload.img" not in names and "original_apex" in names:    # .capex wraps an .apex
                    inner = Path(tmp) / "inner.apex"
                    inner.write_bytes(zf.read("original_apex"))
                    zf = zipfile.ZipFile(inner)
                zf.extract("apex_payload.img", tmp)
            payload = Path(tmp) / "apex_payload.img"
            with open(payload, "rb") as f:
                kind = fs_kind(f, 0)
            if kind:
                extract_fs(payload, 0, kind, out / "apex" / "com.android.runtime")
                return True
    return False


def download_image(api: int, dest: Path) -> Path:
    """Fetch the SDK's default x86_64 system image for an API level."""
    xml = urllib.request.urlopen(SDK_REPO + "sys-img2-3.xml", timeout=60).read().decode()
    m = re.search(rf'path="system-images;android-{api};default;x86_64".*?<url>([^<]+)</url>', xml, re.S)
    if not m:
        raise SystemExit(f"error: no default x86_64 system image for API {api}")
    archive = dest / m.group(1)
    print(f"downloading {SDK_REPO + m.group(1)}", flush=True)
    urllib.request.urlretrieve(SDK_REPO + m.group(1), archive)
    return archive


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 3 and args[0] == "--api":
        out = Path(args[2])
        out.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=out.parent) as tmp:
            unpack(download_image(int(args[1]), Path(tmp)), out)
    elif len(args) == 2:
        unpack(Path(args[0]), Path(args[1]))
    else:
        sys.exit(__doc__)


def unpack(archive: Path, out: Path) -> None:
    with tempfile.TemporaryDirectory(dir=out.parent if out.parent.exists() else None) as tmp:
        with zipfile.ZipFile(archive) as zf:
            member = next(n for n in zf.namelist() if n.endswith("system.img"))
            img = Path(zf.extract(member, tmp))
        offset, kind = find_system(img)
        print(f"{archive.name}: system is {kind} at byte {offset}", flush=True)
        extract_fs(img, offset, kind, out)
    if not (out / "system" / "bin").is_dir():
        wrap_in_system(out)         # old images: the image root *is* /system
    extract_runtime_apex(out, out)
    prune(out)


def wrap_in_system(root: Path) -> None:
    """Android 8 and older ship the system partition alone; later ones ship a
    whole root (with system/ inside). Normalize to the latter."""
    inner = root.with_name(root.name + ".system")
    inner.mkdir()
    for entry in list(root.iterdir()):
        entry.rename(inner / entry.name)
    inner.rename(root / "system")


# Parts of an Android image that a command-line userland doesn't need (and which
# make up most of its size): apps, the Java framework, fonts, APEX packages.
UNNEEDED = ["system/app", "system/priv-app", "system/framework", "system/fonts", "system/media",
            "system/apex", "system/overlay", "system/product", "system/system_ext",
            "system_ext/app", "system_ext/priv-app", "product", "vendor/app", "vendor/overlay",
            "vendor/lib64/hw", "vendor/lib/hw"]


def prune(root: Path) -> None:
    for rel in UNNEEDED:
        shutil.rmtree(root / rel, ignore_errors=True)


if __name__ == "__main__":
    main()
