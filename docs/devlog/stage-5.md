# Stage 5: Memory management (PMM + VMM + heap)

**Done test:** ✅
- 1000 pages allocate + free, bina leak (`make test T=pmm`):
  ```
  pmm: 130636 pages free before
  pmm: allocated 1000 pages, 129636 free now (diff 1000)
  pmm: 64 contiguous pages at phys 0x0000000000100000
  pmm: 130636 pages free after, leak = 0 pages
  ```
- Galat address pe page fault handler address print kare (`make test T=vmm_unmapped`):
  ```
  wrote to 0xffffe00000005000, unmapping it and writing again...
  Page fault at RIP=0xffffffff8000....: not-present page on write, kernel mode, address CR2=0xffffe00000005000
  ```

Poora suite: **23/23 pass**.

## Kya banaya

### 1. PMM: physical pages (`mm/pmm.c`)

- **Bitmap**: har 4 KiB physical page ke liye ek bit (1 = used). 511 MiB RAM ke liye 16 KiB ka bitmap, jo pehle usable region mein `phys 0x7e000` pe rakha hai aur HHDM se access hota hai.
- Init ka order: sab "used" mark karo → sirf `USABLE` regions free karo → bitmap ke apne pages aur page 0 wapas used. Page 0 kabhi nahi dete, isliye `0` ka matlab "fail" ho sakta hai.
- **Bootloader-reclaimable memory abhi reclaim nahi ki.** Isme abhi bhi hamara boot stack, Limine ka memory map aur module structs hain. Ye sirf ~1 MiB hai.
- `pmm_alloc_page` (search hint ke saath), `pmm_alloc_pages(n)` (contiguous), `pmm_alloc_page_zeroed`, aur `pmm_free_page(s)`. Free karte waqt double free aur range check hota hai, galti pe panic.
- Saare operations interrupts off karke hote hain (Stage 6 tasks ke liye).

### 2. VMM: 4-level paging (`mm/vmm.c`)

```
virtual address:  [PML4 9 bit][PDPT 9][PD 9][PT 9][offset 12]
CR3 ──► PML4 ──► PDPT ──► PD ──► PT ──► 4 KiB page      (sab PHYSICAL addresses)
```

- `walk()` har level pe entry follow karta hai. `create = true` ho to naya zeroed table PMM se banata hai. Tables ko CPU physical address se dekhta hai, aur kernel unhe HHDM ke virtual address se.
- `vmm_map_page / vmm_unmap_page (invlpg ke saath) / vmm_translate / vmm_get_flags / vmm_alloc_pages / vmm_free_pages`.
- Intermediate entries permissive hain (present + write, aur user mapping ho to user bhi). Asli permission aakhri PTE tay karta hai.
- **`vmm_init` apne page tables banata hai aur CR3 switch karta hai:**
  1. `EFER.NXE` (NX bit chalne ke liye) aur `CR0.WP` (ring 0 bhi read-only pages pe likh na sake).
  2. Kernel half ke saare 256 PDPTs pehle se bana diye (PML4 entries 256-511). Stage 7 mein har process ka PML4 yahi entries copy karega, to baad mein jo bhi kernel mapping (heap, stacks) add hogi, wo har address space mein apne aap dikhegi.
  3. Kernel image linker symbols (`__kernel_text_start` ...) se, har section alag permission ke saath:

     | Section | Flags | Matlab |
     |---|---|---|
     | `.text` | `0x21` | read + execute |
     | `.rodata` | `NX \| present` | sirf read |
     | `.data/.bss` | `NX \| write \| present` | read/write, execute nahi |

  4. HHDM: usable, bootloader-reclaimable, kernel, ACPI aur framebuffer regions `phys + hhdm_offset` pe RW+NX map kiye (515 MiB). `reserved` regions skip kiye, kyunki QEMU mein ek 12 GiB ka khaali reserved region hai, aur use 4 KiB pages se map karne mein ~24 MiB ke page tables lagte.
  5. `CR3: 0x1ff98000 (Limine) → 0x82000 (hamara)`.

- **Virtual memory layout:**

  | Range | Kya |
  |---|---|
  | `0x0000000000000000 - 0x00007fffffffffff` | user space (Stage 7) |
  | `0xffff800000000000 - ...` | HHDM |
  | `0xffffc00000000000` (1 GiB) | kernel heap |
  | `0xffffd00000000000` | task stacks + guard pages (Stage 6) |
  | `0xffffe00000000000` | test scratch area |
  | `0xffffffff80000000` | kernel image |

### 3. Heap (`mm/heap.c`)

Guide ke order mein do versions bane (dono git history mein hain):

