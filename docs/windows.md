# Native Windows: build, run, test and debug

For the Pi 5 candidate and firmware-handoff tests, see [rpi5-boot.md](rpi5-boot.md).
The commands below continue to run the generic QEMU board.
The new [RAM root filesystem](ramfs.md) adds file commands to that monitor;
`scripts/test-ramfs.ps1` runs its native host tests (including MB/MiB formatting).
For read-only block I/O and FAT32, see [storage.md](storage.md) and run
`scripts/test-storage.ps1`. `scripts/run-qemu.ps1 -StorageImage <image-path>`
attaches a regular image read-only through virtio; `storageinit` mounts it.

WSL is **not required**. Run these commands in Windows PowerShell at
`C:\Users\tharu\operating_system`. Do not run the Ubuntu installer here.
Your reported DISM success enables a Windows feature, but does not restore the
missing WSL application. It can be repaired independently later.

## Tools

The tools are already downloaded and extracted on this machine. For a fresh
checkout, or to verify the cached downloads and tools:

```powershell
cd C:\Users\tharu\operating_system
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-windows.ps1
```

This script uses Windows' `curl.exe` to download into `.dist/`, verifies the
published GNU/QEMU archive digests, extracts with 7-Zip, and checks executable
versions. It does not run either installer, change PATH, change the registry, or
write any disk image to hardware. `ExecutionPolicy Bypass` applies to the single
PowerShell process, not the system policy.

| Tool | Pinned version / purpose |
| --- | --- |
| Arm GNU Toolchain, Windows `aarch64-none-elf` | 13.3.Rel1; GCC 13.3.1, GNU assembler/linker/binutils, target GDB |
| QEMU Windows build | 11.1.0, distributor build 20260811; AArch64 emulation using TCG |
| 7-Zip | 26.06; unpack tools without system installation |
| PowerShell | Native build/test orchestration; no Make/Bash/Python required |
| Existing MSYS2 host GCC | 15.2.0; used only for the additional Windows-host DTB test |

Sources: [Arm release notes and Windows target variant](https://support.arm.com/documentation/109845/13-3-Rel1),
[QEMU's Windows download guidance](https://www.qemu.org/download/),
[Windows QEMU distributor](https://qemu.weilnetz.de/w64/),
[7-Zip downloads](https://www.7-zip.org/download.html).
These are pinned working versions, not a claim that every component is newest.
Vendor licenses remain in their extracted directories. Cache hash records are in
`.dist/downloads.json`; `.dist/` and generated `build/` outputs are Git-ignored.

## Build and run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target virt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-qemu.ps1
```

`check` compiles the nonbootable probe separately with `-mcpu=cortex-a53` and
`-mcpu=cortex-a76`, then validates its ELF type and linkage. `virt` rebuilds every
kernel source for the A53 instruction baseline and links `build/qemu_virt/kernel.elf`.
The same kernel is also tested on QEMU's A76 CPU. A raw `kernel.bin` is generated
for inspection but is **not a Pi boot image**; the runner uses the ELF file.

Expected console output:

```text
OurOS: AArch64 kernel booted
DTB discovered PL011 at 0x0000000009000000 clock_hz=24000000
Kernel monitor ready. Type help.
> help
help info cpuinfo meminfo uptime echo <text> clear halt fault
```

Use `info`, `cpuinfo`, `meminfo`, `uptime`, `echo hello`, or `clear`.
Backspace/DEL editing and CR/LF/CRLF line endings are supported. A line longer
than 127 characters is discarded completely, then the next command is accepted.
The polling UART can lose input from very fast paste/bursts; automated tests
pace input to fit its FIFO. Interrupt-driven buffered input comes later.

`halt` halts the guest CPU and leaves QEMU running. Ctrl+C in the terminal stops
the emulator. `fault` intentionally executes `brk #0x42`; it prints exception
diagnostics and halts. This is a privileged kernel monitor, not an EL0 shell.

For the A76 CPU test configuration:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-qemu.ps1 -Cpu cortex-a76 -MemoryMB 512
```

The runner pins `virt-9.2`, TCG, one core, nonsecure EL1 (`secure=off` and
`virtualization=off`), no network, and a stdio UART. CPU/RAM choices do not make
this a Pi emulator. Firmware, storage and RP1 behavior are not modeled by this
kernel's board port.

## Automated checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-qemu.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-gdb.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-fdt.ps1
```

All passed on this machine. QEMU tests cover A53/128 MiB and A76/512 MiB boot,
console commands, line editing, overflow rejection/recovery, and a deliberate
fatal exception. GDB checks initial stack alignment and clears a deliberately
pre-filled BSS flag through the real startup code before checking it at C entry.
The host DTB test checks real QEMU device discovery and malformed/truncated data,
then performs 5,000 deterministic mutations with an inaccessible guard page.
This is targeted coverage, not a proof that every malformed DTB is handled.

The host DTB test defaults to `C:\msys64\mingw64\bin\gcc.exe`, already installed
here. On another Windows machine supply a native MinGW GCC through
`-HostCompiler 'C:\path\to\gcc.exe'`. This host compiler is separate from the
kernel's AArch64 compiler. Other checks require only the portable tools.

Evidence: `build/qemu_virt/monitor-a53.log`, `monitor-a76.log`, `fault-a53.log`,
`gdb.log`, `readelf.txt`, `disassembly.txt`, and `kernel.map`. The tests launch
headless child processes and stop only those processes when finished.

## Interactive GDB

Terminal 1, **Windows PowerShell**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-qemu.ps1 -Gdb
```

Terminal 2, **Windows PowerShell** from the same repository:

```powershell
& .\.dist\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-aarch64-none-elf\bin\aarch64-none-elf-gdb.exe .\build\qemu_virt\kernel.elf
```

At the **GDB prompt**:

```text
target remote 127.0.0.1:1234
break kernel_main
continue
info registers pc sp cpsr
print console_ready
break exception_fatal
continue
```

Type `fault` in Terminal 1 to hit the exception breakpoint. `detach` lets the
guest continue; `quit` exits GDB. The GDB listener is localhost-only.

VS Code's default build task (`Ctrl+Shift+B`) builds the kernel. Run the
`OS: run QEMU kernel` or `OS: test QEMU kernel` task from the task picker for
execution or integration testing. No WSL extension is necessary for this path.
