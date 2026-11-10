# Stage 3: GDT, IDT aur CPU exceptions

**Done test:** `1/0` → reboot nahi hota, `Divide error at RIP=...` print hota hai. ✅ `make test T=div0`

```
TEST START: div0
dividing by zero...

Divide error at RIP=0xffffffff80001464
  RIP=0xffffffff80001464  CS=8  RFLAGS=10082
  ...
*** KERNEL PANIC at kernel/arch/x86_64/interrupts.c:... ***
unhandled CPU exception 0 (Divide error) in kernel mode
```

`0xffffffff80001464` exactly `idiv %ecx` instruction ka address hai (`objdump` se check kiya). #DE ek *fault* hai, isliye saved RIP faulting instruction pe hi point karta hai, uske baad wali instruction pe nahi.

## Pehle: test infrastructure

Stage 3 se har stage ka Done test headless chalta hai, isliye pehle ye banaya:

| Part | Kaam |
|---|---|
| `kernel/boot.c/.h` | Saari Limine requests ek jagah: framebuffer, HHDM, memory map, kernel address, cmdline, modules, stack size. `boot_init()` check karke `struct boot_info` bharta hai. Baaki kernel Limine structs ko seedha nahi chhoota. |
| `limine.conf` + Makefile | `@TIMEOUT@` aur `@CMDLINE@` Makefile bharta hai (`make run CMDLINE="test=div0"`). `build/config.stamp` sirf value badalne pe update hota hai, taaki ISO bina wajah rebuild na ho. |
| `isa-debug-exit` (port `0xF4`) + `qemu_exit()` | Kernel khud QEMU band kar sakta hai. Exit status `(value << 1) \| 1` hota hai: pass = 3, fail/panic = 5. Triple fault pe QEMU status 0 deta hai, to teeno alag pehchaane jaate hain. |
| `kernel/tests/` | `test=<name>` se test chunte hain. `ktest_finish(pass)` `TEST PASS/FAIL` print karke QEMU band karta hai. Test mode mein `panic()` bhi QEMU band karta hai. |
| `tools/qemu-test.py`, `make test` | Har test ke liye ISO banata hai (`TIMEOUT=0`), QEMU headless chalata hai (serial pipe pe, monitor unix socket pe), steps chalata hai (`expect`, `keys`, `serial`), aur exit status check karta hai. Logs `build/test-logs/` mein jaate hain. |

## Kya banaya

### GDT + TSS (`arch/x86_64/gdt.c`, `gdt_load.asm`)

| Selector | Entry | Access | Flags |
|---|---|---|---|
| `0x00` | null | – | – |
| `0x08` | kernel code | `0x9A` (P, ring 0, code, R) | `L=1` (64-bit) |
| `0x10` | kernel data | `0x92` | – |
| `0x18` | user data | `0xF2` (ring 3) | – |
| `0x20` | user code | `0xFA` (ring 3) | `L=1` |
| `0x28` | TSS (16 bytes, 2 slots) | `0x89` (64-bit TSS available) | – |

- **Ye order kyun:** Stage 7 ke `syscall`/`sysret` selectors STAR MSR se *calculate* karte hain. `sysret` user SS = base+8 aur CS = base+16 leta hai, isliye user data, user code se pehle aana zaroori hai.
- **CS reload:** `lgdt` sirf naya table batata hai. Segment registers mein purane descriptors cached rehte hain, isliye sab reload karne padte hain. CS `mov` se load nahi ho sakta, isliye far return (`push CS; push RIP; retfq`) use kiya.
- **TSS:** 64-bit mode mein ye task switch nahi karta. Isme sirf stacks hote hain:
  - `rsp0`: ring 3 se interrupt aane pe kaunsa kernel stack use ho (Stage 7).
  - `ist[0]` (IST1): double fault ka alag 16 KiB stack.
  - `iomap_base = sizeof(tss)`: I/O bitmap nahi hai, matlab user code kabhi `in`/`out` nahi kar sakta.
- gdb mein dekha ki `kmain` entry pe `CS=0x28` tha, jo Limine ka apna GDT tha. `gdt_init()` ke baad ye `0x08` ho jaata hai (`gdt` test check karta hai: `cs=8 ss=10 ds=10 tr=28`).

### IDT + ISR stubs (`idt.c`, `isr.asm`)

