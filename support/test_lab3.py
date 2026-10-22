#!/usr/bin/env python3
"""Run VM acceptance tests against QEMU, retaining the serial and trap logs."""
import argparse
from pathlib import Path
import os
import re
import selectors
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


class Qemu:
    def __init__(self, logs, name):
        self.data = bytearray()
        self.cursor = 0
        self.log = (logs / f"{name}.serial.log").open("wb")
        self.proc = subprocess.Popen([
            "qemu-system-riscv64", "-machine", "virt", "-bios", "none",
            "-kernel", "kernel/kernel", "-m", "128M", "-smp", "1", "-nographic",
            "-d", "int", "-D", str(logs / f"{name}.int.log")
        ], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)

    def drain(self, timeout=0.1):
        for key, _ in self.selector.select(timeout):
            chunk = os.read(key.fd, 65536)
            if not chunk:
                raise AssertionError(f"QEMU exited: {self.data[-2000:].decode(errors='replace')}")
            self.data.extend(chunk)
            self.log.write(chunk)
            self.log.flush()

    def expect(self, pattern, timeout=30):
        regex = re.compile(pattern.encode(), re.M)
        deadline = time.monotonic() + timeout
        while True:
            if b"panic:" in self.data or b" FAIL" in self.data:
                raise AssertionError(self.data[-3000:].decode(errors="replace"))
            match = regex.search(self.data, self.cursor)
            if match:
                self.cursor = match.end()
                return match
            if time.monotonic() >= deadline:
                raise AssertionError(f"timeout: {pattern}\n{self.data[-3000:].decode(errors='replace')}")
            self.drain()

    def send(self, value):
        self.proc.stdin.write(value.encode() if isinstance(value, str) else value)
        self.proc.stdin.flush()

    def command(self, command, pattern, timeout=30):
        self.send(command + "\n")
        match = self.expect(pattern + r"\r?\n", timeout)
        self.expect("sh> ")
        print(f"PASS {command}: {match.group().decode()}", flush=True)
        return match

    def boot(self, timeout=30):
        self.expect("sh> ", timeout)
        expected = (ROOT / "expect_banner.txt").read_bytes()
        assert bytes(self.data).startswith(expected), "lab1 Banner byte mismatch"

    def free_count(self):
        self.send(b"\x10")
        count = int(self.expect(r"free=(\d+)\r?\n").group(1))
        self.expect("DUMP-PAGETABLE end")
        return count

    def close(self):
        try:
            self.send(b"\x01x")
            self.proc.wait(timeout=3)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self.proc.kill()
            self.proc.wait()
        finally:
            rest = self.proc.stdout.read()
            if rest:
                self.data.extend(rest)
                self.log.write(rest)
            self.proc.stdin.close()
            self.proc.stdout.close()
            self.selector.close()
            self.log.close()


def check_dump(data):
    text = data.decode(errors="replace")
    block = text.split("DUMP-PAGETABLE begin\n", 1)[1].split("DUMP-PAGETABLE end", 1)[0]
    entries = []
    for line in block.splitlines():
        match = re.fullmatch(r"(L2|L1|LEAF) va=(0x[0-9a-f]+) pa=(0x[0-9a-f]+) perm=([r-][w-][x-][u-])", line)
        assert match, f"bad dump row: {line}"
        kind, va, pa, perm = match.groups()
        assert int(va, 16) % 4096 == 0 and int(pa, 16) % 4096 == 0
        if kind != "LEAF":
            assert perm == "----"
        else:
            entries.append((int(va, 16), perm))
    assert entries == sorted(entries)
    leaves = dict(entries)
    assert leaves[0] == "r-xu", "code permissions"
    assert leaves[(1 << 38) - 4096] == "r-x-", "trampoline permissions"
    assert leaves[(1 << 38) - 8192] == "rw--", "trapframe permissions"
    user_pages = sorted(va for va, perm in entries if perm.endswith("u"))
    gaps = [(right - left) // 4096 - 1 for left, right in zip(user_pages, user_pages[1:])]
    assert 2 in gaps, "two guard pages must be absent"
    print("PASS dump ABI, segment permissions and two guard pages", flush=True)


def main():
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--kernel-only", action="store_true")
    modes.add_argument("--dump-only", action="store_true")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    logs = args.log_dir or Path(tempfile.mkdtemp(prefix="oslab3-tests-"))
    logs.mkdir(parents=True, exist_ok=True)
    print(f"Logs: {logs}", flush=True)
    qemu = Qemu(logs, "kernel" if args.kernel_only else "vm")
    try:
        qemu.boot(timeout=180 if args.kernel_only else 30)
        print("PASS boot and lab1 Banner bytes", flush=True)
        if args.kernel_only:
            assert b"KTEST all PASS" in qemu.data
            print("PASS allocator, mapping and growth rollback kernel tests", flush=True)
            return
        check_dump(qemu.data)
        initial_free = qemu.free_count()
        print(f"PASS Ctrl-P dump replay (free={initial_free})", flush=True)
        if args.dump_only:
            print(bytes(qemu.data).decode(errors="replace"))
            return
        qemu.command("hi", r"hi.*pid.*[0-9]+")
        qemu.command("badecall", "TEST-1 PASS.*")
        qemu.send("badaccess\n")
        qemu.expect("TEST-3a PASS.*")
        qemu.expect("TEST-3b PASS.*")
        qemu.expect("sh> ")
        print("PASS badaccess TEST-3a and TEST-3b", flush=True)
        qemu.command("vmguard", "VMGUARD PASS.*")
        qemu.send("vmcopy\n")
        qemu.expect("VMCOPY input-ready")
        qemu.send("abcd\n")
        qemu.expect("VMCOPY PASS.*")
        qemu.expect("sh> ")
        print("PASS cross-page write/read/exec/wait and invalid pointers", flush=True)
        first = qemu.command("memcap", r"TEST-4 PASS: OOM returned error after (\d+) chunks.*", 180)
        second = qemu.command("memcap", r"TEST-4 PASS: OOM returned error after (\d+) chunks.*", 180)
        assert first.group(1) == second.group(1), "memcap leaks pages across runs"
        qemu.command("vmstress", "VMSTRESS PASS.*", 240)
        final_free = qemu.free_count()
        assert initial_free == final_free, f"leaked pages: {initial_free} -> {final_free}"
        print(f"PASS page conservation after fork/exec/exit/OOM (free={final_free})", flush=True)
    finally:
        qemu.close()

    qemu = Qemu(logs, "concurrent")
    try:
        qemu.boot()
        qemu.send("vmconcurrent\n")
        qemu.expect("VMCONCURRENT shell and spin")
        qemu.expect("sh> ")
        qemu.expect(r"spin [0-9]+")
        qemu.command("hi", r"hi.*pid.*[0-9]+")
        qemu.expect(r"spin [0-9]+")
        qemu.command("badecall", "TEST-1 PASS.*")
        qemu.expect(r"spin [0-9]+")
        print("PASS timer preemption: spin keeps running while sh reads and executes", flush=True)
    finally:
        qemu.close()
    print("LAB3 all PASS", flush=True)


if __name__ == "__main__":
    main()
