# OurOS: implemented features, required subsystems and drivers

This is an original AArch64 kernel for the tested Raspberry Pi 5 header-UART
setup, with a separate QEMU `virt` target. It is not a complete desktop OS.
There is no universal list of drivers that every OS needs: a serial embedded
system can run without display, audio, Wi-Fi or even a disk. Our target is a
usable, networked Pi OS with persistent files and applications, so those
capabilities are tracked below.

**Evidence:** the [latest Pi excerpt](../proofs/pi5-file-task-io-verified-2026-11-13.txt)
confirms file handles, cooperative task file I/O before/after USB storage,
automatic handle cleanup and heap/page restoration with MMU/timer enabled.
Earlier captures verify the heap baseline and [WPA3/DNS/Internet ping and
cleanup](wifi-dns.md).
**Current phase:** [application exec and shell program lookup](application-exec.md).
Adds checked `EXEC`, staged image replacement preserving identity/streams,
and fixed `/data/bin:/boot` lookup. New physical acceptance is pending.
The preceding [file-update phase](atomic-file-updates.md)
adds explicit `replace`, checked EL0 `PUBLISH` and validated `appupdate` without
changing the v1/v2/v3 disk formats. Local tests cover interrupted publication,
leases, update rejection and cold emulator boot; new hardware acceptance is
pending. The previous [directory phase](persistent-directories.md) passed DIRCHK.
The [new Pi capture](../proofs/pi5-file-rename-2026-11-15.txt) passes MVCHK on
v2. New v3 images add mkdir/cd/list/remove and file moves in nested `/data`
paths, with explicit backup migration and unchanged legacy v1/v2 mounts.
The [latest Pi capture](../proofs/pi5-persistent-directories-2026-11-15.txt)
now passes DIRCHK with cleanup. Physical reboot, nested installed ELF execution
and serial export remain outstanding.
The [latest restore excerpt](../proofs/pi5-data-restore-2026-11-15.txt) verifies
DEMO.BAK restore, sizes and text readback on Pi. Binary hashes, serial export,
installed-shell and actual power-cycle checks remain pending. MVCHK now
verifies the EL0 rename path; DIRCHK now verifies the directory self-test.
The [new Pi capture](../proofs/pi5-usb-v2-capacity-2026-11-15.txt) now passes
v2 DATACAP, usbcheck and 152 recovered stalls with USB still online. The earlier
lifetime-cutoff fix is physically verified for that test. Installed-shell and
actual power-cycle checks remain pending. Backup export, validated empty-target
restore and Windows archive conversion have narrower evidence: restore now
passes as described above; export/conversion still require physical capture.
The [physical file-tools capture](../proofs/pi5-file-tools-2026-11-14.txt) now
verifies copying, matching SHA-256, sync and cleanup, following interactive shell
acceptance. The [child-program capture](../proofs/pi5-shell-programs-2026-11-14.txt)
passes spawn/wait, early terminal handoff, normal shell runs and timeout cleanup.
The [file-stream capture](../proofs/pi5-stdio-2026-11-14.txt) passes IOCHK, file
output/copy and cleanup; subsequent wrong-path commands are explained in the
pipeline guide. The [pipe capture](../proofs/pi5-pipelines-2026-11-14.txt) now passes
PIPECHK, stream hashing, shell pipelines and cleanup. V2 capacity now passes;
installed-shell persistence and serial export still need Pi acceptance. New
images use v3; old v1/v2 mounts retain their original geometry and limits.
The [new Pi capture](../proofs/pi5-usb-data-write-sync-2026-11-13.txt) confirms
that the BOT fix permits `/data` mount, write/read and sync with WCE=0.
It shows no second boot banner, so physical power-cycle persistence remains
unconfirmed. The [latest EL0 Pi capture](../proofs/pi5-el0-applications-2026-11-13.txt)
passes private mappings, syscall checks, preemption, fault containment and
installed program execution. [Arguments/cwd/metadata/utilities/sleep](../proofs/pi5-user-tools-2026-11-13.txt)
now also pass on Pi, as do terminal input, mutation syscalls and the interactive
shell and copy/hash/sync. V2 persistent capacity requires physical acceptance.
Display/HID/GPU/audio and general installation/packages remain in the
[remaining roadmap](remaining-os-roadmap.md).

