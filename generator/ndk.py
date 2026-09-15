"""
ndk.py - finds a usable Android NDK on this machine, or downloads the
pinned release (generator/spec/toolchains.py:NDK_VERSION) if none is
found, so `build.py --all` / `--docker` can build the android-arm64 and
android-armv7 targets automatically without the caller having to locate
or install an NDK themselves.

"Correct version" is matched by major revision number: an NDK's
source.properties Pkg.Revision (e.g. "27.2.12479018") is compared
against NDK_VERSION's leading digits (e.g. "r27c" -> 27), so any locally
installed rNNx counts as the same NDK line and avoids a redundant
download - only the major number changes the toolchain/API surface in
practice.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import os
import re
import sys
import urllib.request
import zipfile
from pathlib import Path

from generator.spec import NDK_VERSION

CACHE_DIR = Path.home() / ".cache" / "emuroot" / "ndk"


def _major(version: str) -> str | None:
    m = re.match(r"r(\d+)", version)
    return m.group(1) if m else None


def _revision_major(ndk_root: Path) -> str | None:
    props = ndk_root / "source.properties"
    if not props.exists():
        return None
    for line in props.read_text().splitlines():
        if line.strip().startswith("Pkg.Revision"):
            m = re.search(r"=\s*(\d+)", line)
            if m:
                return m.group(1)
    return None


def _candidate_roots() -> list[Path]:
    candidates = []
    for var in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK_LATEST_HOME"):
        if os.environ.get(var):
            candidates.append(Path(os.environ[var]))
    for var in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        sdk = os.environ.get(var)
        if sdk:
            candidates += sorted(Path(sdk, "ndk").glob("*"), reverse=True)
    candidates += sorted((Path.home() / "Android" / "Sdk" / "ndk").glob("*"), reverse=True)
    candidates += sorted(Path("/usr/local/lib/android/sdk/ndk").glob("*"), reverse=True)  # GitHub Actions ubuntu image
    candidates.append(CACHE_DIR / f"android-ndk-{NDK_VERSION}")
    return candidates


def find_local(version: str = NDK_VERSION) -> Path | None:
    """An installed NDK on this machine whose major revision matches
    `version`, or None if none is found."""
    wanted = _major(version)
    for root in _candidate_roots():
        if root.is_dir() and _revision_major(root) == wanted:
            return root
    return None


def download(version: str = NDK_VERSION) -> Path:
    """Downloads and extracts the given NDK release into CACHE_DIR,
    reusing a previous download/extraction if already present there."""
    extracted = CACHE_DIR / f"android-ndk-{version}"
    if extracted.is_dir() and _revision_major(extracted) == _major(version):
        return extracted

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    zip_path = CACHE_DIR / f"android-ndk-{version}-linux.zip"
    if not zip_path.exists():
        url = f"https://dl.google.com/android/repository/android-ndk-{version}-linux.zip"
        print(f"+ downloading {url}\n  (no local Android NDK {version} found; "
              f"caching to {CACHE_DIR})")

        def progress(count, block_size, total_size):
            if total_size <= 0:
                return
            pct = min(100, count * block_size * 100 // total_size)
            print(f"\r  {pct}%", end="", flush=True)

        urllib.request.urlretrieve(url, zip_path, reporthook=progress)
        print()

    print(f"+ extracting {zip_path.name}")
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(CACHE_DIR)
    zip_path.unlink()

    if not extracted.is_dir():
        sys.exit(f"error: expected {extracted} after extracting the NDK zip - "
                  f"unexpected archive layout for version {version!r}?")
    return extracted


def find_or_fetch(version: str = NDK_VERSION) -> Path:
    """The single entry point build.py uses: a locally installed NDK
    matching `version` if one exists, else downloads it."""
    local = find_local(version)
    if local:
        print(f"+ using local Android NDK at {local}")
        return local
    return download(version)
