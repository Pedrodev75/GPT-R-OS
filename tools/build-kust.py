"""Bootstrap the native Kust compiler with Zig and package it as GTRA v1.1."""

import argparse
import os
from pathlib import Path
import struct
import subprocess


ROOT = Path(__file__).resolve().parent.parent
CODE_BASE, DATA_BASE = 0x80000000, 0x80011000
IMAGE_LIMIT = 65536
HEADER = struct.Struct("<4sHH6I32x")


def package(elf):
    # Only the linked, freestanding x86-64 ELF is consumed here.
    if elf[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", elf, 16) != (2, 62):
        raise ValueError("Expected a linked x86-64 ELF")
    entry, _, offset = struct.unpack_from("<QQQ", elf, 24)
    width, count, names_index = struct.unpack_from("<HHH", elf, 58)
    if width != 64 or names_index >= count or offset + width * count > len(elf):
        raise ValueError("Invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", elf, offset + i * width) for i in range(count)]
    names_at, names_size = sections[names_index][4:6]
    names = elf[names_at:names_at + names_size]
    selected = {}
    for name_at, kind, flags, address, start, size, *_ in sections:
        end = names.find(b"\0", name_at)
        name = names[name_at:end]
        if size and flags & 2:
            if name not in (b".text", b".data", b".bss") or name in selected:
                raise ValueError(f"Unexpected allocated section {name!r}")
            if kind != 8 and start + size > len(elf):
                raise ValueError("Truncated ELF section")
            selected[name] = (address, size, b"" if kind == 8 else elf[start:start + size])
    code_address, code_size, code = selected[b".text"]
    data_address, data_size, data = selected.get(b".data", (DATA_BASE, 0, b""))
    bss_address, bss_size, bss = selected.get(b".bss", (DATA_BASE + data_size, 0, b""))
    data_end = DATA_BASE + data_size
    if (code_address != CODE_BASE or data_address != DATA_BASE or bss_address < data_end
            or len(code) != code_size or len(data) != data_size or bss
            or not CODE_BASE <= entry < CODE_BASE + code_size):
        raise ValueError("ELF sections do not fit the GTRA virtual layout")
    bss_size += bss_address - data_end
    if code_size + data_size + bss_size > IMAGE_LIMIT:
        raise ValueError(f"KUST exceeds the 64 KiB image limit ({code_size}+{data_size}+{bss_size})")
    header = HEADER.pack(b"GTRA", 0x0101, HEADER.size, HEADER.size, code_size,
                         HEADER.size + code_size, data_size, bss_size, entry - CODE_BASE)
    return header + code + data, (code_size, data_size, bss_size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default="zig", help="Zig executable (default: zig on PATH)")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "kust" / "KUST.APP")
    args = parser.parse_args()
    output = args.output.resolve()
    work = output.parent
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for kind in ("LOCAL", "GLOBAL"):
        env.setdefault(f"ZIG_{kind}_CACHE_DIR", str(work / f"zig-{kind.lower()}"))
    temp = work / "tmp"
    temp.mkdir(exist_ok=True)
    env.update(TEMP=str(temp), TMP=str(temp), TMPDIR=str(temp))

    def zig(*command):
        subprocess.run([args.zig, *command], cwd=ROOT, env=env, check=True)

    objects = []
    for source in ("entry.S", "main.c", "compiler.c"):
        obj = work / (source + ".o")
        warnings = ["-std=gnu11", "-Wall", "-Wextra", "-Werror"] if source.endswith(".c") else []
        zig("cc", "-target", "x86_64-freestanding-none", *warnings, "-Oz",
            "-ffreestanding", "-fpie", "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-mgeneral-regs-only",
            "-mno-red-zone", "-c", ROOT / "apps/kust" / source, "-o", obj)
        objects.append(obj)
    elf = work / "kust.elf"
    zig("ld.lld", "-m", "elf_x86_64", "-T", ROOT / "apps/kust/layout.ld", "-o", elf, *objects)
    try:
        app, (code, data, bss) = package(elf.read_bytes())
        output.write_bytes(app)
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))
    print(f"{output} ({len(app)} bytes). Code {code}, data {data}, BSS {bss}, image {code + data + bss}.")


if __name__ == "__main__":
    main()