- 256 entries, har entry 16 byte ki: handler address 3 hisson mein, selector `0x08`, IST, aur attributes (P, DPL, type).
- Sab **interrupt gates** (`0xE`) hain, matlab handler chalte waqt CPU IF=0 kar deta hai, aur ek handler doosre IRQ se interrupt nahi hota.
- `#DF` IST1 pe chalta hai. `int3` ka DPL 3 hai (user mode debugger ke liye).
- **Stubs** NASM `%rep` se 256 bante hain. Jin vectors pe CPU error code push nahi karta (8, 10-14, 17, 21, 29, 30 ke alawa sab), unke stub ek fake `0` push karte hain. Isse har frame ka layout ek jaisa rehta hai. Phir vector push hota hai aur `isr_common` pe jump.
- `isr_common`: 15 GPRs push → `mov rdi, rsp` → `cld` → `call interrupt_dispatch` → ulte order mein pop → `add rsp, 16` → `iretq`.
- **Stack alignment:** CPU frame push karne se pehle RSP ko 16-byte align karta hai. Phir 5 (CPU) + 2 (stub) + 15 (regs) = 22 × 8 = 176 bytes push hote hain, jo 16 ka multiple hai. Isliye `call` ke waqt ABI wala 16-byte alignment sahi rehta hai.
- `struct interrupt_frame` exactly push order ke ulte order mein hai: sabse aakhri push (`r15`) sabse kam address pe hota hai.

### Exception handlers (`interrupts.c`)

- `interrupt_register(vector, fn)` ek handler table hai, jise Stage 4 IRQs ke liye aur Stage 5 page fault ke liye use karega.
- Unhandled exception pe naam, decoded details, register dump (CR2, CR3 ke saath) aur `panic` hota hai.
  - `#DE`: guide ka exact text `Divide error at RIP=...`.
  - `#PF`: error code bits decode hote hain (present/not-present, read/write/ifetch, user/kernel, reserved bit) aur **CR2** se faulting address.
  - `#GP/#NP/#SS/#TS`: selector error code decode hota hai (GDT/LDT/IDT, index).
  - `#DF`: saved RIP undefined hota hai. Handler confirm karta hai ki wo IST1 stack pe chal raha hai.
- `#BP` (int3) fatal nahi hai: message print karke wapas aata hai.

## Tests (`make test`: 10/10 pass)

| Test | Kya karta hai | Expected |
|---|---|---|
| `div0` | `one / zero` (dono volatile) | `Divide error at RIP=0xffffffff8...`, panic, exit 5, koi reboot nahi |
| `int3` | 14 registers mein known values daal ke `int3` | `Breakpoint at RIP=...`, `registers preserved` |
| `gp` | `mov 0x80, ds` (GDT mein sirf 7 entries hain) | `selector error: GDT index 16 (selector 80)` |
| `pf` | `0xdeadbeef000` se read | `not-present page on read, kernel mode, address CR2=0x00000deadbeef000` |
| `df` | #PF gate ko not-present karke page fault | `Double fault`, `handler running on IST1 stack: yes` |
| `ud` | `ud2` | `Invalid opcode at RIP=...` |
| `gdt` | CS/SS/DS/TR padho | `cs=8 ss=10 ds=10 tr=28` |

`int3` test guide ke "common bug" ko pakadta hai: push/pop order galat ho to koi register badal jaata, aur test `CORRUPTED` print karta.

## Bugs / issues

1. **`div0` test mein division hua hi nahi.** Output `not reached: 0` aaya.
   **Root cause:** `volatile int zero` ke saath bhi gcc ne `1 / zero` ko `(unsigned)(x + 1) > 2 ? 0 : x` mein badal diya, kyunki int `1/x` sirf `x = ±1` pe non-zero hota hai. Is tarah `idiv` instruction bani hi nahi (`objdump` se confirm kiya). Ye legal hai, kyunki zero se divide C mein undefined behaviour hai.
   **Fix:** dividend bhi `volatile`. Ab `idiv %ecx` banta hai aur #DE aata hai.
2. **Har function ke shuru mein `endbr64`.** Ubuntu gcc default `-fcf-protection` lagata hai (Intel CET), jo hamara kernel enable nahi karta. Ye CPU pe NOP hai, par disassembly gandi karta hai. Fix: `-fcf-protection=none`.
3. **NASM `-Wall` ki 256+ warnings:** `section-crossing relocation`. Ye `isr_stub_table` (`.rodata`) mein `.text` ke addresses ki wajah se aati hain, jo bilkul normal hai. Fix: `-w-reloc-abs-qword -w-reloc-rel-dword`.
4. **`df` test ka RSP misleading tha.** Frame mein saved RSP purana stack dikhata hai, IST nahi. Isliye `gdt_on_double_fault_stack(frame)` se proof add kiya.

## Debugging setup check

`make debug` + `gdb -ex 'target remote :1234' -ex 'break kmain' -ex continue` kaam karta hai: `kmain.c:21` pe breakpoint lagta hai aur `next` se stepping hoti hai.
