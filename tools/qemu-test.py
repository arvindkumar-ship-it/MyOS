#!/usr/bin/env python3
"""Headless QEMU test runner for MyOS.

For each test:
  1. rebuild myos.iso with kernel command line "test=<name>" (boot menu timeout 0)
  2. boot it in QEMU without a window, serial on a pipe, monitor on a unix socket
  3. run the test's steps: wait for text on serial, type keys, send serial input
  4. pass if every step succeeded before the timeout

Usage:
  python3 tools/qemu-test.py            run all tests
  python3 tools/qemu-test.py div0 pf    run some tests
  python3 tools/qemu-test.py --list     list test names

Logs: build/test-logs/<name>.serial.txt and <name>.qemu.log
"""

import os
import re
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_DIR = os.path.join(ROOT, "build", "test-logs")

# ---------------------------------------------------------------------------
# Test definitions.
# Steps:
#   ("expect", "text")       wait until this exact text appears on serial
#   ("expect_re", r"regex")  wait until the regex matches
#   ("keys", "text")         type text on the PS/2 keyboard (QEMU sendkey)
#   ("serial", "text")       send text to the serial port (COM1 input)
#   ("sleep", seconds)
# "forbid": regexes that must never appear on serial.
# "exit": expected QEMU exit status (3 = ktest_finish(true), 5 = panic/fail), or None.
# ---------------------------------------------------------------------------

PANIC_RE = r"KERNEL PANIC"

