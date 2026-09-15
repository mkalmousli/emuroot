"""
spec/ - every plain-data table that defines what emuroot *is*: nothing
in this package runs a subprocess, touches the filesystem, or renders a
single line of C. If you're looking for the one place to change emuroot's
name/author/license/version, which architectures it supports, which
syscalls it intercepts, or which toolchain builds which architecture -
it's one of these four files:

  project.py        author, license, version, one-line summary
  architectures.py   per-CPU ptrace register layout + arch auto-detection
  syscalls.py         which syscalls, which args are paths, identity faking
  toolchains.py       cross-compiler + apt packages per architecture

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from .project import PROJECT, Project
from .architectures import ARCHES
from .syscalls import SYSCALL_CANDIDATES, PATH_SYSCALLS, IDENTITY_ACTIONS
from .toolchains import GNU_ARCHES, MUSL_TARGETS, NDK_VERSION

__all__ = [
    "PROJECT", "Project", "ARCHES",
    "SYSCALL_CANDIDATES", "PATH_SYSCALLS", "IDENTITY_ACTIONS",
    "GNU_ARCHES", "MUSL_TARGETS", "NDK_VERSION",
]
