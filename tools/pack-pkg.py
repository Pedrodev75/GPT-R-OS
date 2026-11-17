"""Build a GTRP v1 package from files with FAT16 short names."""

import argparse
from pathlib import Path
import re
import struct
import zlib


SHORT = re.compile(r"[A-Za-z0-9_-]{1,8}(?:\.[A-Za-z0-9_-]{1,3})?\Z")


def canonical_name(name):
    if not SHORT.fullmatch(name) or name.upper() == "PKG.DAT":
        raise ValueError(f"invalid 8.3 name {name!r}")
    if any(part != part.lower() and part != part.upper() for part in name.split(".")):
        raise ValueError(f"use one case per 8.3 name component for {name!r}")
    return name.upper()


def validate_app(app):
    if len(app) < 8:
        raise ValueError("invalid principal GTRA size")
    magic, version, header = struct.unpack_from("<4sHH", app)
    if magic == b"GTRA" and (version, header) == (1, 32) and 32 < len(app) <= 4128:
        size, entry = struct.unpack_from("<II", app, 8)
        if size == len(app) - header and entry < size and not any(app[16:32]):
            return
    if magic == b"GTRA" and (version, header) == (0x0101, 64) and 64 < len(app) <= 65600:
        code, code_size, data, data_size, bss, entry = struct.unpack_from("<6I", app, 8)
        if (code == header and data == code + code_size and data + data_size == len(app)
                and code_size + data_size + bss <= 65536 and entry < code_size and not any(app[32:64])):
            return
    raise ValueError("invalid principal GTRA header or size")


def make_package(package_id, name, version, description, main, files):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,8}", package_id):
        raise ValueError("invalid package ID")
    fields = {"id": package_id.upper(), "name": name, "version": version}
    if description:
        fields["description"] = description
    for key, value in fields.items():
        limit = {"id": 8, "name": 80, "version": 24, "description": 256}[key]
        encoded = value.encode("utf-8")
        if not encoded or len(encoded) > limit or any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in value):
            raise ValueError(f"invalid manifest field {key}")
    manifest = "".join(f"{key}={value}\n" for key, value in fields.items()).encode("utf-8")
    if not 1 <= len(files) <= 16:
        raise ValueError("expected between 1 and 16 package files")
    names = [canonical_name(file.name) for file in files]
    if len(names) != len(set(names)):
        raise ValueError("duplicate 8.3 name")
    principal = canonical_name(main)
    if not principal.endswith(".APP") or principal not in names:
        raise ValueError("principal must be an included .APP")
    remaining = 256 * 1024 - 16 - len(manifest) - 20 * len(files)
    payload = []
    for file in files:
        with file.open("rb") as source:
            data = source.read(remaining + 1)
        if len(data) > remaining:
            raise ValueError("package exceeds 256 KiB")
        payload.append(data)
        remaining -= len(data)
    entry = names.index(principal)
    validate_app(payload[entry])
    table = b"".join(
        struct.pack("<12sII", file.name.encode("ascii"), len(data), zlib.crc32(data))
        for file, data in zip(files, payload)
    )
    header = struct.pack("<4sHHIHH", b"GTRP", 1, 16, len(manifest), len(files), entry)
    return header + manifest + table + b"".join(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--description", default="")
    parser.add_argument("--main", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("files", type=Path, nargs="+")
    args = parser.parse_args()
    if any(file.resolve() == args.output.resolve() for file in args.files):
        parser.error("output must differ from every source file")
    try:
        image = make_package(args.id, args.name, args.version, args.description, args.main, args.files)
        args.output.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
