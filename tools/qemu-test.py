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
#   ("mark", None)           remember the current time
#   ("elapsed", (lo, hi))    seconds since "mark" must be in [lo, hi]
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

    # Stage 4: PIT at 100 Hz: 200 ticks must take about 2 real seconds.
    dict(name="timer", steps=[
        ("expect", "interrupts enabled"),
        ("expect", "ticks=100"), ("mark", None),
        ("expect", "ticks=300"), ("elapsed", (1.6, 2.6)),
        ("expect", "pit_sleep_ms(500) took"),
        ("expect", "TEST PASS: timer"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 4 Done test: typing on the PS/2 keyboard shows up (shift, backspace too).
    dict(name="keyboard", steps=[
        ("expect", "type a line:"),
        ("keys", "Hello, Worlx\bd! (1+1=2)\n"),
        ("expect", "got line: 'Hello, World! (1+1=2)'"),
        ("expect", "TEST PASS: keyboard"),
    ], forbid=[PANIC_RE], exit=3),

    # Serial input (COM1 IRQ 4) goes to the same queue.
    dict(name="serialin", steps=[
        ("expect", "type a line:"),
        ("serial", "echo from serial\r"),
        ("expect", "got line: 'echo from serial'"),
        ("expect", "TEST PASS: serialin"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 5 Done test (PMM): 1000 pages allocated and freed without a leak.
    dict(name="pmm", steps=[
        ("expect_re", r"pmm: \d+ MiB RAM tracked"),
        ("expect", "allocated 1000 pages"),
        ("expect", "(diff 1000)"),
        ("expect", "leak = 0 pages"),
        ("expect", "TEST PASS: pmm"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="pmm_double_free", steps=[
        ("expect_re", r"pmm_free: double free of phys 0x[0-9a-f]{16}"),
    ], forbid=[r"TEST PASS"], exit=5),

    # VMM: own page tables, map/unmap 1000 pages, no leak.
    dict(name="vmm", steps=[
        ("expect", "vmm: own page tables active"),
        # text: present only (read+execute); rodata: present+NX; data: present+write+NX
        ("expect_re", r"flags: text [0-9a-f]*1, rodata 8000000000000[0-9a-f]*1, "
                      r"data 8000000000000[0-9a-f]*3"),
        ("expect", "vmm: 1000 pages mapped at 0xffffe00000000000"),
        ("expect", "unmapped=yes, leak = 0 pages"),
        ("expect", "TEST PASS: vmm"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 5 Done test (page fault): the handler prints the bad address.
    dict(name="vmm_unmapped", steps=[
        ("expect", "unmapping it and writing again..."),
        ("expect", "not-present page on write, kernel mode, address CR2=0xffffe00000005000"),
    ], forbid=[r"not reached"], exit=5),

    dict(name="nx", steps=[
        ("expect", "protection violation on instruction fetch, kernel mode, "
                   "address CR2=0xffffe00000009000"),
    ], forbid=[r"not reached"], exit=5),

    dict(name="wp", steps=[
        ("expect_re", r"protection violation on write, kernel mode, address CR2=0xffffffff8000"),
    ], forbid=[r"not reached"], exit=5),

    dict(name="null", steps=[
        ("expect", "not-present page on read, kernel mode, address CR2=0x0000000000000000"),
    ], forbid=[r"not reached"], exit=5),

    # Heap: random kmalloc/kfree, everything merges back into one free block.
    dict(name="heap", steps=[
        ("expect", "heap: free-list allocator"),
        ("expect", "heap: after 5000 random ops"),
        ("expect", "heap: all freed: leaked 0 allocations / 0 bytes, 1 free block(s), heap_check ok"),
        ("expect", "ok, no growth"),
        ("expect", "3 MiB kmalloc grew the heap"),
        ("expect", "TEST PASS: heap"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="heap_double_free", steps=[("expect_re", r"kfree: double free of 0xffffc0000000[0-9a-f]{4}")],
         forbid=[r"TEST PASS"], exit=5),

    dict(name="heap_overflow", steps=[("expect", "has a bad header (magic 58585858)")],
         forbid=[r"TEST PASS"], exit=5),

    # Stage 6 Done test: A and B print in turns (yield and sleep versions).
    dict(name="threads", steps=[
        ("expect", "scheduler running"),
        ("expect", "yield: A1 B1 A2 B2 A3 B3 A4 B4 A5 B5"),
        ("expect", "sleep: A1 B1 A2 B2 A3 B3 A4 B4 A5 B5"),
        ("expect", "TEST PASS: threads"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="preempt", steps=[("expect_re", r"preempt: spin0 \d+ loops, spin1 \d+ loops"),
                                ("expect", "TEST PASS: preempt")], forbid=[PANIC_RE], exit=3),

    # Stage 6 Done test: sleep(ms) works (order + real time).
    dict(name="sleep", steps=[
        ("expect", "slept 100 ms (asked 100)"),
        ("expect", "slept 200 ms (asked 200)"),
        ("expect", "slept 300 ms (asked 300)"),
        ("expect", "wake order: 100 200 300"),
        ("mark", None),
        ("expect", "main slept 1000 ms: 100 ticks"), ("elapsed", (0.8, 1.5)),
        ("expect", "TEST PASS: sleep"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="race", steps=[
        ("expect", "race without lock:"),
        ("expect_re", r"race with spinlock: counter (\d+), increments \1, lost 0 updates"),
        ("expect", "TEST PASS: race"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="deadlock", steps=[("expect", "deadlock: spinlock 'test_lock' is already held")],
         forbid=[r"TEST PASS"], exit=5),

    dict(name="stack_overflow", steps=[
        ("expect", "Double fault"),
        ("expect", "handler running on IST1 stack: yes"),
        ("expect", "kernel stack overflow: guard page hit"),
    ], forbid=[r"TEST PASS"], exit=5),

    dict(name="task_leak", steps=[("expect", "100 tasks done: page leak 0, heap allocation leak 0"),
                                  ("expect", "TEST PASS: task_leak")], forbid=[PANIC_RE], exit=3),

    # Stage 7: a user program in ring 3 uses write/getpid/sleep/exit.
    dict(name="user_hello", steps=[
        ("expect", "[process 'hello' started"),
        ("expect", "Hello from user mode!"),
        ("expect", "running in ring 3"),
        ("expect_re", r"pid = \d+"),
        ("expect", "tick\r\ntick\r\ntick"),
        ("expect_re", r"\[process 'hello' \(pid \d+\) exited with code 0\]"),
        ("expect", "TEST PASS: user_hello"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 7 Done test: user programs crash, the kernel kills them and lives on.
    dict(name="user_crash", steps=[
        ("expect", "crash_null: reading address 0..."),
        ("expect_re", r"\[process 'crash_null' \(pid \d+\) killed: Page fault at RIP=0x0000000000400\w+, "
                      r"address 0x0000000000000000 \(not present, read\)\]"),
        ("expect", "crash_priv: trying cli in ring 3..."),
        ("expect_re", r"\[process 'crash_priv' \(pid \d+\) killed: General protection fault"),
        ("expect", "crash_kernel: writing to kernel memory..."),
        ("expect_re", r"\[process 'crash_kernel' \(pid \d+\) killed: Page fault at RIP=0x0000000000400\w+, "
                      r"address 0xffffffff80001000 \(protection violation, write\)\]"),
        ("expect", "Hello from user mode!"),
        ("expect", "kernel still alive after 3 crashed processes"),
        ("expect", "TEST PASS: user_crash"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="user_badptr", steps=[
        ("expect", "badptr: all bad pointers rejected with EFAULT"),
        ("expect", "TEST PASS: user_badptr"),
    ], forbid=[PANIC_RE, r"accepted a bad pointer"], exit=3),

    dict(name="user_read", steps=[
        ("expect", "reader: type lines, 'q' quits"),
        ("serial", "hello kernel\r"),
        ("expect", "hello kernel"),
        ("keys", "typed\n"),
        ("expect", "typed"),
        ("serial", "q\r"),
        ("expect_re", r"\[process 'reader' \(pid \d+\) exited with code 7\]"),
        ("expect", "TEST PASS: user_read"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 7: the syscall/sysret path.
    dict(name="user_syscall", steps=[
        ("expect", "fastcall: hello via the syscall instruction"),
        ("expect", "fastcall: getpid via syscall == getpid via int 0x80"),
        ("expect_re", r"fastcall: 200000 x getpid: int 0x80 \d+ ms, syscall \d+ ms"),
        ("expect_re", r"\[process 'fastcall' \(pid \d+\) exited with code 0\]"),
        ("expect", "Hello from user mode!"),
        ("expect", "TEST PASS: user_syscall"),
    ], forbid=[PANIC_RE], exit=3, timeout=60),

    # Stage 8: VFS over the initrd.
    dict(name="vfs", steps=[
        ("expect_re", r"initrd mounted at /: \d+ files and directories"),
        ("expect", "  docs/\r\n    about.txt (162 bytes)\r\n  etc/\r\n    motd (53 bytes)\r\n"
                   "  hello.txt (59 bytes)\r\n  dev/\r\n    console (device)\r\n    null (device)"),
        ("expect", "/hello.txt (59 bytes): Hello from the initrd!"),
        ("expect", "lookup with . and ..: same node"),
        ("expect", "error cases (ENOENT, EROFS, EISDIR, file as dir): ok"),
        ("expect", "read about.txt in 7-byte pieces: 162 bytes (size 162)"),
        ("expect", "TEST PASS: vfs"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 8 Done test: /hello.txt opened and read (by a user program).
    dict(name="user_cat", steps=[
        ("expect", "cat_hello: opened /hello.txt as fd 3"),
        ("expect", "Hello from the initrd! This file was read through the VFS."),
        ("expect", "cat_hello: error checks ok (EBADF, ENOENT, EFAULT)"),
        ("expect", "heap allocation leak after the process: 0"),
        ("expect", "TEST PASS: user_cat"),
    ], forbid=[PANIC_RE, "FAILED"], exit=3),

    # Stage 8 (later part): ATA PIO driver + FAT32 on a disk.
    dict(name="disk", steps=[
        ("expect", "ata: drive 0: 'QEMU HARDDISK', 131072 sectors (64 MiB)"),
        ("expect", "fat32: 'MYOSDISK' mounted at /disk"),
        ("expect", "  docs/\r\n    nested-file.txt (45 bytes)\r\n  readme.txt (70 bytes)\r\n"
                   "  welcome-to-the-disk.txt (87 bytes)\r\n  numbers.txt (8893 bytes)"),
        ("expect", "/disk/readme.txt: This file lives on a FAT32 disk."),
        ("expect", "/disk/welcome-to-the-disk.txt: Welcome to the disk!"),
        ("expect", "/disk/docs/nested-file.txt: A file inside a directory"),
        ("expect", "numbers.txt: 8893 bytes, lines 1..2000 in order: ok"),
        ("expect", "read across a cluster boundary: ok"),
        ("expect", "TEST PASS: disk"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="ata", steps=[
        ("expect", "ata: wrote 4 sectors at LBA 10: ok, read back: ok, data identical"),
        ("expect", "read past the end of the disk refused: yes"),
        ("expect", "TEST PASS: ata"),
    ], forbid=[PANIC_RE], exit=3),

    # Stage 9 Done test: a separately compiled program runs from the initrd.
    dict(name="elf_hello", steps=[
        ("expect_re", r"Hello from an ELF program! \(pid \d+\)"),
        ("expect", "argc = 3"),
        ("expect", 'argv[0] = "/bin/hello"'),
        ("expect", 'argv[1] = "first"'),
        ("expect", 'argv[2] = "second arg"'),
        ("expect", "kernel: /bin/hello exited with code 0"),
        ("expect", "TEST PASS: elf_hello"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="spawn", steps=[
        ('expect', 'argv[2] = "two words"'),
        ("expect_re", r"spawntest: /bin/hello \(pid \d+\) finished, wait\(\) = \d+, exit code 0"),
        ("expect_re", r"spawntest: /bin/exitcode \(pid \d+\) finished, wait\(\) = \d+, exit code 42"),
        ("expect", "segv: writing to NULL..."),
        ("expect_re", r"\[process 'segv' \(pid \d+\) killed: Page fault"),
        ("expect_re", r"spawntest: /bin/segv \(pid \d+\) finished, wait\(\) = \d+, exit code 142"),
        ("expect", "spawn of a missing file = -2 (expected -2)"),
        ("expect", "spawn of a text file = -8 (expected -8)"),
        ("expect", "wait for a non-child = -10 (expected -10)"),
        ("expect", "spawntest: all ok"),
        ("expect", "kernel: spawntest exited with code 0"),
        ("expect", "TEST PASS: spawn"),
    ], forbid=[PANIC_RE, "segv: not reached"], exit=3),

    dict(name="elf_bad", steps=[
        ("expect", "text file: not an ELF file"),
        ("expect", "cut after 40 bytes: not an ELF file"),
        ("expect", "segment at a kernel address: segment outside user space"),
        ("expect", "segment bigger than the file: segment outside user space or outside the file"),
        ("expect", "entry point in rodata: entry point is not in an executable segment"),
        ("expect", "TEST PASS: elf_bad"),
    ], forbid=[PANIC_RE, "LOADED"], exit=3),

    dict(name="elf_leak", steps=[
        ("expect", "10 x spawn+wait: exit codes ok, page leak 0, heap leak 0"),
        ("expect", "TEST PASS: elf_leak"),
    ], forbid=[PANIC_RE], exit=3),

    dict(name="user_leak", steps=[
        ("expect", "20 processes: page leak 0, heap leak 0, page-table leak 0"),
        ("expect", "TEST PASS: user_leak"),
    ], forbid=[PANIC_RE], exit=3),
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
            "qemu-system-x86_64", "-cdrom", os.path.join(ROOT, "myos.iso"), "-boot", "d", "-m", "512M",
            "-serial", "stdio", "-display", "none", "-no-reboot",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-drive", "file=%s,format=raw,if=ide,index=0,media=disk,snapshot=on"
                      % os.path.join(ROOT, "build", "disk.img"),
            "-drive", "file=%s,format=raw,if=ide,index=1,media=disk,snapshot=on"
                      % os.path.join(ROOT, "build", "scratch.img"),
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
    mark = time.time()
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
            elif kind == "mark":
                mark = time.time()
            elif kind == "elapsed":
                took = time.time() - mark
                if not (arg[0] <= took <= arg[1]):
                    error = "elapsed %.2fs, expected %.1f-%.1fs" % (took, arg[0], arg[1])
                    break
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
