"""Renderers for the non-code files written alongside generated source
(currently just the per-build README.md; LICENSE comes straight from
generator.banner.license_text instead of living here, since it isn't
templated beyond that).

Author: Mohamad Almousli. GPL-3.0-only license, see LICENSE.
"""
from . import readme_src

__all__ = ["readme_src"]
