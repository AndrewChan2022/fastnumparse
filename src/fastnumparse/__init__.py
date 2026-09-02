"""Fast number parsing backed by C++."""

from importlib.metadata import version

from .fastnumparse import _native_version

__version__ = version("fastnumparse")

__all__ = ["__version__"]
