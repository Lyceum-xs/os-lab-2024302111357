#!/usr/bin/env python3
"""Derive link scripts and carry flat-image bounds from ELF into the kernel."""
import argparse
from pathlib import Path
import struct
import subprocess


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"unexpected course linker script: {old!r}")
    return text.replace(old, new, 1)


def link_script(kind, source, output):
    text = Path(source).read_text()
    if kind == "kernel":
        text = replace_once(text, "SECTIONS", "PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }\n\nSECTIONS")
        text = replace_once(text, "  }\n\n  .rodata", "  } :text\n\n  .rodata")
        text = replace_once(text, "  }\n\n  .data", "  } :data\n\n  .data")
        text = replace_once(text, "*(.text .text.*)", """*(.text .text.*)
    . = ALIGN(4096);
    *(trampsec)
    . = ALIGN(4096);
    kernel_text_end = .;""")
        text = replace_once(text, "kernel_end = .;", ". = ALIGN(4096);\n  kernel_end = .;")
    else:
        text = replace_once(text, "  .data :", "  . = ALIGN(4096);\n  user_writable = .;\n  .data :")
    text = replace_once(text, "*(.data .data.*)", "*(.data .data.*) *(.sdata .sdata.*)")
    text = replace_once(text, "*(.rodata .rodata.*)", "*(.rodata .rodata.*) *(.srodata .srodata.*)")
    Path(output).write_text(text)


def images(names, nm, output):
    rows, blobs = [], []
    for name in names:
        if not name.isidentifier() or len(name) >= 32:
            raise ValueError(f"invalid program name: {name}")
        binary = Path(f"user-flat/{name}.bin")
        elf = Path(f"user-flat/{name}.elf")
        header = elf.read_bytes()[:64]
        if header[:6] != b"\x7fELF\x02\x01":
            raise ValueError("expected little-endian ELF64")
        entry = struct.unpack_from("<Q", header, 24)[0]
        symbols = {}
        for line in subprocess.check_output([nm, "-P", str(elf)], text=True).splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[0] in ("user_writable", "end"):
                symbols[fields[0]] = int(fields[2], 16)
        writable, end = symbols["user_writable"], symbols["end"]
        if not (0 <= entry < writable <= end) or writable % 4096:
            raise ValueError(f"invalid segment boundaries: {name}")
        if binary.stat().st_size > end:
            raise ValueError(f"binary extends beyond memory size: {name}")
        rows.extend([f".quad _uprog_{name}_start, _uprog_{name}_end",
                     f".quad {entry}, {writable}, {end}",
                     f'.asciz "{name}"', f".zero {31 - len(name)}"])
        blobs.extend([".balign 8", f"_uprog_{name}_start:",
                      f'.incbin "{binary.as_posix()}"', f"_uprog_{name}_end:"])
    Path(output).write_text('\n'.join([
        '.section .rodata', '.balign 8', '.globl _uprog_table', '_uprog_table:',
        *rows, '.zero 72', *blobs, ''
    ]))


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    linker = commands.add_parser("linker")
    linker.add_argument("kind", choices=["kernel", "user"])
    linker.add_argument("source")
    linker.add_argument("output")
    image = commands.add_parser("images")
    image.add_argument("--nm", required=True)
    image.add_argument("--output", required=True)
    image.add_argument("names", nargs="+")
    args = parser.parse_args()
    if args.command == "linker":
        link_script(args.kind, args.source, args.output)
    else:
        images(args.names, args.nm, args.output)


if __name__ == "__main__":
    main()
