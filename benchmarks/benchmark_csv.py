"""Benchmark fastnumparse for every CSV and non-CSV dataset."""

from __future__ import annotations

import argparse
import gc
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
    repeat: int,
    number: int,
) -> None:
    raw_buffer = dataset.path.read_bytes()

    def parse_fastnumparse() -> np.ndarray:
        values, _ = fnp.from_string_buffer_csv(
            raw_buffer,
            0,
            dataset.dtype,
            "#",
            dataset.rows,
            dataset.columns,
            2,
        )
        return values

    # Measure before validation so there is no explicit warm-up call.
    best = measure(parse_fastnumparse, repeat=repeat, number=number)

    values, next_offset = fnp.from_string_buffer_csv(
        raw_buffer,
        0,
        dataset.dtype,
        "#",
        dataset.rows,
        dataset.columns,
        2,
    )
    expected_shape = (dataset.rows, dataset.columns)
    if values.shape != expected_shape:
        raise AssertionError(
            f"{dataset.path.name}: got shape {values.shape}, "
            f"expected {expected_shape}"
        )
    if next_offset > len(raw_buffer) or raw_buffer[next_offset:].strip():
        raise AssertionError(
            f"{dataset.path.name}: unparsed data remains after byte {next_offset}"
        )

    print(dataset.path.name)
    print(f"  shape:        {expected_shape}")
    print(f"  dtype:        {dataset.dtype}")
    print(f"  size:         {len(raw_buffer) / (1024 * 1024):.2f} MiB")
    print(f"  fastnumparse: {format_duration(best)} best per call")
    print()


def benchmark_noncsv_dataset(
    dataset: NonCsvDataset,
    *,
    repeat: int,
    number: int,
) -> None:
    raw_buffer = dataset.path.read_bytes()

    def parse_fastnumparse() -> np.ndarray:
        values, _ = fnp.from_string_buffer_noncsv(
            raw_buffer,
            0,
            dataset.dtype,
            "#",
            "}",
            dataset.elements,
        )
        return values

    # Measure before validation so there is no explicit warm-up call.
    best = measure(parse_fastnumparse, repeat=repeat, number=number)

    values, next_offset = fnp.from_string_buffer_noncsv(
        raw_buffer,
        0,
        dataset.dtype,
        "#",
        "}",
        dataset.elements,
    )
    expected_shape = (dataset.elements,)
    if values.shape != expected_shape:
        raise AssertionError(
            f"{dataset.path.name}: got shape {values.shape}, "
            f"expected {expected_shape}"
        )
    if next_offset > len(raw_buffer) or not raw_buffer[next_offset:].lstrip().startswith(b"}"):
        raise AssertionError(
            f"{dataset.path.name}: end marker not found at byte {next_offset}"
        )

    print(dataset.path.name)
    print(f"  shape:        {expected_shape}")
    print(f"  dtype:        {dataset.dtype}")
    print(f"  size:         {len(raw_buffer) / (1024 * 1024):.2f} MiB")
    print(f"  fastnumparse: {format_duration(best)} best per call")
    print()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--number", type=int, default=1)
    args = parser.parse_args()
    fnp.set_max_threads(args.threads)

    datasets = discover_datasets(args.data_dir)
    noncsv_datasets = discover_noncsv_datasets(args.data_dir)

    print(f"datasets:     {len(datasets) + len(noncsv_datasets)}")
    print(f"threads:      {args.threads} (0 means automatic)")
    print(f"measurements: {args.repeat} repeats x {args.number} calls")
    print("warm-up:      none")
    print()

    for dataset in datasets:
        # if not dataset.path.name.endswith("noncsv_int_244761.txt"): continue
        benchmark_dataset(
            dataset,
            repeat=args.repeat,
            number=args.number,
        )

    for _, dataset in enumerate(noncsv_datasets):
        # if not dataset.path.name.endswith("noncsv_int_244761.txt"): continue
        benchmark_noncsv_dataset(
            dataset,
            repeat=args.repeat,
            number=args.number,
        )


if __name__ == "__main__":
    main()
