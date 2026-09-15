"""Renderers for emuroot's generated .c files, each: render(project) -> str.

LIB_MODULES build into libemuroot.a; CLI_MODULES link against it to
produce the emuroot binary - build.py's single source of truth for
which generated .c file goes into which output.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from . import (
    bindings_c,
    path_c,
    platform_c,
    emuroot_api_c,
    ptrace_engine_c,
    envonly_engine_c,
    main_c,
)

LIB_MODULES = [bindings_c, path_c, platform_c, emuroot_api_c, ptrace_engine_c, envonly_engine_c]
CLI_MODULES = [main_c]

__all__ = [
    "bindings_c", "path_c", "platform_c", "emuroot_api_c",
    "ptrace_engine_c", "envonly_engine_c", "main_c",
    "LIB_MODULES", "CLI_MODULES",
]
