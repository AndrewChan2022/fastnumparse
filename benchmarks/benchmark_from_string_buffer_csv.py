"""Compare fastnumparse and NumPy for the CSV and non-CSV datasets."""

from __future__ import annotations

import argparse
import gc
import io
import re
import timeit
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import fastnumparse as fnp
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA_DIR = ROOT / "data"
DATASET_NAME = re.compile(r"^csv_(float|int)_(\d+)x(\d+)\.txt$")
NONCSV_DATASET_NAME = re.compile(r"^noncsv_(char|int)_(\d+)\.txt$")


@dataclass(frozen=True)
class Dataset:
    path: Path
    dtype: np.dtype
    rows: int
    columns: int


@dataclass(frozen=True)
class NonCsvDataset:
    path: Path
    dtype: np.dtype
    elements: int


def discover_datasets(data_dir: Path) -> list[Dataset]:
    datasets: list[Dataset] = []

    for path in sorted(data_dir.glob("csv_*_*x*.txt")):
        match = DATASET_NAME.fullmatch(path.name)
        if match is None:
            continue

        value_kind, rows, columns = match.groups()
        dtype = np.dtype(np.float64 if value_kind == "float" else np.int64)
        datasets.append(Dataset(path, dtype, int(rows), int(columns)))

    if not datasets:
        raise FileNotFoundError(f"no csv_*_*x*.txt datasets found in {data_dir}")

    return datasets


def discover_noncsv_datasets(data_dir: Path) -> list[NonCsvDataset]:
    datasets: list[NonCsvDataset] = []

    for path in sorted(data_dir.glob("noncsv_*_*.txt")):
        match = NONCSV_DATASET_NAME.fullmatch(path.name)
        if match is None:
            continue

        value_kind, elements = match.groups()
        dtype = np.dtype(np.int8 if value_kind == "char" else np.int64)
        datasets.append(NonCsvDataset(path, dtype, int(elements)))

    return datasets


def remove_comments(text: str) -> str:
    """Remove comments because np.fromstring has no comment parameter."""
    return "\n".join(line.partition("#")[0] for line in text.splitlines())


def measure(
    function: Callable[[], object],
    *,
    repeat: int,
    number: int,
) -> float:
    gc.collect()
    samples = timeit.repeat(function, repeat=repeat, number=number)
    return min(samples) / number


def format_duration(seconds: float) -> str:
    if seconds < 1e-3:
        return f"{seconds * 1e6:.2f} us"
    return f"{seconds * 1e3:.2f} ms"


def benchmark_dataset(
    dataset: Dataset,
    *,
    threads: int,
    repeat: int,
    number: int,
) -> None:
    raw_buffer = dataset.path.read_bytes()
    numpy_text = remove_comments(raw_buffer.decode("ascii"))

    def parse_fastnumparse() -> np.ndarray:
        values, _ = fnp.from_string_buffer_csv(
            raw_buffer,
            0,
            dataset.dtype,
            "#",
            dataset.rows,
            dataset.columns,
            2,
            threads,
        )
        return values

    def parse_numpy_fromstring() -> np.ndarray:
        return np.fromstring(numpy_text, dtype=dataset.dtype, sep=" ").reshape(
            dataset.rows, dataset.columns
        )

    def parse_numpy_loadtxt_textio() -> np.ndarray:
        with dataset.path.open("r", encoding="ascii", newline="") as input_file:
            return np.loadtxt(
                input_file,
                dtype=dataset.dtype,
                comments="#",
                max_rows=dataset.rows,
                ndmin=2,
            )

    def parse_numpy_loadtxt_bytesio() -> np.ndarray:
        return np.loadtxt(
            io.BytesIO(raw_buffer),
            dtype=dataset.dtype,
            comments="#",
            max_rows=dataset.rows,
            ndmin=2,
        )

    # Measure before validation so there are no explicit warm-up calls.
    fast_best = measure(parse_fastnumparse, repeat=repeat, number=number)
    fromstring_best = measure(parse_numpy_fromstring, repeat=repeat, number=number)
    loadtxt_textio_best = measure(
        parse_numpy_loadtxt_textio, repeat=repeat, number=number
    )
    loadtxt_bytesio_best = measure(
        parse_numpy_loadtxt_bytesio, repeat=repeat, number=number
    )

    fast_values, next_offset = fnp.from_string_buffer_csv(
        raw_buffer,
        0,
        dataset.dtype,
        "#",
        dataset.rows,
        dataset.columns,
        2,
        threads,
    )
    fromstring_values = parse_numpy_fromstring()
    loadtxt_textio_values = parse_numpy_loadtxt_textio()
    loadtxt_bytesio_values = parse_numpy_loadtxt_bytesio()

    if next_offset > len(raw_buffer) or raw_buffer[next_offset:].strip():
        raise AssertionError(
            f"{dataset.path.name}: unparsed data remains after byte {next_offset}"
        )
    np.testing.assert_allclose(fast_values, fromstring_values)
    np.testing.assert_allclose(fast_values, loadtxt_textio_values)
    np.testing.assert_allclose(fast_values, loadtxt_bytesio_values)

    print(dataset.path.name)
    print(f"  shape:                      ({dataset.rows}, {dataset.columns})")
    print(f"  dtype:                      {dataset.dtype}")
    print(f"  size:                       {len(raw_buffer) / (1024 * 1024):.2f} MiB")
    print(f"  fastnumparse:               {format_duration(fast_best):>10}")
    print(f"  np.fromstring:              {format_duration(fromstring_best):>10}")
    print(f"  np.loadtxt (TextIO file):   {format_duration(loadtxt_textio_best):>10}")
    print(f"  np.loadtxt (BytesIO):       {format_duration(loadtxt_bytesio_best):>10}")
    print(f"  speedup vs fromstring:      {fromstring_best / fast_best:>9.2f}x")
    print(f"  speedup vs loadtxt TextIO:  {loadtxt_textio_best / fast_best:>9.2f}x")
    print(f"  speedup vs loadtxt BytesIO: {loadtxt_bytesio_best / fast_best:>9.2f}x")
    print()


