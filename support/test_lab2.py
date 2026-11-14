#!/usr/bin/env python3
"""Personal UART boundary cases, separate from the supplied course tests."""
from pathlib import Path
import tempfile
import time
from test_lab3 import Qemu

q = Qemu(Path(tempfile.mkdtemp(prefix="oslab2-edge-")), "ioedge")
try:
    q.boot()
    q.send("ioedge\n")
    q.expect("IOEDGE empty-ready")
    deadline = time.monotonic() + 0.15
    while time.monotonic() < deadline:
        q.drain(0.01)
    assert b"IOEDGE blocked-read PASS" not in q.data
    q.send("Q")
    q.expect("IOEDGE blocked-read PASS")
    for round in range(3):
        q.expect(f"IOEDGE capacity-ready {round}")
        payload = bytes(ord('a') + (i + round) % 26 for i in range(64))
        for start in range(0, 64, 8):
            q.send(payload[start:start + 8])
            q.drain(0.02)
    q.expect("IOEDGE PASS.*")
    q.expect("sh> ")
    q.command("badecall", "TEST-1 PASS.*")
finally:
    q.close()
print("LAB2 personal edge tests PASS")
