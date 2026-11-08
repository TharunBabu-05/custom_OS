# Architecture and staged implementation

Current status: native Windows cross-builds pass for both CPUs, and the first
QEMU kernel/monitor is tested. See [first-kernel.md](first-kernel.md). WSL is
optional following the user's updated direction. The [Pi 5 boot candidate](rpi5-boot.md)
now builds/packages and its EL1/EL2 entry assembly is emulator-tested. The RP1
header-console kernel has booted on the user's Pi 5 with working commands,
CPU information and advancing uptime; see [hardware evidence](pi5-hardware-verification.md).
Directory placeholders do not imply driver support.
The [RAM/reservation discovery and physical page allocator](memory.md) pass
host/QEMU tests and now have physical Pi evidence. The bounded [RAMFS root
filesystem](ramfs.md) also has physical evidence. The new [read-only storage
and FAT32 mount](storage.md) pass real QEMU I/O tests; Pi SD handoff and the
[Wi-Fi SDIO diagnostic](wifi.md) await hardware capture. Wi-Fi connections and
storage writes remain future stages.
MMU, heap and scheduling are separate later milestones.

## Design

Use a modular monolithic kernel: kernel services and drivers share the privileged
address space, but have explicit interfaces. Start with one active core and
polling UART. Add interrupts, memory protection, scheduling, and user space in
that order. Initial design targets EL1 kernel code and later EL0 applications;
the actual firmware entry EL and transitions must be measured and documented.

Both boards share AArch64 exception handling, context frames, page-table code,
barriers, and generic timer abstractions. They have separate board discovery,
interrupt-controller wiring, UART instances/clocks, peripheral mappings, boot
files, and later storage/USB drivers. A common instruction set is not a common
peripheral map or a promise that the same binary boots both boards.

Compile early shared code for an ARMv8-A baseline when portability is required.
Board builds may use their own `-mcpu` setting. Do not link Cortex-A76-specific
objects into a Cortex-A53 image. Discover RAM from DTB memory/reserved regions;
installed capacity is not all allocatable RAM. Use 4 KiB pages as the initial
design choice, subject to CPU feature checks and documented boot assumptions.

## Source layout

```text
boot/{rpi5,rpi_zero2w}/        Board-specific firmware manifests/configs (future)
arch/aarch64/
  boot/                      Entry, stack, BSS, exception-level normalization
  cpu/                       CPU discovery, barriers, secondary-core startup
  mmu/                       Page tables, MMU enable, TLB maintenance
  interrupts/                Vectors and architectural exception frames
  context/                   Save/restore CPU state
boards/{rpi5,rpi_zero2w,qemu_virt}/  Memory maps, early console, board initialization
kernel/
  core/                      Init ordering, logging, panic, device-tree discovery
  memory/                    Physical allocator, virtual address policy, heap
  interrupts/                IRQ registration and dispatch policy
  scheduler/                 Runnable queues, scheduling policy, timer ticks
  process/                   Tasks, address spaces, lifecycle, image loading
  syscall/                   Validated EL0-to-EL1 ABI and dispatch
drivers/{uart,gpio,timer,irq,storage,display,usb}/  Hardware implementations
filesystem/                  VFS, initial RAM filesystem, later FAT/block storage
lib/                         Small in-tree freestanding utilities
include/                     Public internal interfaces, architecture contracts
user/{shell,applications}/    Future EL0 programs; separate from the kernel
linker/                      Board image linker scripts (future)
scripts/                     Setup, verification, later packaging
tools/                       Future host utilities
tests/toolchain/              Nonbootable C/assembly/linker probes
docs/                        Architecture, setup, evidence, development log
build/                       Ignored generated outputs, separated by target
```

Empty directories are reserved boundaries, not implemented subsystems. No ARMv6
directory is created for the confirmed boards. Add one only for a real ARM32 port.

## Contracts to implement

| Boundary | Contract and key requirements |
| --- | --- |
| Firmware -> architecture entry | Capture incoming registers/DTB before C; verify EL, MMU/cache state and load address; park secondary cores; establish aligned stack and clear BSS |
| Board -> shared kernel | Supply validated boot information: usable/reserved RAM, console, IRQ/timer wiring, and hardware identity |
| CPU exceptions -> kernel | Preserve a defined register frame; distinguish sync faults, IRQ/FIQ and SError; diagnose unexpected vectors |
| Memory -> other services | Reserve kernel/DTB/stacks/firmware first; allocate physical pages; map with permissions and correct device memory attributes |
| IRQ/timer -> scheduler | Acknowledge/end interrupts correctly; expose monotonic time; preempt only with valid context handling |
| Task -> address space | Separate kernel stacks and user mappings; explicit ownership and lifecycle |
| Syscall -> user memory | Validate numbers, pointers, lengths and access; never trust EL0 addresses |
| VFS -> filesystem -> block device | Shared file handles and offsets; device drivers expose blocks; filesystems parse on-disk formats |
| Shell -> syscalls | Start as a kernel monitor if useful, explicitly named as such; later migrate to an actual EL0 shell |

Memory order and locking are part of each interface; SMP comes after the uniprocessor
implementation is stable. GPIO and UART drivers must not silently assume Pi 5's
RP1 peripherals match BCM2710A1 peripherals.

## Milestones and evidence

1. **Environment (native baseline complete):** portable Windows GNU AArch64 tools,
   two successful CPU compile/link checks, QEMU and GDB verified. Ubuntu and SD
   packaging utilities are deferred; they do not block native kernel development.
2. **First kernel:** separate generic QEMU board with UART output and GDB inspection;
   then Pi 5 firmware integration and a captured UART boot message on real hardware.
   QEMU startup/monitor/exception/GDB checks now pass. QEMU success alone does not
   complete the hardware milestone.
3. **Core:** exception diagnostics, timer IRQs, physical pages, heap and MMU.
   Exercise a deliberate fault, allocation boundaries and mapping permissions.
4. **Tasks:** kernel context switches and scheduling, then EL0, syscall validation,
   executable loading and a general VFS. A bounded RAMFS already works in the
   kernel monitor; it does not implement process isolation or executable loading.
   Demonstrate isolation and two tasks before claiming user-space support.
5. **CLI:** implement commands only as their underlying services work. `help`,
   `echo`, `info` first; `ls`/`mkdir` now use the bounded RAMFS; `reboot`/`shutdown` need
   documented board mechanisms. Clearly state RAM-only persistence limits.
6. **Zero 2 W board port:** share AArch64 code, adapt firmware/UART/interrupts,
   verify actual UART boot and memory behavior on its 512 MB platform.
7. **Advanced:** SD/FAT persistence, GPIO/display, supported keyboard/USB, networking,
   SMP, security review, then GUI. Scope each feature independently.

Each stage records: implemented behavior, build result, emulation result, hardware
result, observed failures, and exact next acceptance criterion.

## Build and deployment pipeline (future)

C / `.S` -> per-board objects -> linker script -> debug ELF -> raw boot image ->
board-specific firmware/config package -> regular SD image file -> inspect/check
image -> explicitly confirmed physical flash -> UART capture on the board.

The QEMU board gets its own image and run command. Its memory map and entry
protocol must not be reused as Pi hardware assumptions. Preserve debug ELF files
for GDB even when firmware loads only a raw binary. Do not add successful-looking
placeholder image/run targets.

## Architecture reference

The [Arm A-profile Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest/)
is the normative reference for exception levels, system registers, translation,
barriers, and exception handling. In Phase 2, cite specific manual sections and
board documentation next to every implemented register/boot assumption. No MMIO
addresses or entry-state guarantees are asserted in this initial scaffold.
