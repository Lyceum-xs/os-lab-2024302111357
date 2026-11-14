#!/usr/bin/env python3
"""COW acceptance, exact copy counts, argv ABI and page conservation."""
import argparse
from pathlib import Path
import re
import tempfile
from test_lab3 import Qemu


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel-only", action="store_true")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    logs = args.log_dir or Path(tempfile.mkdtemp(prefix="oslab5-tests-"))
    logs.mkdir(parents=True, exist_ok=True)
    print(f"Logs: {logs}", flush=True)
    q = Qemu(logs, "cow")
    try:
        q.boot(timeout=180 if args.kernel_only else 30)
        initial = q.free_count()
        if args.kernel_only:
            assert b"COWKTEST kill-semantics=1 all PASS" in q.data
            q.command("echo hi", r"T5-3 PASS: argc=2 argv\[0\]=echo aligned")
            layout = re.search(rb"EXECSTACK argc=2 sp=(0x[0-9a-f]+) argv0=(0x[0-9a-f]+) argv1=(0x[0-9a-f]+) sentinel=0x0", q.data)
            assert layout and int(layout.group(1), 16) % 16 == 0
            print(layout.group().decode(), flush=True)
            sp, arg0, arg1 = (int(layout.group(i), 16) for i in (1, 2, 3))
            words = re.findall(rb"EXECWORD va=(0x[0-9a-f]+) data=(0x[0-9a-f]+)", q.data)
            assert [int(addr, 16) for addr, _ in words] == list(range(sp, sp + 64, 8))
            stack = b"".join(int(value, 16).to_bytes(8, "little") for _, value in words)
            assert int.from_bytes(stack[:8], "little") == arg0
            assert int.from_bytes(stack[8:16], "little") == arg1
            assert stack[16:32] == bytes(16)
            assert stack[arg0 - sp:arg0 - sp + 5] == b"echo\0"
            assert stack[arg1 - sp:arg1 - sp + 3] == b"hi\0"
            print("PASS actual argv pointers, sentinel, padding and strings", flush=True)
        else:
            q.command("zerocopy", "T5-1 PASS.*")
            delta = re.search(rb"T5-1 copies: \d+ -> \d+ \(delta (\d+)\)", q.data)
            assert delta and int(delta.group(1)) <= 2
            q.command("cowpair", "T5-2 PASS.*")
            assert b"T5-2 copies delta = 2 (expect 2)" in q.data
            assert b"T5-2 WARN" not in q.data
            q.command("echo hi", r"T5-3 PASS: argc=2 argv\[0\]=echo aligned")
            assert b"\nhi\nT5-3 PASS" in q.data
            q.command("echo", r"T5-3 PASS: argc=1 argv\[0\]=echo aligned")
            q.command("echo alpha beta", r"T5-3 PASS: argc=3 argv\[0\]=echo aligned")
            start = len(q.data)
            q.command("cowtest", "COWTEST all PASS shared=0", 120)
            run = q.data[start:]
            assert run.count(b"reached LAB5_COW_BUDGET=253") == 1
            for label in (b"alternating writes", b"concurrent children", b"readonly protection",
                          b"budget downgrade", b"exec arguments and rejection"):
                assert b"COWTEST " + label + b" PASS" in run
            q.command("bench", "PASS: bench", 60)
            q.command("hi", r"hi.*pid.*[0-9]+")
            print(f"PASS exact copies: zerocopy={int(delta.group(1))}, cowpair=2", flush=True)
        final = q.free_count()
        assert final == initial, f"page leak {initial} -> {final}"
        print(f"PASS page conservation free={final}", flush=True)
    finally:
        q.close()
    print("LAB5 all PASS", flush=True)


if __name__ == "__main__":
    main()
