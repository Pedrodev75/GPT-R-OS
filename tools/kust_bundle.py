"""Install the Kust distribution tree in a copy of a FAT16 disk."""

from pathlib import Path
import struct


DOCUMENTATION = Path(__file__).resolve().parent.parent / "docs" / "kust"
CHAPTERS = ("README", "START", "SYNTAX", "TYPES", "FLOW", "FUNCS", "API", "ERRORS", "LIMITS", "CLI")
EXAMPLES = ("HELLO", "VARIABLE", "MATH", "FLOW", "FUNCS", "FILES", "MEMORY", "GRAPHIC")
SECTOR, ENTRY_SIZE, EOC = 512, 32, 0xffff


class Fat16Bundle:
    def __init__(self, image):
        self.image = image
        self.free_from = 2
        if image[510:SECTOR] != b"\x55\xaa":
            raise ValueError("Missing MBR signature")
        for offset in range(446, 510, 16):
            if image[offset + 4] in (4, 6, 14):
                start, sectors = struct.unpack_from("<II", image, offset + 8)
                if start and sectors and (start + sectors) * SECTOR <= len(image):
                    break
        else:
            raise ValueError("Missing FAT16 partition")
        partition = start * SECTOR
        if image[partition + 510:partition + SECTOR] != b"\x55\xaa":
            raise ValueError("Missing FAT16 boot signature")
        sec, spc, reserved, self.copies, self.root_count, total16 = struct.unpack_from(
            "<HBHBHH", image, partition + 11)
        fat_sectors, = struct.unpack_from("<H", image, partition + 22)
        total32, = struct.unpack_from("<I", image, partition + 32)
        total = total16 or total32
        if (sec != SECTOR or not spc or spc & (spc - 1) or not reserved
                or not 1 <= self.copies <= 2 or not fat_sectors or not self.root_count
                or total > sectors):
            raise ValueError("Unsupported FAT16 geometry")
        self.cluster_bytes = spc * sec
        self.fat_start = partition + reserved * sec
        self.fat_bytes = fat_sectors * sec
        self.root = self.fat_start + self.copies * self.fat_bytes
        root_sectors = (self.root_count * ENTRY_SIZE + sec - 1) // sec
        self.data = self.root + root_sectors * sec
        count = (total - (self.data - partition) // sec) // spc
        if not 4085 <= count < 65525 or (count + 2) * 2 > self.fat_bytes:
            raise ValueError("Invalid FAT16 cluster count")
        self.stop = min(count + 2, 0xfff0)
        if self.copies == 2 and image[self.fat_start:self.root - self.fat_bytes] != image[
                self.root - self.fat_bytes:self.root]:
            raise ValueError("FAT16 copies differ")

    def fat(self, cluster):
        return struct.unpack_from("<H", self.image, self.fat_start + 2 * cluster)[0]

    def set_fat(self, cluster, value):
        for copy in range(self.copies):
            struct.pack_into("<H", self.image, self.fat_start + self.fat_bytes * copy + 2 * cluster, value)

    def cluster_offset(self, cluster):
        return self.data + (cluster - 2) * self.cluster_bytes

    def chain(self, start):
        seen = set()
        while 2 <= start < self.stop and start not in seen:
            seen.add(start)
            yield start
            start = self.fat(start)
        if start < 0xfff8:
            raise ValueError("Damaged FAT16 chain")

    def allocate(self, content):
        first = last = 0
        for start in range(0, len(content), self.cluster_bytes):
            for cluster in range(self.free_from, self.stop):
                if self.fat(cluster) == 0:
                    break
            else:
                raise ValueError("FAT16 disk full while bundling Kust")
            self.free_from = cluster + 1
            offset = self.cluster_offset(cluster)
            self.image[offset:offset + self.cluster_bytes] = content[
                start:start + self.cluster_bytes].ljust(self.cluster_bytes, b"\0")
            self.set_fat(cluster, EOC)
            if last:
                self.set_fat(last, cluster)
            else:
                first = cluster
            last = cluster
        return first

    @staticmethod
    def entry(alias, attribute, cluster, size=0):
        return struct.pack("<11sB14xHI", alias, attribute, cluster, size)

    def offsets(self, clusters):
        if clusters:
            for cluster in clusters:
                start = self.cluster_offset(cluster)
                yield from range(start, start + self.cluster_bytes, ENTRY_SIZE)
        else:
            yield from range(self.root, self.root + self.root_count * ENTRY_SIZE, ENTRY_SIZE)

    def slot(self, directory, alias):
        clusters = list(self.chain(directory)) if directory else []
        entries = iter(self.offsets(clusters))
        free, end = None, False
        for offset in entries:
            first = self.image[offset]
            if first == 0:
                if free is None:
                    free, end = offset, True
                break
            if first == 0xe5:
                if free is None:
                    free = offset
            elif self.image[offset:offset + 11] == alias:
                return offset, True
        if free is not None:
            if end:
                following = next(entries, None)
                if following is not None:
                    self.image[following] = 0
            return free, False
        if not directory:
            raise ValueError("FAT16 root directory full")
        cluster = self.allocate(bytes(self.cluster_bytes))
        self.set_fat(clusters[-1], cluster)
        return self.cluster_offset(cluster), False

    def install(self, directory, name, content=None):
        stem, _, ext = name.partition(".")
        alias = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
        offset, exists = self.slot(directory, alias)
        is_dir = content is None
        if exists:
            attr = self.image[offset + 11]
            cluster, size = struct.unpack_from("<HI", self.image, offset + 26)
            if (attr & 0x18) != (0x10 if is_dir else 0) or self.image[offset + 20:offset + 22] != b"\0\0":
                raise ValueError(f"Conflicting bundle entry {name}")
            if is_dir:
                location = self.cluster_offset(cluster)
                dot = self.entry(b".          ", 0x10, cluster)
                dotdot = self.entry(b"..         ", 0x10, directory)
                for at, wanted in ((location, dot), (location + ENTRY_SIZE, dotdot)):
                    actual = self.image[at:at + ENTRY_SIZE]
                    if (not 2 <= cluster < self.stop or actual[:12] != wanted[:12]
                            or actual[26:28] != wanted[26:28]):
                        raise ValueError(f"Damaged directory {name}")
            else:
                blocks = list(self.chain(cluster)) if cluster else []
                current = b"".join(self.image[at:at + self.cluster_bytes]
                                   for at in map(self.cluster_offset, blocks))
                if (size != len(content) or current[:size] != content
                        or len(blocks) != (size + self.cluster_bytes - 1) // self.cluster_bytes):
                    raise ValueError(f"Different {name} already exists. Use an unbundled base disk.")
            return cluster
        if is_dir:
            cluster = self.allocate(bytes(self.cluster_bytes))
            start = self.cluster_offset(cluster)
            self.image[start:start + 2 * ENTRY_SIZE] = (self.entry(b".          ", 0x10, cluster) +
                                                        self.entry(b"..         ", 0x10, directory))
        else:
            cluster = self.allocate(content)
        self.image[offset:offset + ENTRY_SIZE] = self.entry(
            alias, 0x10 if is_dir else 0x20, cluster, 0 if is_dir else len(content))
        return cluster


def install_bundle(image, app_path):
    app = app_path.read_bytes()
    if len(app) < 64:
        raise ValueError("KUST.APP has an incomplete GTRA header")
    magic, version, header, code_at, code, data_at, data, bss, entry = struct.unpack_from("<4sHH6I", app)
    if ((magic, version, header, code_at) != (b"GTRA", 0x0101, 64, 64)
            or any(app[32:64]) or entry >= code or data_at != 64 + code
            or code + data + bss > 65536 or len(app) != data_at + data):
        raise ValueError("KUST.APP is not a valid GTRA v1.1 executable")
    fs = Fat16Bundle(image)
    system = fs.install(0, "SYS64")
    fs.install(system, "KUST.APP", app)
    docs = fs.install(system, "DOCS")
    kust = fs.install(docs, "KUST")
    examples = fs.install(kust, "EXAMPLES")
    for chapter in CHAPTERS:
        name = chapter + ".TXT"
        fs.install(kust, name, (DOCUMENTATION / name).read_bytes())
    for example in EXAMPLES:
        name = example + ".KST"
        fs.install(examples, name, (DOCUMENTATION / "EXAMPLES" / name).read_bytes())