TESTS = [
    # Stage 1: boot, screen + serial.
    dict(name="boot", steps=[("expect", "Hello from MyOS"), ("expect", "TEST PASS: boot")],
         forbid=[PANIC_RE], exit=3),

    # Stage 2: kprintf + string helpers.
    dict(name="kprintf", steps=[
        ("expect", "string selftest: ok"),
        ("expect", "deadbeef\r\n"),
        ("expect", "[-42] [0] [4294967295] [A] [str] [(null)] [%] [BEEF]"),
        ("expect", "[  -42] [-0042] [00001234] [ffffffff80000000] "
                   "[-9223372036854775808] [0x0000000000001000]"),
        ("expect", "TEST PASS: kprintf"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 2: panic prints file:line and message, then stops.
    dict(name="panic", steps=[
        ("expect_re", r"\*\*\* KERNEL PANIC at kernel/tests/test_basic\.c:\d+ \*\*\*"),
        ("expect", "test 42"),
        ("expect", "System halted."),
    ], exit=5),

    # Stage 3: our GDT and TSS are loaded.
    dict(name="gdt", steps=[("expect", "GDT + TSS loaded"), ("expect", "cs=8 ss=10 ds=10 tr=28"),
                            ("expect", "TEST PASS: gdt")], forbid=[PANIC_RE], exit=3),

    # Stage 3 Done test: 1/0 -> "Divide error at RIP=...", no reboot.
    dict(name="div0", steps=[
        ("expect", "IDT loaded"),
        ("expect", "dividing by zero..."),
        ("expect_re", r"Divide error at RIP=0xffffffff8[0-9a-f]{7}"),
        ("expect", "unhandled CPU exception 0 (Divide error) in kernel mode"),
    ], forbid=[r"not reached", r"MyOS booting\.\.\.[\s\S]*MyOS booting"], exit=5),

    # Breakpoint returns, and iretq restores every register.
    dict(name="int3", steps=[
        ("expect_re", r"Breakpoint at RIP=0xffffffff8[0-9a-f]{7}"),
        ("expect", "back from int3, registers preserved"),
        ("expect", "TEST PASS: int3"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="gp", steps=[
        ("expect", "General protection fault at RIP="),
        ("expect", "selector error: GDT index 16 (selector 80)"),
        ("expect", "unhandled CPU exception 13"),
    ], forbid=[r"TEST PASS"], exit=5),

    dict(name="pf", steps=[
        ("expect", "Page fault at RIP="),
        ("expect", "not-present page on read, kernel mode, address CR2=0x00000deadbeef000"),
        ("expect", "unhandled CPU exception 14"),
    ], forbid=[r"not reached"], exit=5),

    dict(name="df", steps=[
        ("expect", "Double fault"),
        ("expect", "handler running on IST1 stack: yes"),
        ("expect", "unhandled CPU exception 8"),
    ], forbid=[r"not reached"], exit=5),

    dict(name="ud", steps=[
        ("expect", "Invalid opcode at RIP="),
        ("expect", "unhandled CPU exception 6"),
    ], forbid=[r"TEST PASS"], exit=5),
]

# ---------------------------------------------------------------------------

# QEMU "sendkey" names for characters that are not plain letters/digits.
KEY_NAMES = {
    " ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", ",": "comma",
    "/": "slash", ";": "semicolon", "'": "apostrophe", "[": "bracket_left",
    "]": "bracket_right", "\\": "backslash", "\b": "backspace", "\t": "tab",
    "`": "grave_accent",
}
SHIFT_KEYS = {
    "_": "minus", "+": "equal", ">": "dot", "<": "comma", "?": "slash", ":": "semicolon",
    '"': "apostrophe", "{": "bracket_left", "}": "bracket_right", "|": "backslash",
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8",
    "(": "9", ")": "0", "~": "grave_accent",
}


def key_for(ch):
    if ch.isalpha() and ch.isascii():
        return ("shift-" + ch.lower()) if ch.isupper() else ch
    if ch.isdigit():
        return ch
    if ch in KEY_NAMES:
        return KEY_NAMES[ch]
    if ch in SHIFT_KEYS:
        return "shift-" + SHIFT_KEYS[ch]
    raise ValueError("no key for %r" % ch)


class Qemu:
    def __init__(self, name):
        self.name = name
        self.sock_path = os.path.join(LOG_DIR, name + ".monitor.sock")
        self.serial_path = os.path.join(LOG_DIR, name + ".serial.txt")
        self.qemu_log = os.path.join(LOG_DIR, name + ".qemu.log")
        for p in (self.sock_path, self.qemu_log):
            if os.path.exists(p):
                os.remove(p)
        cmd = [
            "qemu-system-x86_64", "-cdrom", os.path.join(ROOT, "myos.iso"), "-m", "512M",
            "-serial", "stdio", "-display", "none", "-no-reboot",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-monitor", "unix:%s,server,nowait" % self.sock_path,
            "-d", "cpu_reset,guest_errors", "-D", self.qemu_log,
        ]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
        self.output = b""
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.monitor = None

    def _read(self):
        while True:
            data = self.proc.stdout.read1(4096)
            if not data:
                break
            with self.lock:
                self.output += data

    def text(self):
        with self.lock:
            return self.output.decode("utf-8", errors="replace")

    def _monitor(self):
        if self.monitor is None:
            for _ in range(50):
                try:
                    s = socket.socket(socket.AF_UNIX)
                    s.connect(self.sock_path)
                    self.monitor = s
                    break
                except OSError:
                    time.sleep(0.1)
            if self.monitor is None:
                raise RuntimeError("cannot connect to QEMU monitor")
        return self.monitor

    def monitor_cmd(self, cmd):
        m = self._monitor()
        m.sendall((cmd + "\n").encode())
        time.sleep(0.05)

    def send_keys(self, text):
        for ch in text:
            self.monitor_cmd("sendkey " + key_for(ch))
            time.sleep(0.03)

    def send_serial(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.monitor_cmd("quit")
                self.proc.wait(timeout=3)
            except Exception:
                self.proc.kill()
                self.proc.wait()
        self.reader.join(timeout=2)
        if self.monitor:
            self.monitor.close()
        with open(self.serial_path, "w") as f:
            f.write(self.text())


def build_iso(name, cmdline):
    r = subprocess.run(["make", "-s", "-C", ROOT, "myos.iso", "TIMEOUT=0",
                        "CMDLINE=" + cmdline],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print(r.stdout.decode(errors="replace"))
        raise RuntimeError("build failed for test " + name)


def run_test(t):
    name = t["name"]
    timeout = t.get("timeout", 20)
    build_iso(name, t.get("cmdline", "test=" + name))

    q = Qemu(name)
    deadline = time.time() + timeout
    pos = 0          # serial text before this index is already "used"
    error = None

    try:
        for step in t["steps"]:
            kind, arg = step
            if kind in ("expect", "expect_re"):
                while True:
                    out = q.text()
                    if kind == "expect":
                        idx = out.find(arg, pos)
                        end = idx + len(arg) if idx >= 0 else -1
                    else:
                        m = re.compile(arg).search(out, pos)
                        idx, end = (m.start(), m.end()) if m else (-1, -1)
                    if idx >= 0:
                        pos = end
                        break
                    if q.proc.poll() is not None:
                        error = "QEMU exited (status %d) before %r" % (q.proc.returncode, arg)
                        break
                    if time.time() > deadline:
                        error = "timeout waiting for %r" % arg
                        break
                    time.sleep(0.05)
                if error:
                    break
            elif kind == "keys":
                q.send_keys(arg)
            elif kind == "serial":
                q.send_serial(arg)
            elif kind == "sleep":
                time.sleep(arg)
            else:
                raise ValueError("bad step " + kind)

        if error is None and t.get("exit") is not None:
            # Wait for the kernel to quit QEMU by itself.
            while q.proc.poll() is None and time.time() < deadline:
                time.sleep(0.05)
            status = q.proc.poll()
            if status is None:
                error = "QEMU did not exit (expected status %d)" % t["exit"]
            elif status != t["exit"]:
                error = "QEMU exit status %d, expected %d" % (status, t["exit"])

        if error is None:
            out = q.text()
            for bad in t.get("forbid", []):
                m = re.search(bad, out)
                if m:
                    error = "forbidden output %r" % m.group(0)
                    break
    finally:
        q.stop()

    return error, q


def main(argv):
    os.makedirs(LOG_DIR, exist_ok=True)
    if "--list" in argv:
        for t in TESTS:
            print(t["name"])
        return 0

    selected = [t for t in TESTS if not argv or t["name"] in argv]
    unknown = set(argv) - {t["name"] for t in TESTS}
    if unknown:
        print("unknown tests: " + ", ".join(sorted(unknown)))
        return 2

    failed = []
    for t in selected:
        start = time.time()
        error, q = run_test(t)
        took = time.time() - start
        if error:
            failed.append(t["name"])
            print("FAIL  %-12s %5.1fs  %s" % (t["name"], took, error))
            print("----- serial (%s) -----" % q.serial_path)
            print(q.text()[-3000:])
            print("-----------------------")
        else:
            print("ok    %-12s %5.1fs" % (t["name"], took))

    # Leave a normal ISO behind so "make run" does not boot a test by accident.
    r = subprocess.run(["make", "-s", "-C", ROOT, "myos.iso"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print(r.stdout.decode(errors="replace"))

    print("%d/%d passed" % (len(selected) - len(failed), len(selected)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
