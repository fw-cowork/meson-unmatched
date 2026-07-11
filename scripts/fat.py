import math
import struct
from pathlib import Path


SECTOR_SIZE = 512
FAT16_EOC = 0xFFFF


def _dos_date():
    return ((2026 - 1980) << 9) | (7 << 5) | 7


def _dos_time():
    return (17 << 11) | (0 << 5) | 0


def _short_name(name):
    mapping = {
        "Image.gz": b"IMAGE   GZ ",
        "hifive-unmatched-a00.dtb": b"HIFIVE~1DTB",
        "extlinux": b"EXTLINUX   ",
        "extlinux.conf": b"EXTLIN~1CON",
    }
    if name in mapping:
        return mapping[name]
    stem, dot, ext = name.partition(".")
    stem = "".join(ch for ch in stem.upper() if ch.isalnum() or ch in "_~")[:8]
    ext = "".join(ch for ch in ext.upper() if ch.isalnum() or ch in "_~")[:3]
    return stem.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def _lfn_checksum(short):
    total = 0
    for byte in short:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def _lfn_entry(order, chunk, checksum):
    values = [ord(ch) for ch in chunk]
    if len(values) < 13:
        values.append(0)
    while len(values) < 13:
        values.append(0xFFFF)

    entry = bytearray(32)
    entry[0] = order
    entry[11] = 0x0F
    entry[12] = 0
    entry[13] = checksum
    struct.pack_into("<5H", entry, 1, *values[0:5])
    struct.pack_into("<6H", entry, 14, *values[5:11])
    struct.pack_into("<H", entry, 26, 0)
    struct.pack_into("<2H", entry, 28, *values[11:13])
    return bytes(entry)


def _lfn_entries(long_name, short):
    if long_name.upper() == long_name and len(long_name.partition(".")[0]) <= 8:
        return []
    chunks = [long_name[index : index + 13] for index in range(0, len(long_name), 13)]
    checksum = _lfn_checksum(short)
    entries = []
    for index in range(len(chunks) - 1, -1, -1):
        order = index + 1
        if index == len(chunks) - 1:
            order |= 0x40
        entries.append(_lfn_entry(order, chunks[index], checksum))
    return entries


def _dir_entry(short, *, attr, cluster=0, size=0):
    high = (cluster >> 16) & 0xFFFF
    low = cluster & 0xFFFF
    return struct.pack(
        "<11sBBBHHHHHHHI",
        short,
        attr,
        0,
        0,
        _dos_time(),
        _dos_date(),
        _dos_date(),
        high,
        _dos_time(),
        _dos_date(),
        low,
        size,
    )


def _entries_for(name, *, attr, cluster=0, size=0):
    short = _short_name(name)
    return b"".join(_lfn_entries(name, short)) + _dir_entry(
        short,
        attr=attr,
        cluster=cluster,
        size=size,
    )


def _cluster_chain(start, count):
    return range(start, start + count)


def _layout(total_sectors, sectors_per_cluster):
    reserved = 1
    fats = 2
    root_entries = 512
    root_dir_sectors = math.ceil(root_entries * 32 / SECTOR_SIZE)
    fat_sectors = 1
    while True:
        data_sectors = total_sectors - reserved - fats * fat_sectors - root_dir_sectors
        clusters = data_sectors // sectors_per_cluster
        required_fat_sectors = math.ceil((clusters + 2) * 2 / SECTOR_SIZE)
        if required_fat_sectors == fat_sectors:
            break
        fat_sectors = required_fat_sectors
    if clusters >= 65525:
        raise ValueError("FAT16 image is too large for the selected cluster size")
    return {
        "reserved": reserved,
        "fats": fats,
        "root_entries": root_entries,
        "root_dir_sectors": root_dir_sectors,
        "fat_sectors": fat_sectors,
        "data_start_sector": reserved + fats * fat_sectors + root_dir_sectors,
        "clusters": clusters,
    }


def _boot_sector(total_sectors, layout, sectors_per_cluster, hidden_sectors, label):
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = sectors_per_cluster
    struct.pack_into("<H", boot, 14, layout["reserved"])
    boot[16] = layout["fats"]
    struct.pack_into("<H", boot, 17, layout["root_entries"])
    struct.pack_into("<H", boot, 19, total_sectors if total_sectors < 65536 else 0)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, layout["fat_sectors"])
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, hidden_sectors)
    struct.pack_into("<I", boot, 32, total_sectors if total_sectors >= 65536 else 0)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x554E4D54)
    boot[43:54] = label.upper().encode("ascii")[:11].ljust(11, b" ")
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xaa"
    return bytes(boot)


