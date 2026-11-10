# Stage 8: Filesystem

**Done test:** `/hello.txt` khul ke padha jaaye. ✅ (`make test T=user_cat`, ek user program syscalls se padhta hai)

```
[process 'cat_hello' started: pid 2, 530 bytes at 0x0000000000400000]
cat_hello: opened /hello.txt as fd 3
Hello from the initrd! This file was read through the VFS.
cat_hello: error checks ok (EBADF, ENOENT, EFAULT)
[process 'cat_hello' (pid 2) exited with code 0]
heap allocation leak after the process: 0 (open file it did not close)
```

Guide ka "baad mein" wala hissa bhi kiya: ATA PIO disk driver aur FAT32 (read-only), jo `/disk` pe mount hota hai. Poora suite: **40/40 pass**.

## File tree (boot ke baad)

```
/
  docs/about.txt          ┐
  etc/motd                ├ initrd (tarfs, read only)
  hello.txt               ┘
  dev/console, dev/null     devfs
  disk/                     FAT32 on ATA drive 0
    docs/nested-file.txt
    readme.txt
    welcome-to-the-disk.txt (long file name)
    numbers.txt             (8893 bytes = 18 clusters)
```

## Kya banaya

### VFS (`fs/vfs.c`)

- `struct vfs_node`: name, type (FILE/DIR/DEV), size, `ops`, filesystem ka private data, aur parent/children list.
- `struct vfs_ops`: `read, write, lookup, readdir`. Har filesystem apne functions deta hai, aur VFS ko farq nahi padta ki file kahan hai.
- In-memory directories ke liye generic ops hain (children list), jinhe tarfs aur devfs reuse karte hain.
- Path walk: absolute paths, `.`, `..`, aur repeated `/`. Aisi galtiyan bhi pakdi jaati hain jaise `file/x` (file ke andar kuch) aur bahut lamba naam.
- `struct file` = node + offset + flags + refcount. fds 0/1/2 ek hi open file share karte hain.
- Errors: `ENOENT`, `EROFS` (read-only file pe write), `EISDIR`, `EBADF`, `EMFILE`.

### File descriptors aur syscalls

- Har task mein `struct file *fds[16]`. User process shuru hone pe fd 0/1/2 = `/dev/console`.
- `open(path, flags)` sabse chhota free fd deta hai. `close(fd)`. `read`/`write` ab fd table → VFS se jaate hain (Stage 7 mein console hardcoded tha).
- User ka path string page by page check hoke kernel buffer mein copy hota hai (`copy_user_string`). Isse galat pointer pe `-EFAULT` milta hai, aur string jo kisi unmapped page mein chali jaaye wo bhi pakdi jaati hai.
- `task_exit` saari open files band karta hai. `cat_hello` jaan-boojh ke `/etc/motd` khula chhod ke exit karta hai, aur heap leak 0 rehta hai.
- **Race fix:** pehle `task_create_user` task ko turant run queue mein daal deta tha, jabki fds abhi set nahi hue the. Ab user task "stopped" banta hai, aur loader setup karke `task_start()` karta hai.

### initrd + tarfs (`fs/tarfs.c`)

- Makefile `initrd/` ko `tar --format=ustar --owner=0 --sort=name` se pack karta hai. Limine `module_path: boot():/boot/initrd.tar` se use memory mein load karta hai, aur `boot_info.modules` mein uska HHDM address aata hai.
- USTAR: 512-byte blocks. Header ke numbers **ASCII octal** mein hote hain. Checksum = header ke saare bytes ka sum, checksum field ko 8 spaces maan ke. Path = `prefix + "/" + name`. Type `'0'` = file, `'5'` = dir. Aakhir mein zero blocks hote hain.
- File data copy nahi hota: node ka `priv` seedha archive ke andar point karta hai.

### devfs (`fs/devfs.c`)

- `/dev/console`: read ke liye input queue (blocking), write ke liye screen + serial.
- `/dev/null`.

### ATA PIO driver (`drivers/ata.c`)

