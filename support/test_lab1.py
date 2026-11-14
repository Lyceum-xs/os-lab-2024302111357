#!/usr/bin/env python3
"""Host-side tests of the real kernel formatter and personal Banner protocol."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def valid_banner(data):
    plain = b"oslab sid=2024302111357 mod97=0x32\n"
    return data == b"".join(bytes((c, ord('.'))) for c in plain)


def main():
    with tempfile.TemporaryDirectory(prefix="oslab1-format-") as directory:
        obj = str(Path(directory) / "printf.o")
        exe = str(Path(directory) / "printf-test")
        env = dict(os.environ, DEVELOPER_DIR="/Library/Developer/CommandLineTools")
        flags = ["clang", "-g", "-O1", "-fno-builtin", "-fsanitize=undefined"]
        subprocess.run(flags + ["-Dprintf=kprintf", "-Dvprintf=kvprintf", "-Dpanic=kpanic", "-c",
                               str(ROOT / "kernel/printf.c"), "-o", obj], check=True, env=env)
        subprocess.run(flags + [str(ROOT / "tests/printf_test.c"), obj, "-o", exe],
                       check=True, env=env)
        subprocess.run([exe], check=True, timeout=20)
    data = (ROOT / "expect_banner.txt").read_bytes()
    assert valid_banner(data)
    for bad in (data[:-1], data.replace(b"s.", b"s", 1), data + b"\n",
                data.replace(b"x.", b"X.", 1), data.replace(b"3.2.", b"0.3.2.", 1)):
        assert not valid_banner(bad), repr(bad)
    print("BANNER PASS exact bytes and five malformed protocol-1 boundaries")


if __name__ == "__main__":
    main()
