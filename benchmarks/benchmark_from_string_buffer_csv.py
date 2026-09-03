"""Benchmark fastnumparse.from_string_buffer_csv against NumPy.

Run after installing the project in the active environment:

    python benchmarks/benchmark_from_string_buffer_csv.py
"""

from __future__ import annotations

import argparse
import gc
import io
import timeit
from pathlib import Path
from typing import Callable

import numpy as np
import fastnumparse as fnp


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "assets" / "csv_float_24608_x_3.txt"


def remove_comments(text: str) -> str:
    """Remove comments because np.fromstring has no comment parameter."""
    return "\n".join(line.partition("#")[0] for line in text.splitlines())


def measure(
    function: Callable[[], object],
    *,
    repeat: int,
    number: int,
) -> np.ndarray:
    gc.collect()
    samples = timeit.repeat(function, repeat=repeat, number=number)
    return np.asarray(samples, dtype=np.float64) / number


def format_duration(seconds: float) -> str:
    if seconds < 1e-3:
        return f"{seconds * 1e6:.2f} us"
    return f"{seconds * 1e3:.2f} ms"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--dtype", choices=("float32", "float64"), default="float64")
    parser.add_argument("--columns", type=int, default=3)
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--repeat", type=int, default=7)
    parser.add_argument("--number", type=int, default=20)
    args = parser.parse_args()

    dtype = np.dtype(args.dtype)
    raw_buffer = args.input.read_bytes()
    numpy_text = remove_comments(raw_buffer.decode("ascii"))
    row_count = len(numpy_text.splitlines())

    def parse_fastnumparse() -> np.ndarray:
        values, _ = fnp.from_string_buffer_csv(
            raw_buffer,
            0,
            dtype,
            "#",
            row_count,
            args.columns,
            2,
            args.threads,
        )
        return values

    def parse_numpy_fromstring() -> np.ndarray:
        return np.fromstring(numpy_text, dtype=dtype, sep=" ").reshape(
            row_count, args.columns
        )

    def parse_numpy_loadtxt_textio() -> np.ndarray:
        with args.input.open("r", encoding="utf-8") as input_file:
            return np.loadtxt(
                input_file,
                dtype=dtype,
                comments="#",
                max_rows=row_count,
                ndmin=2,
            )

    def parse_numpy_loadtxt_bytesio() -> np.ndarray:
        return np.loadtxt(
            io.BytesIO(raw_buffer),
            dtype=dtype,
            comments="#",
            max_rows=row_count,
            ndmin=2,
        )

    fast_values, next_offset = fnp.from_string_buffer_csv(
        raw_buffer,
        0,
        dtype,
        "#",
        row_count,
        args.columns,
        2,
        args.threads,
    )
    fromstring_values = parse_numpy_fromstring()
    loadtxt_textio_values = parse_numpy_loadtxt_textio()
    loadtxt_bytesio_values = parse_numpy_loadtxt_bytesio()

    if next_offset != len(raw_buffer):
        raise AssertionError(
            f"parser stopped at byte {next_offset}, expected {len(raw_buffer)}"
        )
    np.testing.assert_allclose(fast_values, fromstring_values)
    np.testing.assert_allclose(fast_values, loadtxt_textio_values)
    np.testing.assert_allclose(fast_values, loadtxt_bytesio_values)

    # Warm both implementations before collecting samples.
    parse_fastnumparse()
    parse_numpy_fromstring()
    parse_numpy_loadtxt_textio()
    parse_numpy_loadtxt_bytesio()

    fast_times = measure(parse_fastnumparse, repeat=args.repeat, number=args.number)
    fromstring_times = measure(
        parse_numpy_fromstring, repeat=args.repeat, number=args.number
    )
    loadtxt_textio_times = measure(
        parse_numpy_loadtxt_textio, repeat=args.repeat, number=args.number
    )
    loadtxt_bytesio_times = measure(
        parse_numpy_loadtxt_bytesio, repeat=args.repeat, number=args.number
    )

    fast_best = float(fast_times.min())
    fromstring_best = float(fromstring_times.min())
    loadtxt_textio_best = float(loadtxt_textio_times.min())
    loadtxt_bytesio_best = float(loadtxt_bytesio_times.min())

    print(f"input:        {args.input}")
    print(f"shape:        ({row_count}, {args.columns})")
    print(f"dtype:        {dtype}")
    print(f"threads:      {args.threads} (0 means automatic)")
    print(f"measurements: {args.repeat} repeats x {args.number} calls")
    print("note:         comment removal for np.fromstring is performed before timing")
    print("              TextIO includes opening, reading, and decoding the file")
    print("              BytesIO uses the buffer already loaded in memory")
    print()
    print(f"fastnumparse:             {format_duration(fast_best):>10} best per call")
    print(f"np.fromstring:            {format_duration(fromstring_best):>10} best per call")
    print(f"np.loadtxt (TextIO file): {format_duration(loadtxt_textio_best):>10} best per call")
    print(f"np.loadtxt (BytesIO):     {format_duration(loadtxt_bytesio_best):>10} best per call")
    print()
    print(f"vs fromstring:            {fromstring_best / fast_best:>9.2f}x")
    print(f"vs loadtxt TextIO:        {loadtxt_textio_best / fast_best:>9.2f}x")
    print(f"vs loadtxt BytesIO:       {loadtxt_bytesio_best / fast_best:>9.2f}x")


if __name__ == "__main__":
    main()