- Primary bus ports `0x1F0-0x1F7`, control `0x3F6` (nIEN = koi IRQ nahi, hum poll karte hain).
- `IDENTIFY` (0xEC): 256 words aate hain. Words 60-61 mein LBA28 sector count hota hai, aur words 27-46 mein model name (har word mein bytes ulte order mein).
- Read (0x20) / write (0x30): drive select + LBA bytes + count + command, phir har sector ke liye BSY clear aur DRQ ka wait, phir 256 × `inw`/`outw`. Write ke baad cache flush (0xE7).
- Drive select ke baad 400 ns delay ke liye alt-status port ko 4 baar padhte hain.

### FAT32 (`fs/fat32.c`, read only)

- BPB se: bytes/sector, sectors/cluster, reserved sectors, FAT size, root cluster. `first_data_sector = reserved + num_fats × fat_size`.
- Cluster N ka sector = `first_data_sector + (N − 2) × sectors_per_cluster`.
- **FAT = linked list:** `fat[N]` mein agla cluster hota hai, aur `≥ 0x0FFFFFF8` matlab end. Ek FAT sector cache hota hai.
- Directory = 32-byte entries. `0x00` = aage kuch nahi, `0xE5` = deleted, attr `0x0F` = **LFN** entry.
- **Long file names:** LFN entries 8.3 entry se *pehle* aati hain, aakhri hissa sabse pehle. Har entry mein 13 UCS-2 characters 3 hisson mein hote hain (5 + 6 + 2). 8.3 naam ka checksum match hone pe hi long name use hota hai, warna `readme.txt` jaisa 8.3 naam (lowercase).
- Directories **lazily** padhi jaati hain: pehli `lookup`/`readdir` pe saari entries VFS child nodes ban jaati hain (`children_loaded`).
- File read: offset tak chain follow karo, phir cluster by cluster copy karo (cluster ke beech se shuru hone wale reads bhi).

### Disk images (Makefile)

- `build/disk.img`: 64 MiB, `mformat -F` (FAT32, poori image ek volume, koi partition table nahi), aur `mcopy -s disk/*`, saath mein `seq 1 2000 > numbers.txt`.
- `build/scratch.img`: 1 MiB zeros, raw sector write test ke liye.
- QEMU: IDE primary master/slave, `snapshot=on` (image files kabhi nahi badalti).

## Tests

| Test | Kya |
|---|---|
| `vfs` | poora tree, kernel se `/hello.txt`, `//docs/../etc/./motd` == `/etc/motd`, ENOENT/EROFS/EISDIR, 7-byte tukdon mein read, `/dev/null` |
| `user_cat` | **Done**: user program `/hello.txt` padhta hai, EBADF/ENOENT/EFAULT, aur bina close kiye exit pe bhi leak 0 |
| `disk` | LFN naam, nested dir, `numbers.txt` ki 2000 lines order mein (18-cluster chain), cluster boundary ke aar-paar read |
| `ata` | scratch disk pe 4 sectors write → read → same data, disk ke end ke baad read refuse |

## Bugs / issues

1. **Disk lagate hi boot band, serial pe kuch nahi.** Test `timeout waiting for 'Hello from MyOS'` hua.
   **Root cause:** IDE primary master pe hard disk lagne ke baad SeaBIOS pehle usi se boot karne ki koshish karta hai. Us FAT32 disk pe koi boot loader nahi hai, isliye CD tak baat pahunchti hi nahi.
   **Fix:** `-boot d` (CD-ROM pehle). limine-c-template ke `run-bios` target mein bhi yahi hai.
2. **`vfs` test "fail" hua, lekin output sahi tha.** Runner sirf aakhri 3000 characters dikhata hai, isliye log kata hua laga. Asli farq ye tha ki devices bhi `(0 bytes)` print ho rahe the. Tree mein `(device)` kar diya.
3. **Volume label `'MYOSDISK   '`:** FAT labels spaces se padded hote hain, isliye trim kiya.

## Kya seekha

- VFS ek "interface" hai: tar, devices aur FAT, teeno ek hi `open/read` ke peeche chhupe hain.
- fd sirf ek index hai: process ki table → `struct file` (offset) → node → filesystem ops.
- PIO mein CPU khud har word copy karta hai, simple hai par slow. Asli drivers DMA use karte hain.
- FAT ka naam "File Allocation **Table**" isliye hai ki poori file ek table mein bani linked list hai.
