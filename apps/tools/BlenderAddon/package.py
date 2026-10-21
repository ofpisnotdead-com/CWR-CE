#!/usr/bin/env python3
"""Package the addon with a native library; no Blender installation required."""

import argparse
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path, help="Native PoseidonFormats.dll/.so/.dylib")
    parser.add_argument("--output", required=True, type=Path, help="Addon ZIP to create")
    args = parser.parse_args()
    if not args.library.is_file() or args.library.name not in {
        "PoseidonFormats.dll", "PoseidonFormats.so", "PoseidonFormats.dylib"
    }:
        parser.error("Provide the built PoseidonFormats shared library for the target machine.")
    addon = Path(__file__).resolve().parent / "io_import_p3d"
    root = Path(__file__).resolve().parents[3]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for source in sorted(addon.rglob("*")):
            if source.is_file() and source.suffix in {".py", ".toml"} and "__pycache__" not in source.parts:
                archive.write(source, "io_import_p3d/" + source.relative_to(addon).as_posix())
        archive.write(args.library, "io_import_p3d/" + args.library.name)
        archive.write(root / "LICENSE", "io_import_p3d/LICENSE.txt")
        archive.write(root / "THIRD_PARTY_NOTICES.md", "io_import_p3d/THIRD_PARTY_NOTICES.md")
    print(args.output)


if __name__ == "__main__":
    main()