def benchmark_noncsv_dataset(
    dataset: NonCsvDataset,
    *,
    threads: int,
    repeat: int,
    number: int,
) -> None:
    raw_buffer = dataset.path.read_bytes()
    payload = raw_buffer.partition(b"}")[0]

    def parse_fastnumparse() -> np.ndarray:
        values, _ = fnp.from_string_buffer_noncsv(
            raw_buffer,
            0,
            dataset.dtype,
            "#",
            "}",
            dataset.elements,
            threads,
        )
        return values

    if dataset.dtype == np.dtype(np.int64):
        numpy_text = remove_comments(payload.decode("ascii"))

        def parse_numpy() -> np.ndarray:
            return np.fromstring(numpy_text, dtype=dataset.dtype, sep=" ")

        numpy_name = "np.fromstring"
    else:
        whitespace = b" \t\r\n\v\f"

        def parse_numpy() -> np.ndarray:
            compact = payload.translate(None, whitespace)
            return np.frombuffer(compact, dtype=dataset.dtype)

        numpy_name = "bytes.translate + np.frombuffer"

    # Measure before validation so there are no explicit warm-up calls.
    fast_best = measure(parse_fastnumparse, repeat=repeat, number=number)
    numpy_best = measure(parse_numpy, repeat=repeat, number=number)

    fast_values, next_offset = fnp.from_string_buffer_noncsv(
        raw_buffer,
        0,
        dataset.dtype,
        "#",
        "}",
        dataset.elements,
        threads,
    )
    numpy_values = parse_numpy()

    expected_shape = (dataset.elements,)
    if fast_values.shape != expected_shape:
        raise AssertionError(
            f"{dataset.path.name}: got shape {fast_values.shape}, "
            f"expected {expected_shape}"
        )
    if next_offset > len(raw_buffer) or not raw_buffer[next_offset:].lstrip().startswith(b"}"):
        raise AssertionError(
            f"{dataset.path.name}: end marker not found at byte {next_offset}"
        )
    np.testing.assert_array_equal(fast_values, numpy_values)

    print(dataset.path.name)
    print(f"  shape:                      {expected_shape}")
    print(f"  dtype:                      {dataset.dtype}")
    print(f"  size:                       {len(raw_buffer) / (1024 * 1024):.2f} MiB")
    print(f"  fastnumparse:               {format_duration(fast_best):>10}")
    print(f"  {numpy_name + ':':<28} {format_duration(numpy_best):>10}")
    print(f"  speedup:                    {numpy_best / fast_best:>9.2f}x")
    print("  np.loadtxt:                 not applicable (variable row widths)")
    print()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--number", type=int, default=1)
    args = parser.parse_args()

    datasets = discover_datasets(args.data_dir)
    noncsv_datasets = discover_noncsv_datasets(args.data_dir)

    print(f"datasets:     {len(datasets) + len(noncsv_datasets)}")
    print(f"threads:      {args.threads} (0 means automatic)")
    print(f"measurements: {args.repeat} repeats x {args.number} calls")
    print("warm-up:      none")
    print("notes:        np.fromstring comment removal is outside timing")
    print("              loadtxt TextIO includes opening and reading the file")
    print("              loadtxt BytesIO uses the preloaded buffer")
    print()

    for dataset in datasets:
        benchmark_dataset(
            dataset,
            threads=args.threads,
            repeat=args.repeat,
            number=args.number,
        )

    for dataset in noncsv_datasets:
        benchmark_noncsv_dataset(
            dataset,
            threads=args.threads,
            repeat=args.repeat,
            number=args.number,
        )


if __name__ == "__main__":
    main()
