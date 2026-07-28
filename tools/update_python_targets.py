"""Regenerate nix/python-targets.nix from a python-build-standalone release.

Interpreters come from .python-version; the GitHub API serves each asset's
digest, so nothing is downloaded to pin it.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import urllib.request
from pathlib import Path
from typing import Iterator, NamedTuple

RELEASES = "https://api.github.com/repos/astral-sh/python-build-standalone/releases"
ASSET = re.compile(
    r"^cpython-(?P<version>\d+\.\d+(?:\.\w+)?)\+\d+-(?P<triple>.+)-install_only_stripped\.tar\.gz$"
)


class Platform(NamedTuple):
    triple: str
    zig_target: str
    platform_tag: str
    module_name: str
    extra_flags: tuple[str, ...]
    link_python_library: bool


PLATFORMS: dict[str, Platform] = {
    "manylinux-x86_64": Platform(
        "x86_64-unknown-linux-gnu",
        "x86_64-linux-gnu.2.17",
        "manylinux_2_17_x86_64",
        "jpstruct.cpython-{nodot}-x86_64-linux-gnu.so",
        (),
        False,
    ),
    "manylinux-aarch64": Platform(
        "aarch64-unknown-linux-gnu",
        "aarch64-linux-gnu.2.17",
        "manylinux_2_17_aarch64",
        "jpstruct.cpython-{nodot}-aarch64-linux-gnu.so",
        (),
        False,
    ),
    "macos-x86_64": Platform(
        "x86_64-apple-darwin",
        "x86_64-macos.10.13",
        "macosx_10_13_x86_64",
        "jpstruct.cpython-{nodot}-darwin.so",
        ("-undefined", "dynamic_lookup"),
        False,
    ),
    "macos-aarch64": Platform(
        "aarch64-apple-darwin",
        "aarch64-macos.11.0",
        "macosx_11_0_arm64",
        "jpstruct.cpython-{nodot}-darwin.so",
        ("-undefined", "dynamic_lookup"),
        False,
    ),
    "windows-x86_64": Platform(
        "x86_64-pc-windows-msvc",
        "x86_64-windows-gnu",
        "win_amd64",
        "jpstruct.pyd",
        (),
        True,
    ),
    "windows-aarch64": Platform(
        "aarch64-pc-windows-msvc",
        "aarch64-windows-gnu",
        "win_arm64",
        "jpstruct.pyd",
        (),
        True,
    ),
}


def fetch(url: str) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": "jp-struct-update-targets"})

    with urllib.request.urlopen(request) as response:
        return json.load(response)


def to_sri(digest: str) -> str:
    return subprocess.run(
        ["nix", "hash", "convert", "--to", "sri", "--hash-algo", "sha256", digest],
        capture_output=True, text=True, check=True,
    ).stdout.strip()


def minor_of(version: str) -> str:
    return ".".join(version.split(".")[:2])


def nodot_of(minor: str) -> str:
    return minor.replace(".", "")


def indent(depth: int) -> str:
    return "  " * depth


def render_platform(name: str, platform: Platform) -> Iterator[str]:
    flags = "".join(f'\n{indent(4)}"{flag}"' for flag in platform.extra_flags)

    yield f"{indent(2)}{name} = {{"
    yield f'{indent(3)}pbsTriple = "{platform.triple}";'
    yield f'{indent(3)}zigTarget = "{platform.zig_target}";'
    yield f'{indent(3)}platformTag = "{platform.platform_tag}";'
    yield f'{indent(3)}moduleName = "{platform.module_name}";'
    yield f"{indent(3)}extraFlags = [{flags + chr(10) + indent(3) if flags else ''}];"
    yield f"{indent(3)}linkPythonLibrary = {str(platform.link_python_library).lower()};"
    yield f"{indent(2)}}};"


def render_python(minor: str, version: str, hashes: dict[str, str]) -> Iterator[str]:
    yield f'{indent(2)}"{minor}" = {{'
    yield f'{indent(3)}version = "{version}";'
    yield f'{indent(3)}tag = "cp{nodot_of(minor)}";'
    yield f"{indent(3)}hashes = {{"

    for name, digest in hashes.items():
        yield f'{indent(4)}{name} = "{digest}";'

    yield f"{indent(3)}}};"
    yield f"{indent(2)}}};"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", default=None)
    parser.add_argument("--python-version", type=Path, default=Path(".python-version"))
    parser.add_argument("--output", type=Path, default=Path("nix/python-targets.nix"))
    arguments = parser.parse_args()

    release = (
        fetch(f"{RELEASES}/tags/{arguments.release}")
        if arguments.release
        else fetch(f"{RELEASES}/latest")
    )
    wanted = arguments.python_version.read_text().split()

    assets = {}
    for asset in release["assets"]:
        matched = ASSET.match(asset["name"])

        if matched is not None:
            assets[(minor_of(matched["version"]), matched["triple"])] = (
                matched["version"],
                asset["digest"].removeprefix("sha256:"),
            )

    available = {
        minor: {
            name: platform
            for name, platform in PLATFORMS.items()
            if (minor, platform.triple) in assets
        }
        for minor in wanted
    }

    for minor, platforms in available.items():
        if not platforms:
            raise SystemExit(f"release {release['tag_name']} has no assets at all for {minor}")

        for name in PLATFORMS.keys() - platforms.keys():
            print(f"skipping {minor}/{name}: not published in {release['tag_name']}")

    lines = [
        "# Generated by tools/update_python_targets.py -- do not edit by hand.",
        "#",
        "# The interpreter set is .python-version; flake.nix asserts the two agree.",
        "# Headers come from python-build-standalone because a cross build needs the",
        "# target's pyconfig.h, and nixpkgs has no Windows or Darwin CPython.",
        "{",
        f'{indent(1)}release = "{release["tag_name"]}";',
        "",
        f"{indent(1)}platforms = {{",
    ]

    for name, platform in PLATFORMS.items():
        lines.extend(render_platform(name, platform))

    lines.extend([f"{indent(1)}}};", "", f"{indent(1)}pythons = {{"])

    for minor, platforms in available.items():
        version = assets[(minor, next(iter(platforms.values())).triple)][0]
        hashes = {
            name: to_sri(assets[(minor, platform.triple)][1])
            for name, platform in platforms.items()
        }
        lines.extend(render_python(minor, version, hashes))

    lines.extend([f"{indent(1)}}};", "}", ""])
    arguments.output.write_text("\n".join(lines))

    wheels = sum(len(platforms) for platforms in available.values())
    print(f"{arguments.output}: {len(wanted)} interpreters, {wheels} wheels")


if __name__ == "__main__":
    main()
