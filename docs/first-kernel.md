# First emulated AArch64 kernel

## Scope and boot contract

This is original freestanding C/AArch64 code, with no Linux kernel or target
libc. The implementation is single-core EL1 on QEMU's `virt-9.2` board. MMU and
caches stay in their reset state, interrupts stay masked, and no process/user
address space exists yet. Do not load this image on a Pi: it has a QEMU link
address and an explicitly QEMU-specific entry contract.

QEMU loads the ELF segments and starts at `_start` (`0x40200000`). In this
bare-metal ELF mode, its DTB is at RAM start (`0x40000000`); this differs from
the non-ELF Linux boot protocol's register handoff. QEMU only guarantees the
RAM and flash base addresses; peripheral information comes from the DTB.
[QEMU bare-metal boot documentation](https://www.qemu.org/docs/master/system/arm/virt.html#hardware-configuration-information-for-bare-metal-programming).

Entry masks exceptions, parks unexpected secondary cores, requires EL1, selects
SP_EL1, installs a 16-byte-aligned 64 KiB stack, clears BSS, and sets VBAR_EL1.
Unsupported entry EL parks the CPU rather than making an unverified transition.
The linker reserves the first 2 MiB of RAM for the DTB and keeps the entire early
image below `0x41000000`. There is no page allocator, so memory/reservation parsing
for allocation is deliberately not claimed.

## Source map

| File | Important functions / purpose |
| --- | --- |
| `arch/aarch64/boot/start.S` | `_start`: initial CPU state, stack, BSS, vectors, C entry |
| `arch/aarch64/interrupts/vectors.S` | Sixteen 128-byte vector slots aligned to 2048 bytes; fatal routing with vector index, ESR, ELR and FAR |
| `linker/qemu_virt.ld` | ELF load segments, kernel address, BSS and stack boundaries |
| `boards/qemu_virt/board.c` | `board_init`: locate QEMU DTB, validate RAM contract, configure discovered UART |
| `kernel/core/fdt.c` | `fdt_read_board`: bounds-check header/tokens/strings; find the root-level PL011, its fixed clock and one RAM bank |
| `drivers/uart/pl011.c` | `uart_init`: baud/FIFO setup; `uart_putc` and `uart_getc`: polled I/O |
| `drivers/timer/arch_timer.c` | `timer_ticks` reads CNTPCT_EL0; `timer_frequency` reads CNTFRQ_EL0 |
| `kernel/core/console.c` | Newline translation, string output and integer formatting without printf/libc |
| `kernel/core/main.c` | `kernel_main`: initialization and monitor; `exception_fatal`: diagnostics and halt |
| `lib/memory.c` | In-tree `memset`, required by compiler-generated aggregate initialization |
| `include/*.h` | Interfaces shared across the board, driver and kernel code |

The DTB reader implements a narrow, tested subset of
[DTSpec's flattened format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html):
version 17-compatible header, big-endian fields, bounded tokens/string tables,
direct-root devices using two address and size cells, one enabled UART, one RAM
bank and a fixed-clock phandle. It is not a general bus-address translator or
Pi-compatible device discovery layer. Unsupported/malformed descriptions fail
before UART initialization; that early failure currently produces no serial log
and must be examined with GDB.

## UART registers and timer

The PL011 offsets below are relative to the **discovered** CPU physical base.
They are 32-bit volatile accesses with barriers. The QEMU DTB used in testing
reports base `0x09000000`, clock 24 MHz and register region size 4096 bytes.
See the [Arm PL011 technical reference](https://support.arm.com/documentation/ddi0183/latest/)
and [QEMU 9.2 virt board implementation](https://gitlab.com/qemu-project/qemu/-/blob/v9.2.0/hw/arm/virt.c).

| Offset | Register | Use |
| --- | --- | --- |
| `0x00` | UARTDR | Byte I/O; receive-error bits cause the input byte to be discarded |
| `0x18` | UARTFR | Wait for TX FIFO space (bit 5), detect RX empty (bit 4), wait for BUSY clear (bit 3) |
| `0x24`, `0x28` | UARTIBRD, UARTFBRD | Rounded fixed-point baud divisor; 24 MHz / 115200 gives integer 13, fraction 1 |
| `0x2c` | UARTLCR_H | 8-bit data and FIFO enable, no parity, one stop bit |
| `0x30` | UARTCR | Enable UART, transmit and receive |
| `0x38`, `0x44` | UARTIMSC, UARTICR | Mask and clear UART interrupts; polling only |

The generic timer counter is read with an instruction synchronization barrier.
Uptime uses its advertised frequency rather than a guessed CPU clock. This is
counter access only: no timer interrupt or scheduling implementation exists.
System register semantics follow the
[Arm A-profile Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest/).

## Exception behavior

All sixteen vector slots are installed, but only fatal handling is implemented.
The handler does not save a resumable context and never returns. `fault` executes
`brk #0x42` from EL1h, exercising the current-EL SPx synchronous slot (index 4).
The tested output reports ESR `0xf2000042`, the faulting PC and FAR. FAR is not
meaningful for every exception type; it is printed as a raw diagnostic register.
This does not establish IRQ-controller or general context-switch support.

## Validation and next boundary

See [Windows commands](windows.md) for complete build/debug/test instructions.
The recorded tests establish compilation for both physical targets' CPUs,
generic emulator execution on both CPU models, timer/console behavior, bounded
input, fatal exception handling, selected DTB parsing properties and GDB access.
They do not establish Raspberry Pi peripheral or firmware compatibility.

The next physical milestone remains Pi 5 UART boot. The separate board entry,
configuration and package are now described in [rpi5-boot.md](rpi5-boot.md), with
EL1/EL2 handoff tests passing in QEMU. Hardware evidence is still pending. MMU, IRQs,
allocators, tasks, filesystems and EL0 follow as separate tested increments.
