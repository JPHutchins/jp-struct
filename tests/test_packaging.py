"""The legs that exist to test a built artifact must actually be testing one.

The repo root is sys.path[0] for a bare `python -m pytest`, and an in-place
build leaves salix.<abi>.so sitting there, so an installed distribution gets
shadowed by the working tree without anything failing. Only the legs that set
SALIX_REQUIRE_INSTALLED make the claim, so only they are held to it.
"""

import importlib.metadata
import os
import pathlib

import pytest

import salix

pytestmark = pytest.mark.skipif(
    os.environ.get("SALIX_REQUIRE_INSTALLED") != "1",
    reason="this leg imports the working tree on purpose",
)


def test_the_imported_module_is_the_file_the_distribution_installed():
    """Exact rather than path-shaped: --target installs nowhere near site-packages."""

    distribution = importlib.metadata.distribution("salix")
    installed = {
        pathlib.Path(str(distribution.locate_file(entry))).resolve()
        for entry in distribution.files or ()
    }

    assert pathlib.Path(salix.__file__).resolve() in installed


def test_the_import_did_not_come_from_the_working_tree():
    location = pathlib.Path(salix.__file__).resolve()

    assert not (location.parent / "setup.py").exists(), location
    assert not (location.parent / "build_config.py").exists(), location
