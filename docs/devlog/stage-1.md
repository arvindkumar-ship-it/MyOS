# Stage 1: Boot + "Hello"

**Done test:** "Hello from MyOS" on screen and on the serial terminal. ✅ Passed in QEMU.

## Kya banaya

| File | Kaam |
|---|---|
| `kernel/arch/x86_64/io.h` | `outb` / `inb`: inline asm for the x86 I/O port space |
| `kernel/drivers/serial.c/.h` | COM1 (`0x3F8`) 16550 UART: 38400 baud 8N1, FIFO, loopback self-test, polling `serial_putc` |
| `kernel/drivers/framebuffer.c/.h` | `struct framebuffer` (copy of what we need from Limine), `fb_rgb`, `fb_put_pixel`, `fb_fill_rect`, `fb_clear`, `fb_scroll_up` |
| `kernel/drivers/console.c/.h` | Text on the framebuffer: PSF1 header check, glyph drawing, cursor, `\n`, line wrap, scroll |
| `kernel/font.asm` + `assets/font.psf` | 8x16 VGA font (public domain) embedded into `.rodata` with NASM `incbin` |
| `kernel/kmain.c` | Order: serial → Limine checks → framebuffer → console → "Hello" |
| `Makefile` | Also builds `kernel/**/*.asm` (objects named `*.asm.o`) |

Boot log on serial:

```
Hello from MyOS
framebuffer ready
console ready
```

## Design decisions

- **Serial pehle init hota hai.** Uske baad har step serial pe log karta hai, taaki framebuffer fail ho tab bhi error dikhe.
- **`serial_putc` loopback test fail hone pe kuch nahi karta.** Agar UART na ho to "transmit empty" bit kabhi set nahi hoga, aur bina is check ke kernel hamesha ke liye wahi atak jaata.
- **Font kernel ke andar embed kiya.** Abhi filesystem nahi hai, to `incbin` se font build time pe hi kernel image mein chala jaata hai.
- **Unicode table ignore ki.** Is font mein glyph index = ASCII code hai. Ye maine glyph 65 ko bits mein print karke check kiya, wahan `A` hai.
- **`fb_put_pixel` bounds check karta hai.** Galat coordinate se framebuffer ke bahar memory corrupt nahi hogi.
- **`pitch` use kiya, `width * 4` nahi.** Ek row mein padding bytes ho sakte hain.

## Bugs / issues

- Stage 1 mein koi crash nahi aaya. Scroll code ko temporary 60 lines print karke test kiya. Screen 50 rows (800 / 16) ki hai, isliye pehli lines upar se nikal gayi, jaisa expected tha.
- (Setup mein) kernel ELF ka naam `build/kernel` tha, jo objects wale `build/kernel/` folder se clash kar raha tha, isliye link fail hua. Naam `build/myos.elf` kar diya.

## Kya seekha

- x86 pe do address spaces hain: memory (`mov`) aur I/O ports (`in`/`out`).
- UART registers offsets pe hote hain. DLAB bit registers 0/1 ka matlab badal deta hai (data ↔ baud divisor).
- Framebuffer bas memory hai: pixel likho, screen badal jaati hai. Limine ne use pehle se virtual address pe map kar diya hai.
- Bitmap font: 1 bit = 1 pixel, 1 byte = glyph ki ek row.
