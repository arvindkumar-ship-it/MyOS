# Stage 10: Shell

**Done test:** `myos> ` prompt aaye aur commands chalein. ✅ (`make test T=shell`, aur `make run` mein haath se)

```
Welcome to MyOS.
Type "help" for a list of commands.
myos> ls /
bin/                            <dir>
docs/                           <dir>
etc/                            <dir>
hello.txt                          59
dev/                            <dir>
disk/                           <dir>
myos> cat /hello.txt
Hello from the initrd! This file was read through the VFS.
myos> echo hello   "quoted   words" end
hello quoted   words end
myos> ps
  PID  PPID  STATE     TYPE    TIME  NAME
    8     2  running   user     0.00s  ps
    2     0  blocked   user     0.00s  sh
    1     0  ready     kernel   1.84s  idle
    0     0  blocked   kernel   0.01s  kmain
myos> segv
segv: writing to NULL...
[process 'segv' (pid 11) killed: Page fault at RIP=0x0000000000400032, address 0x0000000000000000 (not present, write)]
[segv exited with code 142]
myos> exit 7
```

Screenshot: `docs/screenshot.png`. Poora suite: **45/45 pass**.

## Kya banaya

### Naye syscalls

| # | Syscall | Kiske liye |
|---|---|---|
| 11 | `readdir(path, index, &dirent)` | `ls`: index-th entry deta hai (1 = mili, 0 = khatam) |
| 12 | `stat(path, &stat)` | `ls file`, type aur size |
| 13 | `ps(list, max)` | `ps`: pid, ppid, state, user/kernel, CPU time, name |

Structs (`myos_dirent`, `myos_stat`, `myos_procinfo`) `syscall_nr.h` mein hain, jise kernel aur libc **dono include karte hain**, isliye layout kabhi alag nahi ho sakta. `ps` pehle kernel buffer mein snapshot leta hai (interrupts off), phir user buffer mein copy karta hai.

### Shell (`user/programs/sh.c`)

- **Line editing:** kernel echo nahi karta, isliye shell har key khud dikhata hai. Backspace pe `"\b \b"` bhejta hai (peeche, space, phir peeche). Ctrl+C line cancel karta hai, aur Ctrl+L screen saaf karta hai.
- **Parse:** spaces pe todta hai. `"double quotes"` ke andar ke spaces word mein hi rehte hain. Word usi buffer mein dobara likha jaata hai, bina quotes ke.
- **Builtins:** `help`, `clear`, `exit [code]`.
- **Baaki commands:** `/bin/<naam>` (ya `/` wala path) → `spawn` → `wait`. Ye Unix ke `fork + exec + wait` jaisa hai. Non-zero exit code pe `[cat exited with code 1]` dikhta hai. File na mile to `command not found`.
- Shuru mein `/etc/motd` print hota hai.

### Commands (`user/programs/`)

`ls` (dirs `/`, devices `<dev>`, files size ke saath, kai paths), `cat` (kai files, errors `strerror` se), `echo`, `ps`, `uptime`. `clear` aur `help` builtins hain.

### Kernel side

- **Normal boot:** status bar thread + `/bin/sh` jiska parent `kmain` hai. Shell exit kare to kmain naya shell shuru karta hai. Stage 6/7 ke demos (A/B, echo thread, asm programs) ab sirf tests mein chalte hain, kyunki echo thread aur shell dono ek hi input padhte to shell ko aadhe characters milte.
- **Console ANSI:** `clear` screen ke liye `\f` aur serial terminal ke liye `ESC[2J ESC[H` bhejta hai. Framebuffer console pehle ESC ko ek glyph ki tarah draw kar deta. Ab ek chhota state machine `ESC [ numbers letter` samajhta hai: `2J` = clear, `H` = home, baaki chupchaap skip.

## Test (`shell`)

Runner insaan ki tarah type karta hai: kuch commands PS/2 keyboard se (`sendkey`, backspace bhi: `hellx⌫o world`), kuch serial se. Ye sab check hota hai: `help`, `ls /`, `ls /bin /dev`, `cat` (initrd aur FAT32 dono), quotes wala `echo`, `ps`, `hello world` (argv), `nosuch` (not found), `cat /missing` (exit 1), `segv` (exit 142), `uptime`, `clear` (`\f`), aur `exit 7` (kernel ko exit code 7 milta hai).

## Bugs / issues

1. **`shell` test timeout `ps` pe:** shell sahi tha. `ps` naye tasks ko pehle dikhata hai (ps, sh, idle, kmain), aur test `idle` ko `sh` se pehle dhoondh raha tha. Test mein order theek kiya.
2. **ESC codes screen pe kachra ban jaate.** Log mein `^L^[[2J^[[H` dikha, to console mein ANSI parser add kiya (upar dekho). Screendump se confirm kiya ki `clear` ke baad screen bilkul saaf hai.
3. **Shell parser pehli baar `goto` ke saath likha gaya tha.** Padhne mein mushkil tha, to `in_quotes` flag wale ek saaf loop se badal diya.

## Saath mein band hue purane kaam

- `README.md`: features, build/test commands, layout, progress checklist (sab ✅), screenshot.
- Setup stage se pending: Makefile ka Limine download rule ek alag folder (`DEPS_DIR=build/deps-check`) mein end-to-end chala ke dekha. Download hua, aur files `deps/` se byte-for-byte same nikli.

## Aage (guide ka "Aage" hissa, abhi nahi kiya)

Pipes (`|`) aur redirection, `cd`/working directory, FAT32 write, APIC + SMP, har task ki FPU/SSE state save karna (taaki user code SSE use kar sake), user `malloc` (`brk`), virtio, networking.
