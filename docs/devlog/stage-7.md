# Stage 7: User mode + system calls

**Done test:** user program crash kare, kernel use maar de aur khud chalta rahe. ✅ (`make test T=user_crash`)

```
crash_null: reading address 0...
[process 'crash_null' (pid 2) killed: Page fault at RIP=0x0000000000400018, address 0x0000000000000000 (not present, read)]
crash_priv: trying cli in ring 3...
[process 'crash_priv' (pid 3) killed: General protection fault at RIP=0x0000000000400018]
crash_kernel: writing to kernel memory...
[process 'crash_kernel' (pid 4) killed: Page fault at RIP=0x000000000040001f, address 0xffffffff80001000 (protection violation, write)]
Hello from user mode!
running in ring 3
pid = 5
tick
tick
tick
[process 'hello' (pid 5) exited with code 0]
kernel still alive after 3 crashed processes
```

Poora suite: **36/36 pass**.

## Kya banaya

### 1. Har process ka apna PML4, kernel half shared (`mm/vmm.c`)

- `vmm_create_user_space()`: naya PML4. Entries 0-255 (user half) khaali, aur 256-511 kernel PML4 se **copy**. Kyunki Stage 5 mein kernel ke saare 256 PDPTs pehle se bana diye the, ye entries *same* tables pe point karti hain. Isliye baad mein jo kernel mapping (heap, stacks) add hoti hai, wo har process mein apne aap dikhti hai.
- `vmm_destroy_user_space()`: sirf user half walk karta hai (PDPT → PD → PT → data pages), sab free karta hai, phir PML4.
- `vmm_user_range_ok(as, ptr, len, write)`: range user half mein ho, overflow na ho, aur har page `PTE_USER` (aur zaroorat ho to `PTE_WRITE`) ke saath mapped ho.

### 2. TSS `rsp0` aur ring 3 mein jaana

- `schedule()` har switch pe `tss_set_rsp0(next->virt_kstack_top)` karta hai. Ring 3 mein interrupt aane pe CPU kernel stack TSS se leta hai.
- `enter_user_mode(entry, user_rsp)` (`usermode.asm`): ring 3 mein "jump" karne ki koi instruction nahi hai, isliye ek nakli interrupt frame banate hain (`SS=0x1b, RSP, RFLAGS=IF|2, CS=0x23, RIP`) aur `iretq` karte hain. Saare registers pehle zero kar dete hain, taaki kernel ki values user ko na dikhein.
- User layout: code `0x400000` pe, aur 64 KiB stack `0x7ffffff00000` ke neeche (NX).
- Programs abhi **flat binaries** hain (`nasm -f bin`, `org 0x400000`), jo kernel mein `incbin` se embed hain (`proc/builtin_programs.asm`), kyunki filesystem abhi nahi hai. Stage 9 mein ELF aayega.

### 3. System calls

ABI Linux jaisa hai: `rax` = number, `rdi, rsi, rdx, r10, r8` = arguments, `rax` = result (negative = `-errno`).

| # | Syscall | Kaam |
|---|---|---|
| 1 | `write(fd, buf, len)` | fd 1/2 → console + serial |
| 2 | `exit(code)` | task zombie, address space reap ke waqt free |
| 3 | `getpid()` | task id |
| 4 | `sleep(ms)` | `task_sleep_ms` |
| 5 | `read(fd, buf, len)` | fd 0: kam se kam 1 character ka wait (task block hota hai), phir jo bhi available ho |
| 6 | `uptime()` | ms since boot (benchmark ke liye) |

**Do raaste:**
- **`int 0x80`:** IDT gate DPL 3 ke saath (bina DPL 3 ke ring 3 ko #GP milta). CPU khud rsp0 pe switch karke frame push karta hai.
- **`syscall/sysret`:** MSRs `STAR` (kernel CS 0x08; sysret base 0x10 → user SS 0x18, CS 0x20, isiliye GDT mein user data, user code se pehle hai), `LSTAR` (entry), `FMASK` (IF/DF/TF/AC clear), aur `EFER.SCE`. `syscall` **stack switch nahi karta**, isliye `syscall_entry.asm` khud user RSP ek global mein rakhta hai (IF=0 hai aur single CPU, isliye safe), `sched_kernel_rsp` pe switch karta hai, aur same `struct interrupt_frame` banata hai. Wapsi `sysretq` se hoti hai. Agar return address user half ka na ho, to `iretq` use hota hai, kyunki non-canonical RCX pe `sysret` ring 0 mein fault karta hai (ek mashhoor x86 security issue).
- Benchmark (`fastcall`): 200000 × getpid: **int 0x80 70 ms, syscall 30 ms**.

### 4. Security: user pointers

Kernel ring 0 mein hai, aur bina check ke `write(1, 0xffffffff80000000, 16)` kernel memory print kar deta. Har buffer pehle `vmm_user_range_ok` se check hota hai, aur galat pointer pe `-EFAULT` milta hai, kernel use chhoota bhi nahi. `badptr` program check karta hai: kernel address, NULL, wrap-around length (`0xfffffffffffff000`), aur read ke liye kernel rodata. Chaaron `EFAULT` dete hain.

### 5. User crash = sirf process ki maut

`exception_fatal` dekhta hai ki `CS & 3 == 3` hai ya nahi. Agar hai, to wo `user_exception` mein jaata hai: `[process ... killed: <exception> at RIP=..., address ...]`, phir `task_exit(128 + vector)`. Address space aur stack reaper free karta hai. Kernel ka panic sirf ring 0 ke crash pe hota hai.

## Tests

| Test | Kya |
|---|---|
| `user_hello` | ring 3 (`CS & 3` = 3 print hota hai), write, getpid, sleep ×3, exit(0) |
| `user_crash` | **Done**: NULL read, `cli` (#GP), kernel memory pe likhna, phir `hello` normal chalna |
| `user_badptr` | 4 tarah ke bad pointers → `-EFAULT` |
| `user_read` | `read(0)`: serial input aur PS/2 keyboard (sendkey) dono, echo, `q` pe exit(7) |
| `user_syscall` | `syscall` instruction, dono raaston ka same result, benchmark |
| `user_leak` | 20 processes (10 crash, 10 normal): page, heap aur page-table leak sab 0 |

## Experiment: TSS `rsp0` set na karo to?

`tss_set_rsp0` comment karke `user_hello` chalaya:

```
[process 'hello' (pid 2) killed: Double fault at RIP=0x0000000000400016]
```

Pehle `int 0x80` pe CPU ring 3 → ring 0 jaata hai aur TSS se `RSP0 = 0` load karta hai. Frame push karte waqt `0xfffffffffffffff8` pe page fault aata hai, aur wo fault deliver karte waqt **double fault**. #DF IST1 ke apne stack pe chalta hai (Stage 3), isliye kernel bach gaya. IST ke bina yahan triple fault aur reboot hota. Code restore kar diya.

## Bugs / issues

- Is stage mein sab pehli koshish mein chal gaya. Upar wala experiment jaan-boojh ke tha.
- NASM warning `implicit DEFAULT ABS is deprecated` aayi (`mov rax, [0]`). `[abs 0]` likh ke saaf kiya ki absolute address 0 chahiye, rip-relative nahi.
- **Design note:** syscall handler poore samay IF=0 ke saath chalta hai (FMASK aur interrupt gate dono). Ye simple aur safe hai. Blocking syscalls (`read`, `sleep`) schedule karke CPU chhod dete hain, isliye system atakta nahi.
