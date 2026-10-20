#!/usr/bin/env python3
"""Reject arm64 executables that approach the dyld shared-cache region."""
from pathlib import Path
import struct
import sys


def verify_mapping(executable):
    with Path(executable).open("rb") as file:
        header = file.read(32)
        if len(header) != 32:
            raise ValueError("Missing Mach-O header")
        magic, cpu, _, _, count, size, _, _ = struct.unpack("<8I", header)
        if magic != 0xFEEDFACF or size > 1024 * 1024 or count > size // 8:
            raise ValueError("Unsupported Mach-O executable")
        commands = file.read(size)
    if len(commands) != size:
        raise ValueError("Truncated Mach-O load commands")
    segments = []
    offset = 0
    for _ in range(count):
        if offset + 8 > size:
            raise ValueError("Truncated Mach-O command")
        command, length = struct.unpack_from("<2I", commands, offset)
        if length < 8 or offset + length > size:
            raise ValueError("Invalid Mach-O command size")
        if command == 0x19:
            if length < 72:
                raise ValueError("Truncated Mach-O segment")
            name = commands[offset + 8:offset + 24].rstrip(b"\0")
            address, extent = struct.unpack_from("<2Q", commands, offset + 24)
            if name != b"__PAGEZERO" and extent:
                segments.append((address, address + extent))
        offset += length
    if not segments or offset != size:
        raise ValueError("Invalid Mach-O segment layout")
    span = max(end for _, end in segments) - min(start for start, _ in segments)
    if cpu == 0x100000C and span > 1536 * 1024 * 1024:
        raise ValueError(f"arm64 mapped image is {span // (1024 * 1024)} MiB; "
                         "keep it below 1536 MiB to leave room for dyld and ASLR")
    return span


if __name__ == "__main__":
    try:
        print(f"Mach-O mapped image: {verify_mapping(sys.argv[1]) // (1024 * 1024)} MiB")
    except (ValueError, OSError, IndexError) as error:
        raise SystemExit(str(error))