1. **Bump allocator** (`aa0bfb8`): ek pointer sirf aage badhta hai aur `kfree` kuch nahi karta. Simple hai, par sab leak hota hai.
2. **Free-list allocator** (`17b57f6`):
   - Blocks address order mein ek ke baad ek rakhe hain. Har block ke aage ek 32-byte header hai (`magic, size, prev, next`). 32 bytes isliye, taaki payload 16-byte aligned rahe (`_Static_assert` se check hota hai).
   - `kmalloc`: first fit. Bacha hissa bada ho to block **split** hota hai. Kuch fit na ho to heap ke end pe pages map karke heap **grow** hota hai.
   - `kfree`: block free mark hota hai, aur dono taraf ke free padosi blocks se **coalesce** hota hai.
   - **Safety:** magic numbers se double free aur corrupted header pakde jaate hain. Free ki hui memory `0xDD` se bhar di jaati hai, taaki use-after-free jaldi dikhe. `heap_check()` poori list verify karta hai: koi gap/overlap na ho, links sahi hon, aur do free blocks saath mein na hon.

## Tests

| Test | Kya check karta hai |
|---|---|
| `pmm` | 1000 pages: unique (har page pe pattern), aligned, free ke baad `leak = 0` (Done) |
| `pmm_double_free` | `pmm_free: double free of phys ...` panic |
| `vmm` | Kernel section flags. 1000 pages ko naye virtual address pe map karke likha, aur HHDM alias se wahi data padha. Unmap ke baad translate fail ho, aur na PMM leak ho na table leak |
| `vmm_unmapped` | Unmap ke baad likhne pe PF, aur CR2 mein exact address (Done) |
| `nx` | NX page mein `ret` (0xC3) call karne pe `protection violation on instruction fetch` |
| `wp` | `.rodata` pe likhne pe `protection violation on write` (CR0.WP ki wajah se ring 0 mein bhi) |
| `null` | NULL padhne pe `address CR2=0x0000000000000000` |
| `heap` | 5000 random kmalloc/kfree (1 B - 64 KiB), overlap check, alignment. Sab free hone pe `1 free block`, `heap_check ok`, poore block ka ek kmalloc bina heap grow kiye, aur 3 MiB kmalloc se heap grow hota hai |
| `heap_double_free` | `kfree: double free` panic |
| `heap_overflow` | 32-byte block mein 64 bytes likhne se agla header `0x58585858` ("XXXX") ho jaata hai, jo `kfree` pakad leta hai |

## Bugs / issues

Is stage mein koi crash ya triple fault nahi aaya. CR3 switch pehli koshish mein chal gaya. Phir bhi kuch baatein note karne layak hain:

- **Physical vs virtual confusion (guide ka common bug) se kaise bache:** naming rule (`phys_`/`virt_`) aur do alag helpers. `phys_to_virt()` sirf HHDM ke liye hai, aur `hhdm_virt_to_phys()` kernel image/heap addresses pe galat answer dega, isliye kernel image ka physical address `virt - kernel_virt_base + kernel_phys_base` se nikalte hain. Limine ke `framebuffer->address` aur module addresses pehle se *virtual* (HHDM) hain.
- **TLB:** `vmm_unmapped` test exactly isliye hai. Agar `vmm_unmap_page` `invlpg` na karta, to CPU ke TLB mein purani translation bachi rehti aur likhna chupchaap "kaam" kar jaata. Map karte waqt `invlpg` ki zaroorat nahi, kyunki not-present entries TLB mein cache nahi hoti.
- **12 GiB reserved region:** memory map mein `fd00000000-10000000000 reserved` hai. Ise HHDM mein map karne ki koshish karte to page tables hi ~24 MiB kha jaate. Isliye sirf kaam ki region types map kiye.
- **Khaali page tables free nahi hote.** `vmm` test mein isliye ek warm-up round hai: pehli baar map karne se jo tables bane, wo baad mein bhi rehte hain. Ye leak nahi hai, kyunki agli mapping wahi tables reuse karti hai (`table pages 522 -> 522`).

## Kya seekha

- PMM aur VMM alag cheezein hain. PMM batata hai ki RAM ka kaunsa page free hai. VMM batata hai ki kaunsa virtual address kis physical page pe jaata hai.
- Page table khud physical memory mein rehta hai. Kernel use padhne/likhne ke liye HHDM ka use karta hai.
- Permissions (W, NX, U) page table entries mein hote hain, aur galat access pe CPU #PF deta hai, jiske error code bits batate hain ki kya galat hua.
- Bump vs free-list: free-list mein fragmentation hota hai (random test ke beech 82 free blocks the), aur coalescing usko wapas jod deta hai (aakhir mein 1 block).
