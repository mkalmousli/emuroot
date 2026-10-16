"""Finding or downloading the cross toolchains build.py needs: the Android
NDK and musl.cc's musl toolchains (glibc cross-compilers come from apt)."""
import os
import re
import shutil
import stat
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


def extract_zip(archive: Path, dest: Path) -> None:
    """Like ZipFile.extractall, but keeps permissions and symlinks (the NDK
    is full of executables and symlinks, which extractall flattens)."""
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            target = dest / info.filename
            mode = info.external_attr >> 16
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            elif stat.S_ISLNK(mode):
                target.parent.mkdir(parents=True, exist_ok=True)
                target.unlink(missing_ok=True)
                target.symlink_to(zf.read(info).decode())
            else:
                zf.extract(info, dest)
                if mode:
                    target.chmod(stat.S_IMODE(mode))


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
    extract_zip(archive, dest)
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


# --- Android SDK system images ------------------------------------------------

SDK_IMAGE_REPO = "https://dl.google.com/android/repository/sys-img/android/"


def find_android_image(api: int, abi: str = "x86_64") -> Path:
    """Google's default system image zip for an API level and ABI (x86_64 from
    API 21; 32-bit x86 before that), downloaded once into the cache.
    tools/android_image.py unpacks it."""
    dest = CACHE / "android" / "zips"
    dest.mkdir(parents=True, exist_ok=True)
    index = dest / "sys-img2-3.xml"
    if not index.exists() or index.stat().st_size == 0:
        download(SDK_IMAGE_REPO + "sys-img2-3.xml", index)
    pkg = re.search(rf'<remotePackage path="system-images;android-{api};default;{abi}".*?</remotePackage>',
                    index.read_text(), re.S)
    if not pkg:
        raise RuntimeError(f"no default {abi} system image for Android API {api}")
    urls = re.findall(r"<url>([^<]+)</url>", pkg.group(0))      # some are per host OS
    url = next((u for u in urls if "linux" in u), urls[0])
    archive = dest / url
    if not archive.exists():
        download(SDK_IMAGE_REPO + url, archive)
    return archive
