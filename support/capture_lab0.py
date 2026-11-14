#!/usr/bin/env python3
"""Capture echo hi and representative traps from the supplied xv6 tree."""
from pathlib import Path
import os
import re
import selectors
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
reference = ROOT.parent / "xv6-riscv"
with tempfile.TemporaryDirectory(prefix="lab0-evidence-") as temporary:
    traplog = Path(temporary) / "int.log"
    q = subprocess.Popen([
        "qemu-system-riscv64", "-machine", "virt", "-bios", "none", "-kernel",
        "kernel/kernel", "-m", "128M", "-smp", "1", "-nographic", "-snapshot",
        "-global", "virtio-mmio.force-legacy=false", "-drive",
        "file=fs.img,if=none,format=raw,id=x0", "-device",
        "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0", "-d", "int", "-D", str(traplog)
    ], cwd=reference, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    poll = selectors.DefaultSelector()
    poll.register(q.stdout, selectors.EVENT_READ)
    data = bytearray()
    sent = False
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            for key, _ in poll.select(0.1):
                data.extend(os.read(key.fd, 65536))
            if not sent and b"$ " in data:
                q.stdin.write(b"echo hi\n")
                q.stdin.flush()
                sent = True
            if re.search(rb"echo hi\r?\nhi\r?\n\$ ", data):
                time.sleep(0.15)
                break
        else:
            raise AssertionError(data.decode(errors="replace"))
    finally:
        q.stdin.write(b"\x01x")
        q.stdin.flush()
        q.communicate(timeout=5)
        poll.close()
    excerpts = []
    with traplog.open() as source:
        for line in source:
            if any(token in line for token in ("cause:0000000000000008", "cause:0000000000000009", "cause:0000000000000005")):
                cause = re.search(r"cause:[0-9a-f]+", line).group()
                if not any(cause in previous for previous in excerpts):
                    excerpts.append(line.strip())
    assert len(excerpts) == 3, excerpts
    evidence = "2026-11-14 / supplied xv6 / QEMU virt -smp 1 -snapshot\n"
    evidence += data.decode(errors="replace").rstrip() + "\nRepresentative hardware traps:\n"
    evidence += "\n".join(excerpts) + "\n"
    (ROOT / "docs/lab0/evidence.txt").write_text(evidence)
    print(evidence)
