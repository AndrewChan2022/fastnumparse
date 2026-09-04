"""Extract the benchmark and C++ test datasets from ``assets/``."""

from __future__ import annotations

import argparse
import os
import shutil
import tempfile
from pathlib import Path

try:
    import py7zr
except ImportError as error:
    raise SystemExit(
        'py7zr is required; install it with: python -m pip install -e ".[dev]"'
    ) from error


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS_DIR = ROOT / "assets"
DEFAULT_DATA_DIR = ROOT / "data"

# This archive was named before its fourth column was included. Keep the
# tracked asset name for compatibility, but generate the filename consumed by
# the benchmarks and C++ test application.
OUTPUT_NAME_OVERRIDES = {
    "csv_int_2854577x3.7z": "csv_int_2854577x4.txt",
}


def display_path(path: Path) -> Path:
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def archive_member(archive_path: Path) -> str:
    with py7zr.SevenZipFile(archive_path, mode="r") as archive:
        members = archive.getnames()

    if len(members) != 1:
        raise ValueError(
            f"{archive_path.name} must contain exactly one file; found {members}"
        )

    member = members[0]
    member_path = Path(member)
    if (
        member_path.name != member
        or "/" in member
        or "\\" in member
        or member_path.suffix != ".txt"
    ):
        raise ValueError(
            f"{archive_path.name} contains an unsafe or unexpected member: {member}"
        )
    return member


def extract_archive(archive_path: Path, data_dir: Path, *, force: bool) -> Path:
    member = archive_member(archive_path)
    output_name = OUTPUT_NAME_OVERRIDES.get(archive_path.name, member)
    output_path = data_dir / output_name

    if output_path.exists() and not force:
        print(f"skip    {display_path(output_path)} (already exists)")
        return output_path

    data_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fastnumparse-gendata-") as temp_name:
        temp_dir = Path(temp_name)
        with py7zr.SevenZipFile(archive_path, mode="r") as archive:
            archive.extract(path=temp_dir, targets=[member])

        extracted_path = temp_dir / member
        if not extracted_path.is_file():
            raise RuntimeError(
                f"{archive_path.name} did not extract the expected file {member}"
            )

        temporary_output = data_dir / f".{output_name}.tmp"
        shutil.copyfile(extracted_path, temporary_output)
        os.replace(temporary_output, output_path)

    print(f"extract {archive_path.name} -> {display_path(output_path)}")
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract all assets/*.7z datasets into data/."
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=DEFAULT_ASSETS_DIR,
        help="directory containing .7z archives",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=DEFAULT_DATA_DIR,
        help="output directory",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="replace datasets that already exist",
    )
    args = parser.parse_args()

    assets_dir = args.assets_dir.resolve()
    data_dir = args.data_dir.resolve()
    archives = sorted(assets_dir.glob("*.7z"))
    if not archives:
        raise FileNotFoundError(f"no .7z archives found in {assets_dir}")

    for archive_path in archives:
        extract_archive(archive_path, data_dir, force=args.force)

    print(f"ready: {len(archives)} datasets in {data_dir}")


if __name__ == "__main__":
    main()
