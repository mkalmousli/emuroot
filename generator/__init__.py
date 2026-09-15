"""
generator/ - emuroot's C source, generated fresh for one target
architecture at a time by build.py in the repository root.

Layout:
  spec/        plain-data tables: project identity, syscalls, CPU
               register layouts, cross-toolchains. Edit these to change
               what emuroot *is*.
  banner.py    renders the license text and the standard file-header
               comment every generated C file starts with.
  resolve.py   asks a target compiler for its real predefined macros and
               kernel syscall numbers (compiler introspection - the only
               module here that runs a subprocess).
  templates/   one render() per generated file, grouped into headers/,
               sources/, and docs/.

See the repository root README.md for the full picture and build.py's
module docstring for how these pieces fit together at build time.

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
