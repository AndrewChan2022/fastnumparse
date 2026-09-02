"""Fast number parsing backed by C++."""

from importlib.metadata import version

from ._fastnumparse import _native_version

__version__ = version("fastnumparse")

__all__ = ["__version__"]
