
# fastnumparse

Fast number parsing for NumPy, powered by C++.

## install

requires-python = ">=3.10,<3.15"

```bash
pip install fastnumparse
```

## usage

set parallel thread count:

```pyton
# max 16 threads
fnp.set_max_threads(16)

# max threads same as cpu core number
fnp.set_max_threads(0)
```



for csv-like data, with row * col,  with sep=" " and comment ="#"

```txt
1.694992566108704e+01 4.373334741592407e+01 1.491762180328369e+02  # comment
1.875837993621826e+01 4.405829811096191e+01 1.520713310241699e+02
1.766352510452271e+01 4.732764196395874e+01 1.513219413757324e+02
```

```python
import fastnumparse as fnp

fnp.set_max_threads(16)

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

fnp.set_max_threads(16)

path = "noncsv_data.txt"
raw_buffer = open(path, "rb").read()
values, _ = fnp.from_string_buffer_noncsv(
    raw_buffer,
    offset=0,
    dtype=np.int64,
    comment="#",
    end_char="}",
    nelement=20,
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
python tools/gendata.py
python -m pytest
```

`tools/gendata.py` extracts the compressed test and benchmark datasets from
`assets/` into the ignored `data/` directory. Pass `--force` to replace files
that have already been extracted.

## Build distribution artifacts

```console
python -m build
python -m twine check dist/*
```

## publish to PyPI

The source distribution can be uploaded to PyPI directly. Platform wheels must
be built separately for each supported operating system and Python version;
`cibuildwheel` is included in the development dependencies for that purpose.

See [docs/publish.md](docs/publish.md) for the complete cibuildwheel and
TestPyPI Trusted Publishing workflow.

And here is simple operation
- on github, click action, then choose the workflow to publish
    - Build and publish to TestPyPI
    - Build and publish to PyPI

## limited

- comment only support # now
- delimiter only support space now

## Acknowledgements

- [nanothread](https://github.com/mitsuba-renderer/nanothread) for light tbb style parallel_for
- [fast_float](https://github.com/fastfloat/fast_float) for fast string to float
- [pybind11](https://github.com/pybind/pybind11) for python binding
