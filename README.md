
# fastnumparse

Fast number parsing for NumPy, powered by C++.

> [!NOTE]
> This project is in its initial scaffolding stage; the public parsing API has
> not been defined yet.

## Development setup

Create and activate a virtual environment, then install the project in editable
mode with its development dependencies:

```console
python -m pip install -e ".[dev]"
```

This compiles the C++ extension with CMake and pybind11. Run the tests with:

```console
python -m pytest
```

## Build distribution artifacts

```console
python -m build
python -m twine check dist/*
```

The source distribution can be uploaded to PyPI directly. Platform wheels must
be built separately for each supported operating system and Python version;
`cibuildwheel` is included in the development dependencies for that purpose.
