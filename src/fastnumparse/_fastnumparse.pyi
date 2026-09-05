import numpy as np

def _native_version() -> str: ...

def get_max_threads() -> int: ...

def set_max_threads(max_threads: int) -> None: ...

def from_string_buffer_csv(
    input: object,
    offset: int,
    dtype: object,
    comment: str,
    max_rows: int,
    column_count: int,
    ndmin: int,
) -> tuple[np.ndarray, int]: ...

def from_string_buffer_noncsv(
    input: object,
    offset: int,
    dtype: object,
    comment: str,
    end_char: str,
    nelement: int,
) -> tuple[np.ndarray, int]: ...
