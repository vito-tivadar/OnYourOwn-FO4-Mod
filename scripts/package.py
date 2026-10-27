#!/usr/bin/env python3
"""Package the plugin DLL and its MCM files as a Data-relative ZIP for mod managers.

Usage: package.py <OnYourOwn.dll> <output.zip> [--data-root Data]
"""

import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

REQUIRED_EXPORTS = {"F4SEPlugin_Load", "F4SEPlugin_Version"}
MCM_FILES = ("config.json", "settings.ini")


def pe_exports(data: bytes) -> set[str]:
    """Return the exported function names of a Windows x64 DLL (raises if it is not one)."""
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE header")
    machine, section_count = struct.unpack_from("<HH", data, pe + 4)
    optional_size, characteristics = struct.unpack_from("<HH", data, pe + 20)
    if machine != 0x8664 or not characteristics & 0x2000:
        raise ValueError("expected a Windows x64 DLL")

    optional = pe + 24
    export_rva = struct.unpack_from("<I", data, optional + 112)[0]  # PE32+ data directory 0
    sections = [struct.unpack_from("<IIII", data, optional + optional_size + i * 40 + 8)
                for i in range(section_count)]  # virtual size, virtual address, raw size, raw offset

    def offset(rva: int) -> int:
        for virtual_size, address, raw_size, raw_offset in sections:
            if address <= rva < address + max(virtual_size, raw_size):
                return raw_offset + rva - address
        raise ValueError(f"RVA {rva:#x} is outside all sections")

    if export_rva == 0:
        return set()
    export_dir = offset(export_rva)
    name_count, = struct.unpack_from("<I", data, export_dir + 24)
    names_rva, = struct.unpack_from("<I", data, export_dir + 32)
    names = set()
    for i in range(name_count):
        start = offset(struct.unpack_from("<I", data, offset(names_rva) + i * 4)[0])
        names.add(data[start:data.index(b"\0", start)].decode("ascii"))
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dll", type=Path)
    parser.add_argument("zip", type=Path)
    parser.add_argument("--data-root", type=Path, default=Path("Data"), help="folder holding MCM/ (default: ./Data)")
    args = parser.parse_args()

    dll = args.dll.read_bytes()
    missing = REQUIRED_EXPORTS - pe_exports(dll)
    if missing:
        raise SystemExit(f"{args.dll} is missing F4SE exports: {sorted(missing)}")

    files = {"F4SE/Plugins/OnYourOwn.dll": dll}
    for name in MCM_FILES:
        path = args.data_root / "MCM/Config/OnYourOwn" / name
        if not path.is_file():
            raise SystemExit(f"missing MCM file: {path}")
        files[f"MCM/Config/OnYourOwn/{name}"] = path.read_bytes()
    json.loads(files["MCM/Config/OnYourOwn/config.json"])  # fail early on broken JSON

    # Fixed timestamps and permissions so the ZIP is reproducible.
    args.zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.zip, "w") as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data, compresslevel=9)

    with zipfile.ZipFile(args.zip) as archive:
        if archive.namelist() != sorted(files) or any(archive.read(n) != d for n, d in files.items()):
            raise SystemExit("ZIP verification failed")
    print(f"Created {args.zip} ({', '.join(sorted(files))}; DLL SHA-256 {hashlib.sha256(dll).hexdigest()})")


if __name__ == "__main__":
    main()
