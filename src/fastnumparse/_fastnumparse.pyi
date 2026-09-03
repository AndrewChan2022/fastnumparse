import numpy as np

def _native_version() -> str: ...

def from_string_buffer_csv(
    input: object,
    offset: int,
    dtype: object,
    comment: str,
    max_rows: int,
    column_count: int,
    ndmin: int,
    max_threads: int = 16,
) -> tuple[np.ndarray, int]: ...
