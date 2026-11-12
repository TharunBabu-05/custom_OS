# OurOS: implemented features, required subsystems and drivers

This is an original AArch64 kernel for the tested Raspberry Pi 5 header-UART
setup, with a separate QEMU `virt` target. It is not a complete desktop OS.
There is no universal list of drivers that every OS needs: a serial embedded
system can run without display, audio, Wi-Fi or even a disk. Our target is a
usable, networked Pi OS with persistent files and applications, so those
capabilities are tracked below.

**Evidence:** the [latest Pi log](../proofs/pi5-cooperative-tasks-usb-2026-11-12.txt)
confirms two cooperative task runs, private stacks/register/heap preservation,
sleep/wakeup/exit and private-stack IRQ return, with USB/FAT and memory tests
between runs. MMU and timer IRQs remain active without unexpected IRQs/faults.
Earlier captures verify the heap baseline and [WPA3/DNS/Internet ping and
cleanup](wifi-dns.md).
**Current phase:** [persistent USB storage](persistent-storage.md), block
write/flush and a separate `/data` snapshot volume. Native/QEMU tests cover
the implementation; its new Pi write/durability test is pending. A successful QEMU run does not establish hardware
support on a different board.

## Core OS subsystems

| Subsystem | OurOS now | What remains |
| --- | --- | --- |
| Boot/firmware handoff | AArch64 entry, BSS/stack, primary-core selection, EL2 to EL1; Pi 5 UART boot observed | Pi Zero 2 W bring-up, other boot protocols/architectures |
| CPU/platform discovery | MIDR, current/entry EL, architectural timer frequency, selected DT properties | Broader DT bus/device discovery and feature handling |
| Exceptions | Common 288-byte frame saves x0..x30/ELR/SPSR/ESR/FAR/vector; fatal diagnostics and exact armed BRK/MMU return; synchronous/asynchronous return verified on Pi | EL0 faults, signal/process policy and robust emergency stack |
| Physical memory | DT RAM/reservations, 4 KiB page bitmap allocator, allocation/free tests; Pi verified | Per-owner accounting, richer allocation policies and contiguous multi-page allocation |
| Kernel heap | Zeroed 16..4096-byte buffers, calloc/realloc/free, page sharing/reclamation; Pi self-tests verified | Large allocations, IRQ/SMP synchronization and additional consumers |
| Virtual memory/protection | Opt-in EL1 identity tables, RO code/constants/DTB/tables, RW/XN data, null/stack guard; repeated Pi protection and USB tests pass | Dynamic mappings, per-process address spaces, user/kernel split and cache maintenance |
| Interrupts | GICv2 PPI30, raw acknowledge/EOI, IRQ return and bounded unexpected-source quarantine; opt-in, Pi verified | General peripheral handlers/registration, IRQ-safe locks and SMP |
| Time | Counter/uptime, opt-in 100 Hz CNTP IRQs, start/stop, IRQ-safe snapshots and cooperative timed sleep/wakeup verified on Pi | Wall clock/RTC/time synchronization and broader timer services |
| Scheduling | Cooperative EL1 contexts, private guarded 16 KiB stacks, ready/sleep states, round-robin yield, wake/exit and repeatable self-test; Pi verified | Preemption, blocking I/O/driver ownership, general task creation, synchronization and background services |
| Processes/system calls | Everything runs in EL1 kernel context | EL0 entry, syscall ABI, user-pointer checks, exit/wait, permissions and process isolation |
| Programs/runtime | Kernel is freestanding C/assembly; built-in monitor commands | ELF loader, user C runtime/library, executable applications, later dynamic linking if needed |
| Filesystem namespace | Writable volatile RAMFS `/`; read-only FAT32 `/boot`; new opt-in persistent `/data` route | General mount/open/read/write/close handles, offsets, richer metadata and pseudo-filesystems |
| Persistent files | New USB writes/synchronous flush and two checksummed snapshots in a separate 1 MiB data partition, eight flat 4 KiB files | Pi persistence validation, larger/nested files, writable general filesystem metadata, migration/export and physical power-loss tests |
| Shell | Serial kernel monitor with file, memory, storage and network commands | A real EL0 shell, job control/pipes/redirection and program launch |
| Networking | WPA3/SAE, keys/port, DHCP, ARP, IPv4, ICMP and UDP DNS; Pi Internet pings verified | Renewal/rekey/reconnect, background service, neighbor cache, sockets, TCP, HTTP/TLS; IPv6 if required |
| Security | Reservation-aware allocation, hidden volatile credential input, verified handshake checks; new page permissions | User/process boundaries, filesystem permissions, access control, secure update/boot design and review |
| Multicore/concurrency | One active CPU; secondary cores parked | SMP startup, per-CPU state, locking, memory-order/IRQ-safe primitives and concurrent drivers |
| Diagnostics/testing | UART logs, self-tests, independent native fixtures, guard/mutation tests, QEMU A53/A76/EL1/EL2 and image checks | Continuous integration, longer hardware soak/failure/recovery tests, coverage and debugging tools |
| Build/update/install | Windows-native cross-build, FAT disk packaging, hashes, archived full IMG/ZIP files | On-device installer/update rollback, package format/manager and application repository |

