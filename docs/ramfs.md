# First root filesystem: RAMFS

**Current revision:** [kernel heap buffers](kernel-heap.md) replace the original
one-page-per-file storage. Three boot files now share two pages (544 bytes of
live buffer capacity). `fsinfo` reports actual distinct pages and heap buffers.
The [new Pi capture](../proofs/pi5-heap-page-reclamation-2026-11-12.txt) confirms
that baseline and heap tests. Heap-backed edit/isolation tests pass natively
and in QEMU; earlier RAMFS file commands also have hardware evidence below.

The kernel now builds a small root filesystem in RAM and exposes it through
the serial monitor. **The original RAMFS revision now has physical Pi evidence**:
the user's screenshot shows file creation, cat, cwd navigation and MB/MiB memory
details. A later physical USB-boot capture also confirms the expanded root
directories. Its SD attachment fails as expected without an SD card; that
capture does not verify mounting the pendrive. See [storage](storage.md) and
the next [USB discovery phase](usb.md).

The root `/` is a logical directory. It does not have to be an SD partition.
Our boot firmware reads the FAT32 boot partition (USB in the user's setup) to load the kernel and DTB;
the kernel then creates this separate RAM filesystem. The new storage revision
can attempt a read-only FAT32 mount at `/boot` through `storageinit`. RAMFS
commands still do not write SD files. See [storage.md](storage.md) for that path.

## Initial tree

```text
/
├── README.txt            Kernel-supplied introduction and command examples
├── etc/
│   ├── hostname          ouros
│   └── board             Current board build name, e.g. rpi5-header
├── home/
│   └── user/
└── tmp/
```

That tree describes the physically tested RAMFS baseline. The new storage
revision additionally creates `/boot`, `/dev`, `/proc`, `/sys`, `/var/log` and
`/mnt`, for twelve directories total including root. The empty `/dev`, `/proc`
and `/sys` directories reserve names; device nodes and pseudo-files are not yet
implemented. Three nonempty files are still created at boot.
Each nonempty file now owns a 16..4096-byte heap buffer; small files share
physical pages. Therefore `meminfo` initially shows **two allocated pages**,
and `fsinfo` shows three buffers with 544 bytes rounded capacity, before manual allocations.
The allocator bitmap, stack and kernel metadata are separately reserved memory.

**Files disappear on reset or power loss.** Boot recreates the initial files
from the compiled kernel. This is an interactive kernel monitor, not an EL0
shell, and these files are not executable applications.

## Commands

| Command | Supported behaviour |
| --- | --- |
| `pwd` | Print the current absolute directory |
| `ls [path]` | List a directory, or show one file; default is current directory |
| `cd [path]` | Change directory; with no path, return to `/` |
| `cat <path>` | Display file contents |
| `mkdir <path>` | Create one directory; parent must already exist |
| `touch <path>` | Create an empty file, or preserve an existing regular file |
| `write <path> <text>` | Create or replace a file's contents with the rest of the line |
| `write <path>` | Truncate/create an empty file |
| `rm <path>` | Delete one regular file and free its heap buffer; empty pages return to PMM |
| `rmdir <path>` | Delete an empty directory; root/current directory are protected |
| `fsinfo` | Show volatile status, file/directory/unique-page/buffer counts and limits |

Paths are case-sensitive. Absolute paths, relative paths, `.` and `..` work;
`..` at root stays at root. Repeated slashes are accepted. A regular file cannot
be traversed as a directory, including through `file/..` or a trailing slash.

This first command parser accepts a single path argument. Filenames use ASCII
letters, digits, dot, underscore and hyphen; spaces in names, wildcard expansion,
quotes, redirection, pipelines and shell operators are not implemented. Spaces
after the path in `write` separate it from the text; later spaces belong to the
contents. Quotation marks in the text are literal characters, not shell syntax.
`cat` adds a display newline if nonempty text does not end in one; it does not
change the file contents. `ls` prefixes directories with `d` and files with `f`
plus their exact byte length.

## Test on the Pi

Build or use the new header image at the usual project-local output path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Image: `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`.
After writing the intended test card, retain the same CP2102 wiring and 115200
8N1/no-flow-control settings. Look for `Root filesystem ready: RAMFS at / (volatile).`

Run one command per line:

```text
meminfo
fsinfo
pwd
ls /
cat /README.txt
cat /etc/board
cd /home/user
mkdir notes
cd notes
write hello.txt Hello from OurOS
ls
cat hello.txt
pwd
cd ..
rm notes/hello.txt
rmdir notes
cd /
fsinfo
meminfo
```

The two fsinfo results and the free-page counts should match after cleanup.
If you leave the test file in place and power-cycle, it should disappear and
the initial tree should return. Save the complete capture before reporting
the new filesystem as physically verified.

The previous hardware-observed memory image is preserved locally as
`build/rpi5-header/OurOS-Pi5-header-memory-verified.img`; the still older UART
baseline remains `OurOS-Pi5-header-boot-verified.img`.

## Implementation and tests

- `filesystem/ramfs.c`: nodes, traversal, reads/writes, resource ownership and limits.
- `include/ramfs.h`: bounded filesystem operations and error codes.
- `kernel/core/fs_commands.c`: serial-monitor commands and current directory.
- File contents use `kmalloc`/`kfree`; short files share heap class pages.
- A failed write cannot create a partial file or consume a node/page. Existing
  contents are preserved when allocation or size validation fails.
- Initialization failure reclaims acquired pages and leaves the filesystem
  unavailable while preserving the rest of the monitor for diagnosis.

Limits: 64 nodes total, 31-byte names, 255-byte canonical paths and up to 4096
bytes of contents per file. Empty files/directories use metadata only. The
monitor's 127-byte input line imposes a smaller limit on one interactive write.
There are no permissions, symlinks, file descriptors, seek/append, general VFS
mount layer, concurrent access, crash recovery or persistent storage yet.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-ramfs.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1 -Handoff
```

Host tests cover MB/MiB formatting, initial contents, path rules, full-page binary
files, overwrite/truncation, invalid operations, out-of-memory rollback, node
exhaustion/reuse, exact path limits and 5000 guard-page path mutations. QEMU
tests run real command sequences and compare both filesystem statistics and
physical page counters before and after cleanup.

## Following milestones

This bounded RAMFS gives useful files now without waiting for virtual memory
or user processes. Those features still need their own implementation: virtual
memory/device mappings, interrupt controllers and timer interrupts,
context switching, scheduling, EL0 and system calls. Persistent files later need
an SD/block driver, a suitable filesystem driver and integration with a VFS.
