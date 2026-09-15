"""
project.py - THE single source of truth for emuroot's identity.

Every file build.py generates (C sources, headers, the per-build LICENSE
and README) pulls its author line, license, and version number from the
one `PROJECT` object here. Change a value in this file and rebuild
(`./build.py`) - every emitted file picks it up automatically. Nothing
downstream should ever hardcode the author's name, the license
identifier, or the version number again.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Project:
    name: str = "emuroot"
    author: str = "Mohamad Almousli"
    repo: str = "https://github.com/mkalmousli/emuroot"
    year: int = 2026
    license_spdx: str = "GPL-3.0-only"
    version: tuple[int, int, int] = (0, 1, 0)
    summary: str = (
        "An independent, from-scratch user-space filesystem/identity "
        "emulation engine - a \"fake root\" tool in the spirit of "
        "chroot/fakeroot-style tools, but written entirely from zero "
        "with its own design and code."
    )

    @property
    def version_str(self) -> str:
        return ".".join(str(v) for v in self.version)

    @property
    def attribution(self) -> str:
        return f"Author: {self.author}. {self.license_spdx} license, see LICENSE."


PROJECT = Project()