Creating `/dev`, `/proc` and `/sys` directories does not implement device nodes
or kernel information files. Likewise, a command named `ping` does not imply
TCP, sockets or software installation. The current heap test proves the
allocation layer, not virtual memory or process isolation.

## Drivers and platform support

| Driver/interface | Status and scope |
| --- | --- |
| PL011 serial UART | Working on QEMU and the tested Pi 5 RP1 GPIO14/15 header console; firmware setup preserved on Pi |
| Architectural timer | Counter/frequency and 100 Hz NS physical timer IRQ via PPI30, `timerstart`/`timerstop` and asynchronous register-preservation test verified on Pi |
| Interrupt controller (GIC) | Bounded GICv2 nonsecure CPU-interface/PPI30 driver for explicit Pi/QEMU DT profiles; raw IAR/EOIR tokens and unexpected-source quarantine; Pi verified, no general peripheral/SMP routing |
| Device-tree reader | Bounded memory/reservation and selected device/property readers; not a general driver bus |
| RNG200 | Pi hardware source, conditioning and engineering failure checks implemented; used by SAE/DNS/ping |
| PCIe/RP1 | Uses validated firmware-selected Pi windows/DMA handoff; no general PCIe reset/enumeration/hotplug driver |
| xHCI USB host | Experimental polled controller ownership/DMA, direct USB2 enumeration and transfers; verified with the test pendrive |
| USB mass storage | BOT + SCSI sectors/capacity verified on Pi; new WRITE10/CACHE10 and bounded `/data` snapshot writes, Pi writes pending; no UAS or general USB3/hub support |
| QEMU virtio block | Read-only block path tested in QEMU; not a Pi hardware driver |
| Pi SDHCI storage | Limited firmware-selected SDHC/SDXC handoff, read-only; no complete cold card initialization; not verified without an SD card |
| Wi-Fi SDIO | Pi power/pins/400 kHz SDIO/F1/F2/backplane/firmware/FullMAC control implemented and verified |
| Wi-Fi station/authentication | WPA3-Personal SAE + required PMF and data key setup, foreground DHCP/DNS/ping verified on Jack's; not general long-lived station management |
| GPIO/pin control | Limited WLAN power/mux setup and firmware-initialized UART; no general user GPIO API |
| USB keyboard/mouse (HID) | Needed for local interactive use; not implemented |
| HDMI/display/framebuffer | Needed for a local screen; no OurOS display driver or graphical console yet |
| Graphics/GPU acceleration | Optional later desktop capability; not implemented |
| Ethernet | Optional alternative network transport on Pi 5; MAC/PHY driver and integration not implemented |
| Bluetooth | Optional; not implemented |
| Audio | Optional; controller/codec/audio service not implemented |
| I2C/SPI/PWM/general GPIO | Add for attached peripherals as needed; not general-purpose drivers yet |
| RTC/watchdog/power/reboot | Add platform drivers and lifecycle handling; `halt` only stops the CPU; not a shutdown/reboot implementation |
| Thermal/fan management | Firmware may manage hardware during boot; no OurOS management policy/driver |
| Camera, touch, other sensors | Optional board/device-specific work after core services |
| NVMe/SATA/other storage | Optional extra transports, not covered by the USB BOT or SD handoff driver |

A driver for one device class does not support every USB device. The Pi 5's
RP1/peripheral setup also does not automatically apply to Pi Zero 2 W, Jetson,
x86 laptops or RISC-V boards. Our shared C services are reusable; each platform
still needs its own boot, interrupt, peripheral and device implementations.

## Development order

1. **Now:** validate USB data writes/flush and persistence across Pi power-cycles,
   preserving the verified cooperative task/IRQ/MMU/read-only boot paths.
2. **Files and applications:** general file handles/offsets and task ownership,
   then application loading, blocking/wakeup APIs, critical sections and driver
   contracts before preemption or background services.
3. **Timer/peripheral services:** carefully scoped peripheral handlers and
   timer-driven scheduling, with explicit IRQ-safe resource ownership.
4. **User mode:** process address spaces, system calls, validated user memory,
   executable loader and a small user runtime/shell.
5. **Persistent storage:** safe device writes/flush, writable filesystem,
   generalized VFS and power-loss/error recovery tests.
6. **Network services:** renewal/rekey/reconnect and background service; sockets,
   TCP/HTTP/TLS and downloads before package installation can be offered.
7. **Local interaction:** USB HID, display/text console, then UI/audio and optional
   hardware. These may be developed alongside core work where dependencies allow.
8. **Expansion:** SMP, additional boards/devices, performance, update/install
   tooling and longer reliability/security testing.

This list is the roadmap, not a claim that all remaining subsystems fit into
one phase. Each stage needs its own implementation and evidence before the
next layer can rely on it.
