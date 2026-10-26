#!/usr/bin/env python3
"""Run lab4 mechanism, scheduling and lab3 regression checks in QEMU."""
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
                raise AssertionError(self.data[-3000:].decode(errors="replace"))
            self.data.extend(chunk)
            self.log.write(chunk)
            self.log.flush()

    def expect(self, pattern, timeout=30):
        regex = re.compile(pattern.encode(), re.M)
        deadline = time.monotonic() + timeout
        while True:
            match = regex.search(self.data, self.cursor)
            if match:
                self.cursor = match.end()
                return match
            if b"panic:" in self.data or b"FAIL:" in self.data:
                raise AssertionError(self.data[-5000:].decode(errors="replace"))
            if time.monotonic() >= deadline:
                raise AssertionError(f"timeout {pattern}\n{self.data[-3000:].decode(errors='replace')}")
            self.drain()

    def command(self, command, pattern, timeout=30):
        self.send(command + "\n")
        result = self.expect(pattern + r"\r?\n", timeout)
        self.expect("sh> ", timeout)
        print(f"PASS {command}: {result.group().decode().strip()}", flush=True)
        return result

    def send(self, value):
        self.proc.stdin.write(value.encode() if isinstance(value, str) else value)
        self.proc.stdin.flush()

    def boot(self):
        self.expect("sh> ", 30)
        expected = (ROOT / "expect_banner.txt").read_bytes()
        assert bytes(self.data).startswith(expected), "Banner mismatch"

    def free_count(self):
        self.send(b"\x10")
        count = int(self.expect(r"free=(\d+)\r?\n").group(1))
        self.expect("DUMP-PAGETABLE end")
        return count

    def close(self):
        try:
            self.proc.stdin.write(b"\x01x")
            self.proc.stdin.flush()
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


def run(args):
    logs = args.log_dir or Path(tempfile.mkdtemp(prefix="oslab4-tests-"))
    logs.mkdir(parents=True, exist_ok=True)
    print(f"Logs: {logs}", flush=True)
    q = Qemu(logs, "lab4")
    try:
        q.boot()
        print("PASS boot and Banner", flush=True)
        if args.kernel_only:
            for marker in (b"SCHEDTEST named dispatches=100:200:400 PASS",
                           b"SCHEDTEST overrides inheritance wakeup normalization PASS",
                           b"SCHEDTEST all PASS"):
                assert marker in q.data, f"missing kernel check: {marker!r}"
            print("PASS named defaults, overrides, inheritance and pass normalization", flush=True)
        else:
            initial_free = q.free_count()
            if not args.policy_only and not args.sleep_only:
                q.command("bench", "PASS: bench", 60)
                q.send("alt\n")
                sequence = q.expect(r"([AB]{32})\r?\nalt done", 30).group(1)
                assert sequence.count(b"A") == sequence.count(b"B") == 16
                assert sum(a != b for a, b in zip(sequence, sequence[1:])) >= 2, \
                    f"no timer preemption: {sequence!r}"
                q.expect("sh> ", 30)
                print(f"PASS timer preemption: {sequence.decode()}", flush=True)
            if not args.policy_only:
                q.command("schededge", "PASS: schededge", 40)
            if not args.sleep_only:
                q.command("stride_test", "PASS: stride", 120)
                q.command("stride_cases", "PASS: stride_cases", 120)
            if not args.policy_only and not args.sleep_only:
                q.command("hi", r"hi.*pid.*[0-9]+", 30)
                q.command("badecall", r"TEST-1 PASS.*", 30)
                q.send("badaccess\n")
                q.expect("TEST-3a PASS", 30)
                q.expect("TEST-3b PASS", 30)
                q.expect("sh> ", 30)
                print("PASS lab3 badaccess regression", flush=True)
            final_free = q.free_count()
            assert initial_free == final_free, f"leaked pages: {initial_free} -> {final_free}"
            print(f"PASS page conservation (free={final_free})", flush=True)
        for result in re.findall(rb"^RESULT [^\r\n]+", q.data, re.M):
            print(result.decode(), flush=True)
    finally:
        q.close()
    print("LAB4 all PASS", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--policy-only", action="store_true")
    modes.add_argument("--sleep-only", action="store_true")
    modes.add_argument("--kernel-only", action="store_true")
    parser.add_argument("--log-dir", type=Path)
    run(parser.parse_args())
