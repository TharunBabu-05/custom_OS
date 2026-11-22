# OurOS: implemented features, required subsystems and drivers

This is an original AArch64 kernel for the tested Raspberry Pi 5 header-UART
setup, with a separate QEMU `virt` target. It is not a complete desktop OS.
There is no universal list of drivers that every OS needs: a serial embedded
system can run without display, audio, Wi-Fi or even a disk. Our target is a
usable, networked Pi OS with persistent files and applications, so those
capabilities are tracked below.

**Status as of 2026-11-21:** an early single-core OS with protected user
applications, a serial shell, bounded persistent storage and foreground
networking. The original monitor-only stage is behind us. No percentage of a
complete OS is assigned: the remaining features have very different scopes.

**Current development** reports physical, kernel and per-process memory
pressure. Private heap diagnostics distinguish quota, fragmentation and
backing-page failures. [Memory pressure](memory-pressure.md)

**Latest verified hardware path** covers private heap quota, fragmentation
and reuse. Page, heap and open-file counts returned to their baselines.
[Pi capture](../proofs/pi5-memory-pressure-2026-11-21.txt)

Earlier hardware evidence covers [single background-job control](shell-jobs.md).
The [new Pi excerpt](../proofs/pi5-shell-jobs-2026-11-15.txt) shows a responsive
parent while SPIN runs, stop/collection with 130, one child started/reaped and
shell exit with code 0, zero live heap pages and restored resources.
The preceding [Pi process capture](../proofs/pi5-process-control-2026-11-15.txt) passes PS
check (12 children started/reaped, code 0) and interactive stop/start (two
children collected), with zero live heap pages and restored resources.
Earlier [allocator](../proofs/pi5-user-allocator-2026-11-15.txt) and
[private-page](../proofs/pi5-user-memory-2026-11-15.txt) checks also pass on Pi.
Broader installed-program/reboot and lifecycle cases retain their own scopes.

Hardware evidence also includes:

- [MMU protection](../proofs/pi5-mmu-usb-protection-2026-11-12.txt): unmapped
  null/guard faults, read-only code/constants and non-executable data.
- [Exceptions, GIC and 100 Hz timer IRQs](../proofs/pi5-timer-irq-usb-2026-11-12.txt):
  register-preserving return, delivered interrupts and responsive foreground.
- [EL0 processes](../proofs/pi5-el0-applications-2026-11-13.txt): private
  mappings, checked calls, preemption, contained faults/timeouts and installation.
- [Writable USB storage](../proofs/pi5-usb-v2-capacity-2026-11-15.txt),
  [v3 directories](../proofs/pi5-persistent-directories-2026-11-15.txt) and
  [restore](../proofs/pi5-data-restore-2026-11-15.txt): bounded commits and
  the specific same-boot file tests shown in those captures.
- [Shell programs](../proofs/pi5-shell-programs-2026-11-14.txt),
  [streams](../proofs/pi5-stdio-2026-11-14.txt),
  [pipes](../proofs/pi5-pipelines-2026-11-14.txt),
  [exec](../proofs/pi5-exec-lookup-2026-11-15.txt) and
  [saved profiles](../proofs/pi5-session-profile-2026-11-15.txt): the recorded
  user-shell paths, cleanup and same-boot profile reload.
- [Networking](../proofs/pi5-dns-hostname-ping-clean-disconnect-2026-11-12.txt):
  WPA3, DHCP, ARP, DNS and Internet ICMP followed by controlled disconnect.

The header image selects automatic startup, which mounts storage and sets up
the MMU and timer before opening the shell. Manual monitor boot is available
through the firmware command line. [Startup guide](startup.md)

Open hardware evidence includes interrupted-write recovery on physical
media, broader memory lifecycle/concurrency, atomic overwrite/appupdate,
and installed-shell persistence.
The [publication excerpt](../proofs/pi5-file-publication-2026-11-15.txt) used
a different destination spelling and does not establish replacement of the
original file. These gaps are tracked without requiring old passing tests
to be repeated at every development step.

## Core OS subsystems

