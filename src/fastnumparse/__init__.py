"""Fast number parsing backed by C++."""

from importlib.metadata import version

from ._fastnumparse import _native_version, from_string_buffer_csv

__version__ = version("fastnumparse")

__all__ = ["__version__", "from_string_buffer_csv"]
