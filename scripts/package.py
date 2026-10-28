#!/usr/bin/env python3
"""Package the plugin DLL and its MCM files as a Data-relative ZIP for mod managers.

Usage: package.py <OnYourOwn.dll> <output dir> [--data-root Data] [--old-gen]
Writes <output dir>/OnYourOwn-<version>.zip (or -OldGen.zip), with the version taken from xmake.lua.
"""

import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

REQUIRED_EXPORTS = {"F4SEPlugin_Load", "F4SEPlugin_Version"}
OLD_GEN_EXPORTS = {"F4SEPlugin_Query"}  # F4SE 0.6 loads plugins through Query
NEXT_GEN_RUNTIME, OLD_GEN_RUNTIME = "1.11.240", "1.10.163"  # F4SEPlugin_Version.compatibleVersions[0]
MCM_FILES = ("config.json", "settings.ini")
XMAKE_LUA = Path(__file__).resolve().parent.parent / "xmake.lua"


def project_version() -> str:
    match = re.search(r'^set_version\("([^"]+)"\)', XMAKE_LUA.read_text(), re.MULTILINE)
    if not match:
        raise SystemExit(f"no set_version(...) in {XMAKE_LUA}")
    return match.group(1)


def pe_exports(data: bytes) -> dict[str, int]:
    """Return the exports (name -> file offset) of a Windows x64 DLL (raises if it is not one)."""
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
        return {}
    export_dir = offset(export_rva)
    name_count, functions_rva, names_rva, ordinals_rva = struct.unpack_from("<IIII", data, export_dir + 24)
    exports = {}
    for i in range(name_count):
        start = offset(struct.unpack_from("<I", data, offset(names_rva) + i * 4)[0])
        ordinal, = struct.unpack_from("<H", data, offset(ordinals_rva) + i * 2)
        function_rva, = struct.unpack_from("<I", data, offset(functions_rva) + ordinal * 4)
        exports[data[start:data.index(b"\0", start)].decode("ascii")] = offset(function_rva)
    return exports


def declared_runtime(data: bytes, version_data: int) -> str:
    """The first game version in F4SEPlugin_Version.compatibleVersions (F4SE::PluginVersionData + 0x210)."""
    packed, = struct.unpack_from("<I", data, version_data + 0x210)
    return f"{packed >> 24}.{packed >> 16 & 0xFF}.{packed >> 4 & 0xFFF}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dll", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--data-root", type=Path, default=Path("Data"), help="folder holding MCM/ (default: ./Data)")
    parser.add_argument("--old-gen", action="store_true", help="DLL built with --oldgen=y (game 1.10.163)")
    args = parser.parse_args()

    dll = args.dll.read_bytes()
    exports = pe_exports(dll)
    missing = REQUIRED_EXPORTS - exports.keys()
    if args.old_gen:
        missing |= OLD_GEN_EXPORTS - exports.keys()
    elif OLD_GEN_EXPORTS & exports.keys():
        raise SystemExit(f"{args.dll} is an old-gen build; package it with --old-gen")
    if missing:
        raise SystemExit(f"{args.dll} is missing F4SE exports: {sorted(missing)}")
    runtime = declared_runtime(dll, exports["F4SEPlugin_Version"])
    if runtime != (OLD_GEN_RUNTIME if args.old_gen else NEXT_GEN_RUNTIME):
        raise SystemExit(f"{args.dll} declares game {runtime}, mod managers would reject it")

    files = {"F4SE/Plugins/OnYourOwn.dll": dll}
    for name in MCM_FILES:
        path = args.data_root / "MCM/Config/OnYourOwn" / name
        if not path.is_file():
            raise SystemExit(f"missing MCM file: {path}")
        files[f"MCM/Config/OnYourOwn/{name}"] = path.read_bytes()
    json.loads(files["MCM/Config/OnYourOwn/config.json"])  # fail early on broken JSON

    # Fixed timestamps and permissions so the ZIP is reproducible.
    zip_path = args.out_dir / f"OnYourOwn-{project_version()}{'-OldGen' if args.old_gen else ''}.zip"
    args.out_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w") as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data, compresslevel=9)

    with zipfile.ZipFile(zip_path) as archive:
        if archive.namelist() != sorted(files) or any(archive.read(n) != d for n, d in files.items()):
            raise SystemExit("ZIP verification failed")
    print(f"Created {zip_path} ({', '.join(sorted(files))}; DLL SHA-256 {hashlib.sha256(dll).hexdigest()})")


if __name__ == "__main__":
    main()
