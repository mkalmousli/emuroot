"""
musl.py - finds a usable musl cross-toolchain for a target architecture
on this machine, or downloads it from musl.cc if none is found, so
`build.py --all` / `--docker` / `--musl` can build the musl targets
automatically without the caller having to install anything themselves.

Each musl.cc toolchain is a single self-contained tarball (its own
gcc/ar/sysroot, statically linked, no host dependency) named
<triple>-cross.tgz - there is no apt package for most of these
architectures, so unlike the glibc GNU_ARCHES targets, this is a
download-or-nothing toolchain the same way generator/ndk.py's Android
NDK is.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

from generator.spec import MUSL_TARGETS

CACHE_DIR = Path.home() / ".cache" / "emuroot" / "musl"


def triple_for(arch_name: str) -> str:
    triple = MUSL_TARGETS.get(arch_name)
    if not triple:
        sys.exit(f"error: no musl cross-toolchain known for {arch_name!r} "
                  f"(see generator/spec/toolchains.py:MUSL_TARGETS)")
    return triple


def _bin_dir(triple: str) -> Path:
    return CACHE_DIR / f"{triple}-cross" / "bin"


def find_local(arch_name: str) -> Path | None:
    """A previously downloaded/extracted toolchain for this arch, or one
    already on PATH (e.g. installed by hand under the same triple), or
    None if neither is present."""
    triple = triple_for(arch_name)
    bin_dir = _bin_dir(triple)
    if (bin_dir / f"{triple}-gcc").exists():
        return bin_dir
    if shutil.which(f"{triple}-gcc"):
        return Path(shutil.which(f"{triple}-gcc")).parent
    return None


def download(arch_name: str) -> Path:
    """Downloads and extracts the musl.cc toolchain for arch_name into
    CACHE_DIR, reusing a previous download/extraction if already there."""
    triple = triple_for(arch_name)
    extracted = CACHE_DIR / f"{triple}-cross"
    bin_dir = extracted / "bin"
    if (bin_dir / f"{triple}-gcc").exists():
        return bin_dir

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    tarball = CACHE_DIR / f"{triple}-cross.tgz"
    if not tarball.exists():
        url = f"https://musl.cc/{triple}-cross.tgz"
        print(f"+ downloading {url}\n  (no local musl toolchain for {arch_name}; "
              f"caching to {CACHE_DIR})")

        def progress(count, block_size, total_size):
            if total_size <= 0:
                return
            pct = min(100, count * block_size * 100 // total_size)
            print(f"\r  {pct}%", end="", flush=True)

        urllib.request.urlretrieve(url, tarball, reporthook=progress)
        print()

    print(f"+ extracting {tarball.name}")
    with tarfile.open(tarball) as tf:
        tf.extractall(CACHE_DIR)
    tarball.unlink()

    if not (bin_dir / f"{triple}-gcc").exists():
        sys.exit(f"error: expected {bin_dir}/{triple}-gcc after extracting the "
                  f"musl toolchain for {arch_name!r} - unexpected archive layout?")
    return bin_dir


def find_or_fetch(arch_name: str) -> Path:
    """The single entry point build.py uses: a locally available musl
    toolchain for arch_name if one exists, else downloads it. Returns
    the directory containing <triple>-gcc and <triple>-ar."""
    local = find_local(arch_name)
    if local:
        print(f"+ using local musl toolchain at {local}")
        return local
    return download(arch_name)