def write_fat16_image(path, *, size_bytes, files, hidden_sectors=0, label="BOOT"):
    path = Path(path)
    total_sectors = size_bytes // SECTOR_SIZE
    sectors_per_cluster = 8
    cluster_size = sectors_per_cluster * SECTOR_SIZE
    layout = _layout(total_sectors, sectors_per_cluster)

    file_data = {name: Path(source).read_bytes() for name, source in files.items()}
    clusters = {}
    next_cluster = 2
    for name, data in file_data.items():
        count = max(1, math.ceil(len(data) / cluster_size))
        clusters[name] = (next_cluster, count)
        next_cluster += count
    if "extlinux.conf" in file_data:
        clusters["extlinux"] = (next_cluster, 1)
        next_cluster += 1
    if next_cluster - 2 > layout["clusters"]:
        raise ValueError("FAT16 image is too small for boot files")

    fat = [0] * (layout["clusters"] + 2)
    fat[0] = 0xFFF8
    fat[1] = FAT16_EOC
    for _name, (start, count) in clusters.items():
        chain = list(_cluster_chain(start, count))
        for current, following in zip(chain, chain[1:]):
            fat[current] = following
        fat[chain[-1]] = FAT16_EOC

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as image:
        image.truncate(size_bytes)
        image.seek(0)
        image.write(_boot_sector(total_sectors, layout, sectors_per_cluster, hidden_sectors, label))

        fat_bytes = bytearray(layout["fat_sectors"] * SECTOR_SIZE)
        for index, value in enumerate(fat):
            if index * 2 + 2 <= len(fat_bytes):
                struct.pack_into("<H", fat_bytes, index * 2, value)
        for fat_index in range(layout["fats"]):
            sector = layout["reserved"] + fat_index * layout["fat_sectors"]
            image.seek(sector * SECTOR_SIZE)
            image.write(fat_bytes)

        root = bytearray(layout["root_dir_sectors"] * SECTOR_SIZE)
        cursor = 0
        label_entry = _dir_entry(label.upper().encode("ascii")[:11].ljust(11, b" "), attr=0x08)
        root[cursor : cursor + len(label_entry)] = label_entry
        cursor += len(label_entry)
        for name in file_data:
            if name == "extlinux.conf":
                continue
            start, _count = clusters[name]
            entry = _entries_for(name, attr=0x20, cluster=start, size=len(file_data[name]))
            root[cursor : cursor + len(entry)] = entry
            cursor += len(entry)
        if "extlinux.conf" in file_data:
            start, _count = clusters["extlinux"]
            entry = _entries_for("extlinux", attr=0x10, cluster=start, size=0)
            root[cursor : cursor + len(entry)] = entry
        root_sector = layout["reserved"] + layout["fats"] * layout["fat_sectors"]
        image.seek(root_sector * SECTOR_SIZE)
        image.write(root)

        data_start = layout["data_start_sector"] * SECTOR_SIZE
        for name, data in file_data.items():
            start, _count = clusters[name]
            image.seek(data_start + (start - 2) * cluster_size)
            image.write(data)

        if "extlinux.conf" in file_data:
            extlinux_start, _count = clusters["extlinux"]
            extlinux = bytearray(cluster_size)
            dot = _dir_entry(b".          ", attr=0x10, cluster=extlinux_start, size=0)
            dotdot = _dir_entry(b"..         ", attr=0x10, cluster=0, size=0)
            conf_start, _count = clusters["extlinux.conf"]
            conf = _entries_for(
                "extlinux.conf",
                attr=0x20,
                cluster=conf_start,
                size=len(file_data["extlinux.conf"]),
            )
            extlinux[0 : len(dot)] = dot
            extlinux[len(dot) : len(dot) + len(dotdot)] = dotdot
            extlinux[len(dot) + len(dotdot) : len(dot) + len(dotdot) + len(conf)] = conf
            image.seek(data_start + (extlinux_start - 2) * cluster_size)
            image.write(extlinux)