## Core OS subsystems

| Subsystem | OurOS now | What remains |
| --- | --- | --- |
| Boot/firmware handoff | AArch64 entry, BSS/stack, primary-core selection, EL2 to EL1; Pi 5 UART boot observed | Pi Zero 2 W bring-up, other boot protocols/architectures |
| CPU/platform discovery | MIDR, current/entry EL, architectural timer frequency, selected DT properties | Broader DT bus/device discovery and feature handling |
| Exceptions | Common 288-byte frame; EL1 return and contained EL0 faults/syscalls/IRQ switching Pi verified | Richer signal/fault policy and emergency stack |
| Physical memory | DT RAM/reservations, 4 KiB page bitmap allocator, allocation/free tests; Pi verified | Per-owner accounting, richer allocation policies and contiguous multi-page allocation |
| Kernel heap | Zeroed 16..4096-byte buffers, calloc/realloc/free, page sharing/reclamation; Pi self-tests verified | Large allocations, IRQ/SMP synchronization and additional consumers |
| Virtual memory/protection | Pi verified EL1 identity profile, private user pages, EL0 AP/W^X/guards and checked copies | Dynamic mappings, larger address spaces and cache acceleration |
| Interrupts | GICv2 PPI30, raw acknowledge/EOI, IRQ return and bounded unexpected-source quarantine; opt-in, Pi verified | General peripheral handlers/registration, IRQ-safe locks and SMP |
| Time | Counter/uptime, opt-in 100 Hz CNTP IRQs, start/stop, IRQ-safe snapshots and cooperative timed sleep/wakeup verified on Pi | Wall clock/RTC/time synchronization and broader timer services |
| Scheduling | Cooperative EL1 tasks, EL0 preemption/sleep/input/spawn/wait; new blocked FIFO reads/writes and two-child jobs | Pi pipe validation, general process trees/jobs, broader blocking I/O, SMP and background services |
| Processes/system calls | Three private EL0 slots; checked files/console/time/args/cwd/mutations/spawn/wait/file streams; new two-stage PIPE_RUN | Pi new-call validation, broader IPC/descriptor API, credentials/access control and background services |
| Programs/runtime | Bounded ELF loader, tiny runtime, 41 sample/test ELFs, shell/streams/pipes, Pi MVCHK/DIRCHK, nested installation, atomic updates and new exec/program lookup | Pi exec/update acceptance, installed-shell persistence, environment, larger programs/files, libraries/dynamic linking, packages and general installer |
| Filesystem namespace | RAMFS `/`, read-only FAT32 `/boot`, opt-in `/data`; 16 owned generation-checked open/read/write/seek/close handles with cursors and path leases | General mounts, richer metadata, permissions, pseudo-filesystems and preemptive driver synchronization |
| Persistent files | Pi verifies v2, restore excerpt and MVCHK; new v3 sixteen file/directory nodes, nested 8 KiB files and explicit legacy backup migration | Pi v3/export/power-cycle checks, larger files/volumes, richer metadata and power-loss tests |
| Shell | Kernel monitor with shell-command hints; Pi-verified EL0 files/run/runio/pipes; new installable shell on v2 | Pi stored-shell acceptance, job control, longer pipelines, shell operators and environment |
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
| USB mass storage | BOT + SCSI sectors/capacity, WRITE10 and WCE=0 synchronous `/data` writes verified on Pi; no UAS or general USB3/hub support |
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

1. **Now:** validate v2 capacity and installed shell on the Pi, and
   installed ELF execution across actual power-cycles without reflashing.
2. **Files and applications:** extend the implemented handles/ELF/runtime with
   broader process lifecycles, blocking I/O and synchronization contracts.
3. **Timer/peripheral services:** carefully scoped peripheral handlers and
   timer-driven scheduling, with explicit IRQ-safe resource ownership.
4. **User mode:** expand the bounded private address spaces/syscalls/runtime
   into a real user shell, IPC and longer-lived process lifecycle.
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
