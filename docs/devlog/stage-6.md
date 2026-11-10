# Stage 6: Multitasking

**Done test:** threads "A" aur "B" baari-baari print karein, aur `sleep(ms)` kaam kare. ✅

```
yield: A1 B1 A2 B2 A3 B3 A4 B4 A5 B5        (make test T=threads)
sleep: A1 B1 A2 B2 A3 B3 A4 B4 A5 B5
slept 100 ms (asked 100)                     (make test T=sleep)
slept 200 ms (asked 200)
slept 300 ms (asked 300)
wake order: 100 200 300
main slept 1000 ms: 100 ticks                (runner ne real time ~1 s bhi check kiya)
```

Normal boot (`make run`) mein bhi A/B demo chalta hai, aur status bar `tasks: 3  switches: 27  uptime: 5s` dikhata hai. Poora suite: **30/30 pass**.

## Kya banaya

### `struct task` (`sched/task.h`)

`id, name, state, saved_rsp, virt_kstack_bottom/top, as (page table), wake_tick, ticks_used, exit_code, next, all_next`

States: `READY → RUNNING → (READY | SLEEPING | BLOCKED | ZOMBIE)`.

### Context switch (`arch/x86_64/switch.asm`)

```
switch_context(&old->saved_rsp, new->saved_rsp):
    push rbx, rbp, r12, r13, r14, r15     ; callee-saved
    mov [rdi], rsp                         ; old task ka stack pointer save
    mov rsp, rsi                           ; ab naye task ke stack pe
    pop r15 ... rbx
    ret                                    ; naya task wahan se chalta hai jahan usne switch_context call kiya tha
```

- **Sirf 6 registers kyun:** `switch_context` ek normal C function ki tarah call hota hai, aur ABI ke hisaab se caller pehle se maan ke chalta hai ki `rax, rcx, rdx, rsi, rdi, r8-r11` badal jaayenge. RIP stack pe return address ki shakal mein already hai.
- **Naya task:** `task_create` stack pe ek nakli "switch frame" banata hai: return address = `task_trampoline`, `r12 = fn`, `r13 = arg`. Pehla switch `ret` karke trampoline pe pahunchta hai, jo `task_entry(fn, arg)` call karta hai. Wo `sti` karta hai, `fn(arg)` chalata hai, phir `task_exit(0)`.
- **Alignment:** trampoline pe `rsp` = stack top (16 ka multiple), isliye `call task_entry` ABI ke hisaab se sahi hai.

### Scheduler (`sched/sched.c`)

- **Round robin:** FIFO run queue. PIT tick (100 Hz) time slice ghatata hai. 5 ticks = 50 ms ke baad `schedule()` chalta hai: current task queue ke peeche jaata hai aur aage wala task CPU leta hai.
- Timer IRQ handler ke **andar** se switch hota hai. Preempted task ka interrupt frame uske apne kernel stack pe rehta hai, aur jab wo dobara chunaa jaata hai, to `irq_dispatch → isr_common → iretq` se wapas wahin pahunch jaata hai. Isi wajah se Stage 4 mein EOI handler se *pehle* bheja tha.
- **Idle task:** run queue mein nahi rehta. Jab koi ready nahi hota tab chalta hai, `sti; hlt` ke saath.
- **kmain = task 0:** Limine ke boot stack pe chalta hai, isliye kabhi exit nahi karta (status bar update karta rehta hai).
- **Kernel stacks + guard pages:** har stack 16 KiB ka hai aur 64 KiB ke virtual slot (`0xffffd00000000000 + i*64K`) ke top pe rehta hai. Neeche ke 48 KiB kabhi map nahi hote. Stack overflow hone pe guard page pe page fault aata hai, aur kisi doosre task ki memory chupchaap kharab nahi hoti.
- **Zombie reaping:** koi task apna hi stack free nahi kar sakta, kyunki wo usi pe chal raha hota hai. `task_exit` sirf ZOMBIE mark karke switch karta hai. Agla task `finish_switch()` mein uska stack aur struct free karta hai.
- **sleep(ms):** sleep list `wake_tick` ke hisaab se sorted hai. Tick handler list ke shuru se jaagne wale tasks nikalta hai.
- **Wait queues:** `wait_queue_sleep` (interrupts off ho tab call karna hai) aur `wait_queue_wake_all`. `input_getc` ab task ko block karta hai (keyboard/serial IRQ use jagata hai), `hlt` pe ghoomta nahi.

