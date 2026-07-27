"""Pack a built extension module into a PEP 427 wheel.

The wheel is byte-for-byte reproducible: entries are sorted and every timestamp
is pinned to the zip epoch, so the same inputs always hash the same.

    python tools/pack_wheel.py --extension record.pyd \
        --extension-name record.cp314-win_amd64.pyd \
        --python-tag cp314 --abi-tag cp314 --platform-tag win_amd64 \
        --outdir dist
"""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import io
import tomllib
import zipfile
from pathlib import Path
from typing import Iterator, NamedTuple

ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)


class Metadata(NamedTuple):
    name: str
    version: str
    summary: str
    requires_python: str
    authors: tuple[str, ...]
    readme: str
    readme_content_type: str

    @property
    def normalized_name(self) -> str:
        return self.name.replace("-", "_").replace(".", "_").lower()

    @property
    def dist_info(self) -> str:
        return f"{self.normalized_name}-{self.version}.dist-info"

    def core_metadata(self) -> str:
        return "\n".join(
            (
                "Metadata-Version: 2.1",
                f"Name: {self.name}",
                f"Version: {self.version}",
                f"Summary: {self.summary}",
                *(f"Author-email: {author}" for author in self.authors),
                f"Requires-Python: {self.requires_python}",
                f"Description-Content-Type: {self.readme_content_type}",
                "",
                self.readme,
            )
        )


class Entry(NamedTuple):
    path: str
    data: bytes

    def record_row(self) -> tuple[str, str, int]:
        digest = base64.urlsafe_b64encode(hashlib.sha256(self.data).digest()).rstrip(b"=")
        return (self.path, f"sha256={digest.decode()}", len(self.data))


def read_metadata(pyproject: Path) -> Metadata:
    project = tomllib.loads(pyproject.read_text())["project"]
    readme = pyproject.parent / project["readme"]

    return Metadata(
        name=project["name"],
        version=project["version"],
        summary=project["description"],
        requires_python=project["requires-python"],
        authors=tuple(author["email"] for author in project.get("authors", []) if "email" in author),
        readme=readme.read_text(),
        readme_content_type="text/markdown" if readme.suffix == ".md" else "text/plain",
    )


def wheel_metadata(tag: str) -> str:
    return "\n".join(
        (
            "Wheel-Version: 1.0",
            "Generator: jp-struct pack_wheel",
            "Root-Is-Purelib: false",
            f"Tag: {tag}",
            "",
        )
    )


def render_record(entries: tuple[Entry, ...], record_path: str) -> bytes:
    buffer = io.StringIO(newline="")
    writer = csv.writer(buffer, lineterminator="\n")
    writer.writerows(entry.record_row() for entry in entries)
    writer.writerow((record_path, "", ""))

    return buffer.getvalue().encode()


def build_entries(metadata: Metadata, tag: str, module: Entry) -> Iterator[Entry]:
    yield module
    yield Entry(f"{metadata.dist_info}/METADATA", metadata.core_metadata().encode())
    yield Entry(f"{metadata.dist_info}/WHEEL", wheel_metadata(tag).encode())


def write_wheel(destination: Path, entries: tuple[Entry, ...], record_path: str) -> None:
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for entry in (*entries, Entry(record_path, render_record(entries, record_path))):
            info = zipfile.ZipInfo(entry.path, date_time=ZIP_EPOCH)
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, entry.data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pyproject", type=Path, default=Path("pyproject.toml"))
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--extension-name", required=True)
    parser.add_argument("--python-tag", required=True)
    parser.add_argument("--abi-tag", required=True)
    parser.add_argument("--platform-tag", required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    arguments = parser.parse_args()

    metadata = read_metadata(arguments.pyproject)
    tag = f"{arguments.python_tag}-{arguments.abi_tag}-{arguments.platform_tag}"
    entries = tuple(
        sorted(
            build_entries(
                metadata, tag, Entry(arguments.extension_name, arguments.extension.read_bytes())
            ),
            key=lambda entry: entry.path,
        )
    )

    arguments.outdir.mkdir(parents=True, exist_ok=True)
    destination = arguments.outdir / f"{metadata.normalized_name}-{metadata.version}-{tag}.whl"
    write_wheel(destination, entries, f"{metadata.dist_info}/RECORD")

    print(destination)


if __name__ == "__main__":
    main()
