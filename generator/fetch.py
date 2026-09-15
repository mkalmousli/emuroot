"""
fetch.py - shared "download a URL to a file" helper for generator/ndk.py
and generator/musl.py.

Prefers curl (present on every GitHub Actions runner and virtually every
Linux dev machine): unlike a plain urllib.request.urlretrieve() call,
curl tries both IPv4 and IPv6 addresses for a dual-stack host and moves
on if one is unreachable. That matters concretely: GitHub Actions hosted
runners have no IPv6 egress route, and musl.cc/Google's NDK host both
publish AAAA records - a bare urlretrieve() that happens to pick the
IPv6 address fails hard with "Network is unreachable" instead of falling
back. Falls back to urllib, forced to IPv4 only, if curl isn't on PATH.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
import shutil
import socket
import subprocess
import urllib.request
from pathlib import Path


def download(url: str, dest: Path) -> None:
    if shutil.which("curl"):
        # --connect-timeout bounds each individual connection attempt (the
        # OS default TCP connect timeout is well over a minute, which is
        # much too slow to "fail fast" when a host is unreachable -
        # generator/musl.py's best-effort callers rely on this returning
        # quickly); --max-time bounds the whole transfer once connected.
        subprocess.run(["curl", "-fLsS", "--connect-timeout", "10", "--max-time", "120",
                         "--retry", "2", "--retry-connrefused", "-o", str(dest), url], check=True)
        return

    real_getaddrinfo = socket.getaddrinfo

    def ipv4_only(host, port, family=0, type=0, proto=0, flags=0):
        return real_getaddrinfo(host, port, socket.AF_INET, type, proto, flags)

    socket.getaddrinfo = ipv4_only
    try:
        socket.setdefaulttimeout(15)

        def progress(count, block_size, total_size):
            if total_size <= 0:
                return
            pct = min(100, count * block_size * 100 // total_size)
            print(f"\r  {pct}%", end="", flush=True)

        urllib.request.urlretrieve(url, dest, reporthook=progress)
        print()
    finally:
        socket.getaddrinfo = real_getaddrinfo
        socket.setdefaulttimeout(None)