| Subsystem | OurOS now | What remains |
| --- | --- | --- |
| Boot/firmware handoff | AArch64 entry, BSS/stack, primary-core selection, EL2 to EL1; Pi 5 UART boot observed | Pi Zero 2 W bring-up, other boot protocols/architectures |
| CPU/platform discovery | MIDR, current/entry EL, architectural timer frequency, selected DT properties | Broader DT bus/device discovery and feature handling |
| Exceptions | Common 288-byte frame; EL1 return and contained EL0 faults/syscalls/IRQ switching Pi verified | Richer signal/fault policy and emergency stack |
| Physical memory | DT RAM/reservations, 4 KiB page bitmap allocator, allocation/free tests; Pi verified | Per-owner accounting, richer allocation policies and contiguous multi-page allocation |
| Kernel heap | Zeroed 16..4096-byte buffers, calloc/realloc/free, page sharing/reclamation; Pi self-tests verified | Large allocations, IRQ/SMP synchronization and additional consumers |
| Virtual memory/protection | Pi verified identity/private pages and single-process dynamic RW/NX allocation; guarded arena and checked copies | Broader Pi dynamic lifecycle/concurrent checks, larger VM, shared/file mappings and cache acceleration |
| Interrupts | Pi-verified GICv2/PPI30, IRQ return and BCM compare channel 3 through the bounded SPI dispatcher and deferred callbacks. [Peripheral IRQs](peripheral-irqs.md) | Additional interrupt-driven drivers, IRQ-safe locks and SMP |
| Time | Counter/uptime, opt-in 100 Hz CNTP IRQs, start/stop, IRQ-safe snapshots and cooperative timed sleep/wakeup verified on Pi | Wall clock/RTC/time synchronization and broader timer services |
| Scheduling | Cooperative EL1 tasks; preempted EL0, sleep/wake/spawn/wait/pipes/process control; Pi bg/jobs/stop responsiveness and cleanup | Pi fg/bgio and broader lifecycle checks, multiple jobs/process trees, broader blocking I/O, SMP and indefinite services |
| Processes/system calls | Three private EL0 slots; checked files/console/args/cwd/spawn/wait/streams/pipes/exec/env/profiles/pages; snapshot/poll/cancel 40..42 and mount query 43 | Broader lifecycle hardware checks, IPC/descriptor API, credentials/access control and background services |
| Programs/runtime | Bounded ELF loader, 53 sample/test ELFs, Pi PS/memory/allocator checks, and QEMU-verified named app installs, upgrades and rollback. [Local app lifecycle](app-lifecycle.md) | Pi managed-app checks, larger VM, general libc/dynamic linking, packages and installer |
| Filesystem namespace | RAMFS `/`, read-only FAT32 `/boot`, opt-in `/data`; 16 owned generation-checked open/read/write/seek/close handles with cursors and path leases | General mounts, richer metadata, permissions, pseudo-filesystems and preemptive driver synchronization |
| Persistent files | Pi-verified v4 retention and v5 startup. Optional v5 raises the limits to 32 nodes and 32 KiB files and imports v4 backups. [Storage prototype](data-v5.md) | Physical v5 migration, retention and power-cut checks, and richer metadata |
| Shell | Pi-verified files/run/runio/pipes/exec/env/profiles/expansion. Two jobs, PID selection, command lists and `source` passed on Pi. [Shell workflows](shell-workflows.md) | Longer pipelines |
| Networking | WPA3/SAE, DHCP, ARP, IPv4, ICMP, UDP DNS and local EL0 UDP sockets. The Pi passed background pings, DHCP renewal, hotspot reconnect with a changed BSSID, and UDP echo with a Windows PC. [Background Wi-Fi](background-networking.md) · [UDP sockets](udp-sockets.md) | Pi UDP peer timeout and recovery, group key rotation and DHCP rebinding, TCP and HTTP/TLS |
| Security | Reservation-aware allocation, hidden volatile credentials, verified handshake checks, kernel/user page isolation, RW/NX and checked user copies | Credentials/permissions, broader access control, syscall/driver hardening, secure update/boot design and independent review |
| Multicore/concurrency | One active CPU; secondary cores parked | SMP startup, per-CPU state, locking, memory-order/IRQ-safe primitives and concurrent drivers |
| Diagnostics/testing | UART logs, self-tests, independent native fixtures, guard/mutation tests, QEMU A53/A76/EL1/EL2 and image checks | Continuous integration, longer hardware soak/failure/recovery tests, coverage and debugging tools |
| Build/update/install | Windows-native build, FAT packaging, hashes and archived full IMG/ZIP files; boot-file update can retain older data formats; explicit backup conversion | General update policy, on-device installer/rollback, package format/manager and application repository |

The system has three process slots, a 64 KiB dynamic page arena per process,
and bounded foreground sessions. The default v4 format supports 16 persistent
nodes and 16 KiB files. The optional v5 image supports 32 persistent nodes
and 32 KiB files. The volatile root `/` uses RAMFS. Persistent files live
under mounted `/data`. A general persistent root and startup service remain
open work.

Creating `/dev`, `/proc` and `/sys` directories does not implement device nodes
or kernel information files. DNS and `ping` do not supply sockets, TCP or a
package installer. Allocator tests, protection tests and process tests provide
different evidence; no individual self-test proves the entire OS.

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
| RTC/watchdog/power/reboot | Monitor `reboot` flushes and quiesces storage. Pi watchdog reboot and USB remount are verified. QEMU PSCI paths passed. RTC and power-off remain future work. [Safe reboot](safe-reboot.md) |
| Thermal/fan management | Firmware may manage hardware during boot; no OurOS management policy/driver |
| Camera, touch, other sensors | Optional board/device-specific work after core services |
| NVMe/SATA/other storage | Optional extra transports, not covered by the USB BOT or SD handoff driver |

A driver for one device class does not support every USB device. The Pi 5's
RP1/peripheral setup also does not automatically apply to Pi Zero 2 W, Jetson,
x86 laptops or RISC-V boards. Our shared C services are reusable; each platform
still needs its own boot, interrupt, peripheral and device implementations.

## Development order

The Pi passed the shell workflows and safe reboot with persisted data.
The Pi booted the Phase 5 v5 image and reported the expected storage limits.
Migration and recovery passed native and emulator tests. [Storage prototype](data-v5.md) · [Implementation plan](implementation-plan.md)

The Pi verified the Phase 6 managed-app listing. The full lifecycle passed
QEMU on v4 and v5. The first Phase 7 worker increment passed native and
QEMU checks and is ready for live Pi verification.
[Local app lifecycle](app-lifecycle.md) · [Background networking](background-networking.md)
