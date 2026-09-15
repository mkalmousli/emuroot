"""
templates/ - one render() function per file build.py generates, grouped
by what they produce:

  headers/    the .h files (include/emuroot.h, src/*.h)
  sources/    the .c files (src/*.c, src/cli/main.c)
  docs/       README.md written alongside the generated source

Each module here takes the single `Project` from generator.spec and
returns the file's full text - no file I/O happens in this package;
build.py decides where each string gets written.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from . import headers, sources, docs

__all__ = ["headers", "sources", "docs"]
