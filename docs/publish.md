# Building wheels and publishing to TestPyPI

This project uses scikit-build-core to invoke CMake and cibuildwheel to build,
repair, and test platform wheels. The GitHub Actions workflow in
`.github/workflows/testpypi.yml` builds the complete distribution set and
publishes it to TestPyPI.

Publishing is intentionally manual: the workflow runs only when started with
GitHub's **Run workflow** button. TestPyPI rejects an upload when a file with
the same project version and filename already exists.

## Supported wheel targets

The cibuildwheel settings are stored in `pyproject.toml`. The initial release
builds 64-bit CPython 3.10 through 3.13 wheels for:

- Linux manylinux x86-64
- Windows AMD64
- macOS Intel x86-64
- macOS Apple Silicon ARM64

musllinux wheels are skipped. Python 3.14 wheels should be enabled after the
vendored pybind11 is upgraded from 2.13.6 to a version that officially supports
Python 3.14 and the project tests pass with that interpreter.

## Versioning

For Python package builds, `pyproject.toml` is the package version source used
by scikit-build-core. `CMakeLists.txt` contains the fallback version used by a
standalone CMake build. Keep both values identical. They are currently:

```text
0.0.1
```

TestPyPI and PyPI do not permit replacing an existing distribution file. Bump
the version before running the workflow again, for example from `0.0.1` to
`0.0.2` or `0.0.2.dev1`.

## Build a wheel locally

Install the development tools:

```powershell
python -m pip install -e ".[dev]"
```

Build and test one wheel matching the current Windows/Python installation:

```powershell
python -m cibuildwheel --only cp313-win_amd64 --output-dir wheelhouse .
```

Replace `cp313-win_amd64` with an identifier printed by:

```powershell
python -m cibuildwheel --print-build-identifiers --platform windows
```

Build every configured Windows wheel:

```powershell
python -m cibuildwheel --platform windows --output-dir wheelhouse .
```

Build every configured Linux wheel:
```bash
python -m cibuildwheel --platform linux --output-dir wheelhouse .
```

cibuildwheel builds only wheels. Build the source distribution separately:

```powershell
python -m build --sdist --outdir wheelhouse
python -m twine check wheelhouse/*
```

A local Windows machine builds Windows wheels only. Use the GitHub Actions
workflow for Linux and macOS wheels.

## Configure TestPyPI Trusted Publishing

https://test.pypi.org/manage/account/publishing/

https://pypi.org/manage/account/publishing/

TestPyPI has accounts and project settings separate from production PyPI.
Create or sign in to the TestPyPI account, then configure a pending or existing
GitHub Trusted Publisher with these exact values:

| Setting | Value |
| --- | --- |
| PyPI project name | `fastnumparse` |
| GitHub owner | `AndrewChan2022` |
| GitHub repository | `fastnumparse` |
| Workflow filename | `testpypi.yml` |
| Environment name | `testpypi` |

The workflow requests only the short-lived `id-token: write` permission in the
publishing job. No TestPyPI API token needs to be stored in GitHub secrets.

## Build and publish with GitHub Actions

1. Commit and push `pyproject.toml`, the source code, and
   `.github/workflows/testpypi.yml` to GitHub.
2. Open the repository's **Actions** tab.
3. Select **Build and publish to TestPyPI**.
4. Select **Run workflow**.
5. Wait for all wheel and source-distribution jobs to pass.
6. The final job downloads those artifacts and publishes them to TestPyPI.

The workflow creates one artifact per operating-system runner and one sdist
artifact. The final publishing job merges them into `dist/` before upload.

## Install the TestPyPI package

Use TestPyPI for `fastnumparse` and regular PyPI as the fallback for NumPy and
other dependencies:

```powershell
python -m pip install `
  --index-url https://test.pypi.org/simple/ `
  --extra-index-url https://pypi.org/simple/ `
  fastnumparse==0.0.1
```

Verify the Python metadata and compiled extension use the same version:

```powershell
python -c "import fastnumparse as fnp; print(fnp.__version__, fnp._native_version())"
```

## Optional local upload

Trusted Publishing through GitHub Actions is preferred. To upload manually,
create a TestPyPI API token and set Twine's credentials only for the current
PowerShell session:

```powershell
$env:TWINE_USERNAME = "__token__"
$env:TWINE_PASSWORD = "pypi-your-testpypi-token"
python -m twine upload --repository testpypi wheelhouse/*
```

Do not commit the token or store it in this repository.

## Release checks

Before starting the publishing workflow:

- Confirm the versions in `pyproject.toml` and `CMakeLists.txt` match.
- Confirm internal timing output is disabled for normal Python API calls.
- Run `python -m pytest` locally.
- Ensure the intended version has not already been uploaded to TestPyPI.

References:

- [cibuildwheel CI configuration](https://cibuildwheel.pypa.io/en/stable/ci-services/)
- [cibuildwheel delivery guidance](https://cibuildwheel.pypa.io/en/latest/deliver-to-pypi/)
- [Using TestPyPI](https://packaging.python.org/en/latest/guides/using-testpypi/)
- [PyPI Trusted Publishing](https://docs.pypi.org/trusted-publishers/using-a-publisher/)
