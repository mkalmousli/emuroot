"""Renderers for emuroot's generated .h files (each: render(project) -> str,
except arch_h, which is render_one(project, arch_key, syscalls) -> str
since its content depends on which architecture build.py resolved).

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from . import emuroot_h, internal_h, regs_io_h, arch_h

__all__ = ["emuroot_h", "internal_h", "regs_io_h", "arch_h"]
