import fastnumparse
import numpy as np


def test_native_extension_is_importable() -> None:
    assert fastnumparse._native_version() == fastnumparse.__version__


def test_from_string_buffer_csv_is_public() -> None:
    values, next_offset = fastnumparse.from_string_buffer_csv(
        b"1 2 3\n4 5 6\n", 0, np.dtype("float64"), "#", 2, 3, 2
    )

    np.testing.assert_array_equal(values, [[1, 2, 3], [4, 5, 6]])
    assert next_offset == 12
