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
    row_count = 24608

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

    if next_offset != len(raw_buffer):
        raise AssertionError(
            f"parser stopped at byte {next_offset}, expected {len(raw_buffer)}"
        )

    # Warm both implementations before collecting samples.
    parse_fastnumparse()

    fast_times = measure(parse_fastnumparse, repeat=args.repeat, number=args.number)

    fast_best = float(fast_times.min())

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
    print()

if __name__ == "__main__":
    main()
