import fastnumparse


def test_native_extension_is_importable() -> None:
    assert fastnumparse._native_version() == fastnumparse.__version__
