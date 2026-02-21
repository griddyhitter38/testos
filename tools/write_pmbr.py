#!/usr/bin/env python3
import os
import struct
import sys

def main():
    if len(sys.argv) != 2:
        print("usage: write_pmbr.py <disk-image>", file=sys.stderr)
        return 1

    path = sys.argv[1]
    size = os.path.getsize(path)
    if size < 512:
        print("image too small", file=sys.stderr)
        return 1

    total_sectors = size // 512
    # Protective MBR partition: type 0xEE, start LBA 1, size = min(total-1, 0xFFFFFFFF)
    start_lba = 1
    max_size = 0xFFFFFFFF
    part_size = total_sectors - 1
    if part_size > max_size:
        part_size = max_size

    mbr = bytearray(512)
    # Partition entry at 0x1BE
    # status, chs_first(3), type, chs_last(3), lba_first(4), sectors(4)
    entry = struct.pack("<B3sB3sII",
                        0x00,
                        b"\x00\x02\x00",
                        0xEE,
                        b"\xff\xff\xff",
                        start_lba,
                        part_size)
    mbr[0x1BE:0x1BE + 16] = entry
    mbr[0x1FE:0x200] = b"\x55\xaa"

    with open(path, "r+b") as f:
        f.seek(0)
        f.write(mbr)

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
