
# fastnumparse

Fast number parsing for NumPy, powered by C++.

> [!NOTE]
> This project is in its initial scaffolding stage; the public parsing API has
> not been defined yet.


## install


```bash
pip install fastnumparse
```

## usage


for csv-like data, with row * col,  with sep=" " and comment ="#"

```txt
1.694992566108704e+01 4.373334741592407e+01 1.491762180328369e+02  # comment
1.875837993621826e+01 4.405829811096191e+01 1.520713310241699e+02
1.766352510452271e+01 4.732764196395874e+01 1.513219413757324e+02
```

```python
import fastnumparse as fnp

path = "csv_data.txt"
raw_buffer = open(path, "rb").read()
values, _ = fnp.from_string_buffer_csv(
    raw_buffer,
    offset=0,
    dtype=np.float64,
    comment="#",
    max_rows = 3,
    column_count=3,
    ndmin=2,            # 2: return 2d array, 0~1: return 1d array
    max_threads=16,
)
```


for noncsv data, each row unkown number count, with sep=" " and comment ="#", and with an end char
```txt
15 780721 655221 
955795 127489 609646 896903 101275 343792 898984
699394 898633 781093 1361969 631076 899312 898945 899053 780575 149987
}
```


```python
import fastnumparse as fnp

path = "noncsv_data.txt"
raw_buffer = open(path, "rb").read()
values, _ = fnp.from_string_buffer_noncsv(
    raw_buffer,
    offset=0,
    dtype=np.int64,
    comment="#",
    end_char="}",
    nelement=20,
    max_threads=16,
)
```


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

## publish to PyPI

The source distribution can be uploaded to PyPI directly. Platform wheels must
be built separately for each supported operating system and Python version;
`cibuildwheel` is included in the development dependencies for that purpose.
