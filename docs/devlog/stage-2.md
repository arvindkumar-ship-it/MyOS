# Stage 2: kprintf + panic

**Done test:** `kprintf("%x", 0xDEADBEEF)` prints `deadbeef`. ✅ Passed in QEMU (serial + screen).

## Kya banaya

| File | Kaam |
|---|---|
| `kernel/lib/kprintf.c/.h` | `kprintf` / `kvprintf`: `%d %i %u %x %X %p %s %c %%`, `l`/`ll` (64-bit), width aur zero-pad (`%016lx`). Output serial pe hamesha jaata hai, screen pe console ready hone ke baad. |
| `kernel/lib/panic.c/.h` | `panic(...)` macro → `panic_at(file, line, fmt, ...)`: `cli`, message print, `halt_forever()`. `kassert(cond)` condition ka text bhi print karta hai. |
| `kernel/arch/x86_64/cpu.h` | `halt_forever()` (`cli; hlt` loop), jo pehle `kmain.c` mein `hcf()` tha. |
| `kernel/lib/string.c/.h` | `memcpy/memset/memmove/memcmp` + naya `strlen`. |
| `kernel/lib/string_test.c` | `string_selftest()`: overlap wala `memmove` (dono directions), `memcmp` ka sign, `memset`, `strlen`. |
| `kernel/drivers/console.c` | `console_is_ready()`, taaki kprintf console init se pehle screen pe na likhe. |

Serial output:

```
MyOS booting...
framebuffer: 1280x800, pitch 5120, virt 0xffff8000fd000000
console ready
Hello from MyOS
string selftest: ok
deadbeef
[-42] [0] [4294967295] [A] [str] [(null)] [%] [BEEF]
[  -42] [-0042] [00001234] [ffffffff80000000] [-9223372036854775808] [0xffffffff80001000]
```

Panic test (temporary, baad mein hata diya):

```
*** KERNEL PANIC at kernel/kmain.c:92 ***
test 42
System halted.
```

## Bugs / issues

- **Selftest asli functions ko call hi nahi kar raha tha.** Jab test `string.c` ke andar tha, gcc ne `memcpy/memmove` ko inline karke zyaadatar checks compile time pe hi solve kar diye. `objdump -dr` mein sirf `panic_at` ke calls dikhe. Fix: test ko alag file `string_test.c` mein le gaye. Ab `memcmp`, `memmove` wagairah asli calls hain.
- **Recursion ka risk check kiya.** `-O2` pe gcc `memset` jaisa loop dekh ke uski jagah `call memset` daal sakta hai, aur `memset` ke andar ye infinite recursion ban jaata. Disassembly mein `mem*` ke andar koi `call` nahi mila. Wajah `-ffreestanding` hai, jo `-fno-builtin` bhi lagata hai.
- **gcc warning: `%s` ko NULL.** Ye `format(printf)` attribute ki compile-time check thi, aur NULL hum jaan-boojh ke test kar rahe the. `volatile` pointer se value compiler se chhupa di.

## Design decisions

- `INT64_MIN` ke liye `-value` nahi likha, kyunki signed overflow undefined behaviour hai. Pehle `uint64_t` mein convert karke phir negate kiya.
- `%p` hamesha 16 hex digits deta hai, taaki addresses ek column mein line up hon.
- Unknown specifier (`%q`) as-is print hota hai, taaki galti dikhe. Format string `%` pe khatam ho to function wahin ruk jaata hai, string ke aage nahi padhta.
- `va_arg` hamesha exact passed type se padhte hain. `char` ko `int` ki tarah padhte hain, kyunki `...` mein `char` promote hoke `int` ban jaata hai.

## Kya seekha

- Variable arguments `stdarg.h` se aate hain, aur ye compiler ka hissa hai, libc ka nahi. Isliye freestanding mein bhi kaam karta hai.
- Panic mein sabse pehle `cli` kyun: taaki tootte kernel pe koi interrupt handler na chale.
- Framebuffer `0xffff8000fd000000` pe hai, jo kernel (`0xffffffff80000000`) se alag area hai. Ye Limine ka HHDM (higher half direct map) hai, Stage 5 mein detail mein dekhenge.