### Sync

- **"Interrupts band karke critical section":** single CPU pe saare scheduler, PMM aur heap data `cpu_irq_save/restore` ke andar badalte hain.
- **Spinlock** (`lib/spinlock.h`): pehle interrupts off, phir `xchg` se atomic lock. Single CPU pe agar lock pehle se pakda hua mile, to holder kabhi chal hi nahi sakta (interrupts off hain), isliye spin karne ki jagah **turant panic "deadlock"**.

## Tests

| Test | Result |
|---|---|
| `threads` | yield aur sleep, dono versions mein `A1 B1 A2 B2 ... A5 B5` |
| `preempt` | do tasks jo kabhi CPU nahi chhodte, dono chalte hain: 1 s mein ~26 switches |
| `sleep` | 300/100/200 ms ke sleepers 100 → 200 → 300 order mein jaage, har ek exact time pe |
| `race` | lock ke bina `lost 4211173 updates`, spinlock ke saath `lost 0` |
| `deadlock` | same spinlock do baar lene pe panic |
| `stack_overflow` | infinite recursion → **double fault** (IST1 pe) → `kernel stack overflow: guard page hit` |
| `task_leak` | 100 tasks create aur finish hue: page leak 0, heap leak 0 |

## Bugs / issues

1. **A/B baari-baari nahi chal rahe the:** pehle demo output `A1 B1 B2 A2 A3 B3 B4 A4 ...` tha.
   **Root cause:** dono threads same tick pe jaagte the, aur sleep list mein naya sleeper *aage* lagta tha (LIFO). Har round mein jaagne ka order ulat jaata tha.
   **Fix:** sleep list `wake_tick` ke hisaab se sorted rakhi, aur barabar `wake_tick` wala naya task purane ke *baad* lagta hai (FIFO). Ab order fair hai.
2. **`race` test ne lock ke bina bhi `lost 0` dikhaya,** to wo kuch sabit nahi kar raha tha.
   **Root cause:** dono racers ka saara kaam (200000 loops) ek hi 50 ms slice se kam mein khatam ho jaata tha, aur read aur write ke beech kabhi preemption hua hi nahi.
   **Fix:** test ko time-based banaya (1 second, ~20 preemptions), aur har racer apne increments khud bhi ginta hai. Lost updates itne zyada (4.2M) isliye hain ki ek preemption = doosre task ke poore slice ke saare increments mit jaana.
3. **`task_leak` mein 6 pages "leak".** 100 stacks naye virtual slots mein gaye, aur un slots ke page tables bane jo free nahi hote (Stage 5 jaisa hi case). Asli leak nahi tha. Fix: warm-up round bhi 100 tasks ka (`warm-up: 3 new page-table pages`), uske baad leak 0.
4. **Heap tests fail hue** kyunki ab boot pe 2 allocations pehle se hoti hain (kmain aur idle ke `struct task`). Tests ko absolute ki jagah "leaked X" (relative) check pe badla.
5. **gcc `-Winfinite-recursion` warning** stack overflow test pe aayi. Fix: ek `volatile` flag, jo hamesha true hai par compiler ko pata nahi.

## Stack overflow → double fault (Stage 3 ka IST yahan kaam aaya)

Jab recursion guard page pe pahunchta hai, CPU #PF deliver karne ke liye exception frame **usi** toote stack pe push karna chahta hai. Ye bhi fault karta hai, aur fault deliver karte waqt fault = **double fault**. #DF IST1 ke alag stack pe chalta hai, isliye handler chal paata hai aur `CR2` (guard page address) dekh ke batata hai ki ye stack overflow tha. Agar IST na hota, to yahan triple fault aur reboot hota.
