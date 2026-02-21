#!/usr/bin/env python3
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: bin2c.py <input.bin> <output.c>", file=sys.stderr)
        return 2

    in_path = sys.argv[1]
    out_path = sys.argv[2]

    with open(in_path, "rb") as f:
        data = f.read()

    with open(out_path, "w", encoding="utf-8") as out:
        out.write("// Auto-generated. Do not edit.\n")
        out.write("#include <stdint.h>\n\n")
        out.write("const unsigned char kernel_blob[] = {\n")
        for i, b in enumerate(data):
            if i % 12 == 0:
                out.write("  ")
            out.write(f"0x{b:02x}, ")
            if i % 12 == 11:
                out.write("\n")
        if len(data) % 12 != 0:
            out.write("\n")
        out.write("};\n\n")
        out.write(f"const unsigned int kernel_blob_len = {len(data)};\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
