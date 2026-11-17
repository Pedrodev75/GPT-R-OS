"""Place the BIOS bootstrap and separate kernel payload into a disk image."""

import argparse
from pathlib import Path
import struct
from kust_bundle import install_bundle


SECTOR = 512
LOADER_SECTORS = 16
HEADER_SECTOR = 1 + LOADER_SECTORS
KERNEL_LIMIT = 0x60000


def pack(boot, loader, kernel, disk):
    if len(disk) % SECTOR or len(disk) < 1440 * 1024:
        raise ValueError("The destination needs a sector-aligned disk image")
    if len(boot) != SECTOR or boot[-2:] != b"\x55\xaa":
        raise ValueError("Invalid BIOS boot sector")
    if not 0 < len(loader) <= LOADER_SECTORS * SECTOR or not 0 < len(kernel) <= KERNEL_LIMIT:
        raise ValueError("Bootstrap or kernel exceeds the boot layout")
    start = HEADER_SECTOR * SECTOR
    end = start + SECTOR + (len(kernel) + SECTOR - 1) // SECTOR * SECTOR
    for offset in range(446, 510, 16):
        entry_type = disk[offset + 4]
        if entry_type:
            first = struct.unpack_from("<I", disk, offset + 8)[0]
            if first * SECTOR < end:
                raise ValueError("A partition overlaps the boot payload")
    image = bytearray(disk)
    image[:446] = boot[:446]
    image[510:SECTOR] = boot[510:SECTOR]
    image[SECTOR:start] = loader.ljust(start - SECTOR, b"\0")
    image[start:start + SECTOR] = struct.pack("<4sI", b"GTRK", len(kernel)).ljust(SECTOR, b"\0")
    image[start + SECTOR:end] = kernel.ljust(end - start - SECTOR, b"\0")
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("boot", "loader", "kernel", "disk", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    bundle = parser.add_mutually_exclusive_group()
    bundle.add_argument("--kust-app", type=Path,
                        default=Path(__file__).resolve().parent.parent / "build" / "kust" / "KUST.APP",
                        help="native compiler to install (default: build/kust/KUST.APP)")
    bundle.add_argument("--without-kust", action="store_true", help="pack the kernel only")
    args = parser.parse_args()
    if args.output.resolve() == args.disk.resolve():
        parser.error("Use a separate output path for the new disk image")
    try:
        image = pack(*(path.read_bytes() for path in (args.boot, args.loader, args.kernel, args.disk)))
        if not args.without_kust:
            if not args.kust_app.is_file():
                raise ValueError("KUST.APP is missing. Run tools/build-kust.py or pass --kust-app.")
            install_bundle(image, args.kust_app)
        args.output.write_bytes(image)
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
