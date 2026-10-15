"""Finding or downloading the cross toolchains build.py needs: the Android
NDK and musl.cc's musl toolchains (glibc cross-compilers come from apt)."""
import os
import re
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path

CACHE = Path.home() / ".cache" / "emuroot"
NDK_VERSION = "r28b"


def download(url: str, dest: Path) -> None:
    """curl tries IPv4 and IPv6 in turn; a bare urllib may pick an
    unreachable IPv6 address (e.g. on GitHub runners) and fail outright."""
    print(f"+ downloading {url}")
    try:
        subprocess.run(["curl", "-fLsS", "--connect-timeout", "10", "--max-time", "600",
                        "--retry", "2", "--retry-connrefused", "-o", str(dest), url], check=True)
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        dest.unlink(missing_ok=True)
        raise RuntimeError(f"couldn't download {url}: {e}") from e


# --- Android NDK -----------------------------------------------------------

def _ndk_major(root: Path) -> str | None:
    props = root / "source.properties"
    m = re.search(r"Pkg\.Revision\s*=\s*(\d+)", props.read_text()) if props.exists() else None
    return m.group(1) if m else None


def _ndk_candidates() -> list[Path]:
    roots = [Path(os.environ[v]) for v in
             ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK_LATEST_HOME") if os.environ.get(v)]
    sdks = [Path(os.environ[v]) for v in ("ANDROID_SDK_ROOT", "ANDROID_HOME") if os.environ.get(v)]
    sdks += [Path.home() / "Android" / "Sdk", Path("/usr/local/lib/android/sdk")]
    for sdk in sdks:
        roots += sorted((sdk / "ndk").glob("*"), reverse=True)
    return roots + [CACHE / "ndk" / f"android-ndk-{NDK_VERSION}"]


def find_ndk() -> Path:
    """A local NDK with the pinned major version, else download one."""
    major = re.match(r"r(\d+)", NDK_VERSION).group(1)
    for root in _ndk_candidates():
        if root.is_dir() and _ndk_major(root) == major:
            return root
    dest = CACHE / "ndk"
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / f"android-ndk-{NDK_VERSION}-linux.zip"
    download(f"https://dl.google.com/android/repository/{archive.name}", archive)
    with zipfile.ZipFile(archive) as zf:
        zf.extractall(dest)
    archive.unlink()
    return dest / f"android-ndk-{NDK_VERSION}"


# --- musl.cc ---------------------------------------------------------------

def find_musl(triple: str) -> Path:
    """bin/ directory of the musl toolchain for `triple`: on PATH, cached, or downloaded."""
    on_path = shutil.which(f"{triple}-gcc")
    if on_path:
        return Path(on_path).parent
    bin_dir = CACHE / "musl" / f"{triple}-cross" / "bin"
    if not (bin_dir / f"{triple}-gcc").exists():
        dest = CACHE / "musl"
        dest.mkdir(parents=True, exist_ok=True)
        archive = dest / f"{triple}-cross.tgz"
        download(f"https://musl.cc/{archive.name}", archive)
        with tarfile.open(archive) as tf:
            tf.extractall(dest)
        archive.unlink()
    return bin_dir
