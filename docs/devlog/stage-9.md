# Stage 9: ELF loader + exec

**Done test:** alag compile kiya `hello` program initrd se chale. ✅ (`make test T=elf_hello`)

```
Hello from an ELF program! (pid 2)
argc = 3
argv[0] = "/bin/hello"
argv[1] = "first"
argv[2] = "second arg"
kernel: /bin/hello exited with code 0
```

`user/programs/hello.c` ek alag C program hai jo hamari libc ke saath link hota hai. Makefile use initrd ke `/bin/hello` mein daalta hai, aur kernel use VFS se padh ke ELF loader se chalaata hai. Poora suite: **44/44 pass**.

## Kya banaya

### ELF loader (`proc/elf.c`)

1. **Header checks:** magic `\x7fELF`, 64-bit, little endian, `ET_EXEC`, x86_64, aur program header table file ke andar ho.
2. **Har `PT_LOAD` segment:**
   - `[vaddr, vaddr + memsz)` user space mein ho (`≥ 0x1000`, `< 0x800000000000`, koi overflow na ho), `filesz ≤ memsz`, aur file mein bytes sach mein hon.
   - Har page ke liye naya zeroed physical page map hota hai, segment ke flags ke hisaab se: `W` → `PTE_WRITE`, aur `X` nahi hai to `PTE_NX`. Ab code read+execute hai, rodata sirf read, aur data read+write+NX (flat binaries mein sab RWX tha).
   - `filesz` bytes copy hote hain. Baaki `memsz − filesz` (yaani `.bss`) zero rehta hai, kyunki pages zeroed hain.
3. Entry point kisi executable segment ke andar hona chahiye.

### User stack: `argc/argv` (System V ABI)

```
stack top ──► "second arg\0" "first\0" "/bin/hello\0"     (strings)
              ...padding (16-byte alignment)...
              0, 0          auxv: AT_NULL
              0             envp: khaali
              0             argv ka end
              argv[2] ──┐
              argv[1]   ├── upar wali strings ke USER addresses
              argv[0] ──┘
  rsp ──────► argc = 3
```

Pehle stack ek kernel buffer mein banta hai, phir `process_copy_to` se user stack ke top pe copy hota hai. `_start` pe `rsp` 16-byte aligned hota hai.

### `spawn` + `wait`

- `spawn(path, argv)`: path aur argv (user pointers ka array, NULL pe khatam) dono check hoke ek kernel buffer mein copy hote hain (max 32 args, 2 KiB). Phir `process_spawn_elf`: VFS se file → naya address space → ELF load → stack → task. Child ko parent ke fds 0-2 milte hain (same `struct file`, refcount++).
- **Parent/child:**
  - Child exit karta hai to wo ZOMBIE banta hai aur parent ki `children_wq` jagata hai.
  - Agle switch pe `finish_switch` child ka kernel stack aur address space free karta hai (`resources_freed`). Lekin `struct task` (exit code ke saath) **tab tak rehta hai** jab tak parent `wait()` na kare.
  - `wait(pid, &status)` exit code deta hai aur struct free karta hai. `pid` agar apna child na ho to `-ECHILD`.
  - Parent pehle mar jaaye to children orphan ho jaate hain (`parent = NULL`) aur khud reap hote hain.
- Crash se mare process ka exit code `128 + vector` hota hai (page fault → 142), Unix shell ke signal codes jaisa.

### User libc (`user/libc/`)

| File | Kya |
|---|---|
| `crt0.asm` | `_start`: `argc = [rsp]`, `argv = rsp + 8`, `main(argc, argv)`, `exit(return value)` |
| `include/syscall.h` | `syscall0..3`: `syscall` instruction (rcx/r11 clobbered), numbers kernel ke `syscall_nr.h` se (wahi file include hoti hai) |
| `unistd.c` | `write read open close getpid sleep_ms uptime_ms spawn wait exit` |
| `string.c`, `stdlib.c` | `strlen strcmp strncmp strcpy strchr mem*`, `atoi` |
| `stdio.c` | `printf dprintf snprintf vsnprintf putchar puts` (kernel jaisa formatter; 128-byte buffer jo `write()` se flush hota hai) |

- `user/user.ld`: `0x400000` se shuru, aur text/rodata/data teen alag page-aligned segments (R X / R / RW). Kernel loader do segments ko ek page share karne nahi deta.
- User programs bhi `-mno-sse -mno-mmx -mno-80387` ke saath compile hote hain. **Wajah:** kernel context switch pe FPU/SSE registers save nahi karta, to do processes ek doosre ki SSE state kharab kar dete.

### Programs (initrd `/bin`)

`hello` (argv print), `exitcode N` (N return), `segv` (NULL pe likhta hai), `spawntest` (spawn/wait aur saare error cases).

## Tests

| Test | Kya |
|---|---|
| `elf_hello` | **Done**: kernel `/bin/hello` ko 3 args ke saath chalaata hai aur wait karta hai |
| `spawn` | user space se: hello (args ke saath, space wala arg bhi), `exitcode 42` → 42, `segv` → 142, missing file → `-ENOENT`, text file → `-ENOEXEC`, non-child wait → `-ECHILD` |
| `elf_bad` | text file, 40 bytes pe kati ELF, kernel address wala segment, file se bada segment, rodata mein entry point: sab reject |
| `elf_leak` | 10 × spawn+wait: sahi exit codes, page leak 0, heap leak 0 |

## Bugs / issues

1. **Kernel panic `RIP=0` jab teesra process spawn hua.**
   ```
   Page fault at RIP=0x0000000000000000 ... kernel mode
   RSP=0xffffd00000030000  RSI=0xffffd0000002ffc8
   ```
   - `RSI` ek naye task ka switch frame tha (slot top − 7 words), aur `ret` ne `task_trampoline` ki jagah `0` pop kiya. Matlab frame likhne ke baad wo memory badal gayi thi.
   - Temporary logging se dikha: har naya process **wahi kernel stack slot 2** reuse kar raha tha (same virtual address, har baar naye physical pages).
   - **Root cause: stale TLB.** `vmm_unmap_page` sirf `is_current(as)` hone pe `invlpg` karta tha. Kernel stack `kernel_space` mein free hota tha, lekin us waqt CR3 mein *user* process (spawntest) ka PML4 tha, to "current nahi hai" maan ke `invlpg` skip ho gaya. Kernel half har address space mein shared hai, isliye CPU ke TLB mein purani translation bachi rahi. Naye task ka frame *purane, free ho chuke* physical page mein likha gaya, aur PMM ne baad mein wahi page zero karke kisi aur ko de diya.
   - **Fix:** kernel-half (`≥ 0xffff800000000000`) addresses pe hamesha `invlpg`. Fix ke baad `spawn` + `elf_leak` 5 baar chalaye, har baar pass.
   - Ye bug timing pe depend karta tha: debug prints se crash `exitcode` se `segv` pe chala gaya. TLB bugs aise hi hote hain.
2. **`vfs` test fail:** test initrd mein exactly 6 entries expect karta tha, jabki ab `/bin` bhi hai. Count ko regex bana diya.
