# Stage 4: Timer + keyboard

**Done test:** keyboard pe type karo to screen pe aaye, aur tick counter badhta dikhe. ✅

Headless check: QEMU `sendkey` se `hello from keyboard!` ↵ `MyOS rocks` type kiya. Screendump mein text, cursor, aur status bar mein `ticks: 800  uptime: 8s` dikha. Automatic tests:

```
ok    timer          4.0s     ticks=100 -> ticks=300 real time mein ~2 s; pit_sleep_ms(500) = 51 ticks
ok    keyboard       2.5s     sendkey "Hello, Worlx<BS>d! (1+1=2)" -> got line: 'Hello, World! (1+1=2)'
ok    serialin       0.6s     serial "echo from serial\r" -> got line: 'echo from serial'
13/13 passed
```

## Kya banaya

| File | Kaam |
|---|---|
| `drivers/pic.c/.h` | 8259 remap: master → vectors 32-39, slave → 40-47 (ICW1-ICW4). Shuru mein sab masked, sirf cascade (IRQ 2) khula. `pic_unmask/mask`, `pic_send_eoi`, `pic_is_spurious` |
| `arch/x86_64/irq.c/.h` | Vectors 32-47 → `irq_dispatch`: spurious check → EOI → driver ka handler. `irq_register(irq, fn)` handler lagata hai aur line unmask karta hai. `irq_count()` debugging ke liye. Saath mein LAPIC disable (neeche bug #1 dekho) |
| `drivers/pit.c/.h` | Channel 0, mode 2 (rate generator), divisor `1193182 / 100 = 11931` → 100.007 Hz. `pit_ticks`, `pit_uptime_ms`, `pit_sleep_ms` (`hlt` ke saath), aur tick callback (Stage 6 scheduler ke liye) |
| `drivers/keyboard.c/.h` | PS/2 port `0x60`, scancode set 1 → ASCII (US layout). Shift, Caps Lock, Ctrl (Ctrl+C = 3), aur `0xE0` extended prefix. Key release (bit 7) pe koi character nahi |
| `drivers/input.c/.h` | Ek common input queue (`lib/ringbuf.h`). Keyboard aur serial dono isme push karte hain. `input_getc()` `sti; hlt` ke saath wait karta hai |
| `drivers/serial.c` | Receive interrupt (IER bit 0, IRQ 4). `\r` → `\n`, DEL → `\b`. Isse `make run` wale terminal se bhi type kar sakte ho, aur headless tests bhi serial se input bhej sakte hain |
| `drivers/console.c` | Row 0 pe status bar (`console_set_status`), text uske neeche scroll hota hai. Cursor bar, aur `\b \t \f` handling |
| `lib/kprintf.c` | Formatter ab `struct out` ke through likhta hai, isse `ksnprintf` bhi wahi code use karta hai. Print ke dauran interrupts off rehte hain, taaki do messages beech mein mix na hon |
| `kmain.c` | `irq_init → pit_init → keyboard_init → serial_enable_rx_irq → sti`, phir echo loop jo har second status bar update karta hai |

## Design decisions

- **EOI handler se pehle.** Stage 6 mein timer handler doosre task pe switch karega aur der se wapas aayega. Agar EOI handler ke baad bhejte, to PIC tab tak naya IRQ nahi bhejta. Nesting ka darr nahi hai, kyunki interrupt gate IF=0 rakhta hai jab tak `iretq` na ho.
- **Spurious IRQ 7/15:** PIC ka ISR register padh ke check karte hain. Spurious IRQ 7 ko EOI nahi milta. Spurious IRQ 15 pe sirf master ko EOI milta hai, kyunki cascade line pe signal asli tha.
- **`sti; hlt` ek saath.** `sti` ka effect agli instruction ke *baad* hota hai. Isliye "queue khaali hai, check kiya" aur "so gaye" ke beech koi interrupt nahi aa sakta. Agar `sti` alag hota aur key exactly beech mein aati, to hum agli key tak soye rehte (lost wakeup).
- **Ring buffer, lock-free:** ek writer (IRQ) aur ek reader. `head`/`tail` sirf badhte hain, aur unsigned overflow ke baad bhi `head - tail` sahi count deta hai. Reader pop karte waqt interrupts band rakhta hai.
- **IRQ handler mein `kprintf` nahi.** Keyboard handler sirf queue mein daalta hai, aur screen pe echo main loop karta hai.

## Bugs / issues

1. **Koi IRQ CPU tak nahi pahunch raha tha.** Timer, keyboard, serial teeno tests timeout ho gaye, jabki exceptions (Stage 3) kaam kar rahe the.
   - QEMU monitor `info pic`: `pic0: irr=01 imr=e8 irq_base=20`. Matlab remap sahi tha, IRQ 0/1/4 unmasked the, aur timer IRQ *pending* tha.
   - `info registers`: `RFL=0x297`, matlab IF=1 tha.
   - `info lapic`: `LVT0 0x00018700 ... masked ExtINT` aur `SPIV 0x1ff APIC enabled`.
   - **Root cause:** modern x86 pe 8259 ka output Local APIC ke LINT0 pin pe "ExtINT" ki tarah aata hai. Limine ne LAPIC enabled chhoda, lekin LINT0 **masked** tha (SMP setup ke INIT/SIPI ke baad, `ICR ... assert all`). Is wajah se PIC ka IRQ beech mein hi atak jaata tha.
   - **Fix:** `IA32_APIC_BASE` MSR (`0x1B`) ka bit 11 clear karke LAPIC globally disable kiya. Tab LINT0 purane INTR pin ki tarah kaam karta hai (Intel SDM Vol. 3, "Local APIC State After It Has Been Disabled").
   - Doosra option tha LINT0 ko unmask karna (MMIO `0xFEE00000`), lekin wo address memory map mein nahi hai, isliye Limine ke HHDM mein mapped nahi hai, aur abhi apna VMM bhi nahi hai. APIC guide mein "baad mein" ka kaam hai. Jab APIC aayega, to LAPIC ko MMIO map karke wapas enable karna hoga.
2. **Experiment: EOI hatane pe kya hota hai (guide ka common bug).** `pic_send_eoi` comment karke `timer` test chalaya: ticks kabhi 100 tak nahi pahunche, test timeout hua. PIC pehla IRQ "in service" maan ke rakhta hai aur usse kam ya barabar priority wala koi IRQ nahi bhejta. Code restore kar diya.

## Kya seekha

- IRQ ka raasta: device → 8259 → (LAPIC LINT0) → CPU → IDT vector → stub → C handler → EOI.
- Remap kyun: BIOS default mein IRQ 0-7 vectors 8-15 pe hote hain. Bina remap ke timer tick ek "double fault" (vector 8) jaisa dikhta.
- PIT ka frequency formula: `1193182 / divisor`. 100 Hz pe ek tick = 10 ms.
- Scancode ≠ ASCII. Keyboard sirf "kaunsi key dabi/chhoot gayi" batata hai. Shift/Caps ka state driver ko khud yaad rakhna padta hai.
