# OurOS: implemented features, required subsystems and drivers

**2026-11-27 status review.** The [extended roadmap](remaining-os-roadmap.md#extended-plan)
tracks current Pi acceptance, remaining work, and the phase sequence through
Phase 70. Parts of the subsystem inventory below retain their earlier
baseline scope.

The Pi verified Phase 63 multicore EL0 batches, demand-zero reservations,
broadcast invalidation for user mapping updates, and cache-maintained RP1
DMA. The current scheduler extends normal shell and program execution to the
boot CPU and all three workers. QEMU measured overlapping EL0 execution on
all four CPUs. [Implementation and acceptance](core-vm-smp.md)

Phase 65 passed the v4-to-v5 archive import and the physical power-cut
test on spare media. The Pi recovered the last committed generation and
successfully wrote a new one. [Storage acceptance](storage-apps.md)

This is an original AArch64 kernel for the tested Raspberry Pi 5 header-UART
setup, with a separate QEMU `virt` target. It is not a complete desktop OS.
There is no universal list of drivers that every OS needs: a serial embedded
system can run without display, audio, Wi-Fi or even a disk. Our target is a
usable, networked Pi OS with persistent files and applications, so those
capabilities are tracked below.

Memory diagnostics report physical, kernel and per-process memory
pressure. Private heap diagnostics distinguish quota, fragmentation and
backing-page failures. [Memory pressure](memory-pressure.md)

The Pi verified private heap quota, fragmentation
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

Open hardware checks include the current four-CPU scheduler, relocated
DTB boot, and the integrated HDMI and USB keyboard console. Broader memory
stress, atomic overwrite test coverage, and installed-shell persistence
remain tracked in the [extended roadmap](remaining-os-roadmap.md).

## Core OS subsystems

| Subsystem | OurOS now | What remains |
| --- | --- | --- |
| Boot/firmware handoff | AArch64 entry, BSS/stack, primary-core selection, EL2 to EL1; Pi 5 UART boot observed | Pi Zero 2 W bring-up, other boot protocols/architectures |
| CPU/platform discovery | MIDR, current/entry EL, architectural timer frequency, selected DT properties | Broader DT bus/device discovery and feature handling |
| Exceptions | Common 288-byte frame; EL1 return and contained EL0 faults/syscalls/IRQ switching Pi verified | Richer signal/fault policy and emergency stack |
| Physical memory | Pi-verified 4 KiB bitmap allocator, worker page leases, allocation windows, boundary constraints and per-zone fragmentation reports. | Pi USB accessory DMA placement and ownership beyond worker leases |
| Kernel heap | Zeroed 16..4096-byte buffers, calloc/realloc/free, page sharing/reclamation; Pi self-tests verified | Large allocations, IRQ/SMP synchronization and additional consumers |
| Virtual memory/protection | Pi-verified private pages, dynamic RW/NX allocation, shared memory, file-backed snapshots, demand-zero reservations and broadcast TLB invalidation. [Core VM and SMP](core-vm-smp.md) | Larger address spaces, copy-on-write and broader concurrency and mapping stress tests |
| Interrupts | Pi-verified GICv2/PPI30, IRQ return and BCM compare channel 3 through the bounded SPI dispatcher and deferred callbacks. [Peripheral IRQs](peripheral-irqs.md) | Additional interrupt-driven drivers, IRQ-safe locks and SMP |
| Time | Counter, uptime, 100 Hz CNTP IRQ and cooperative sleep verified on Pi. The Pi passed operator-set UTC, the EL0 query, and trust validity preflight. [UTC and trust guide](trust-time.md) | RTC and automatic time synchronization |
| Scheduling | Cooperative EL1 tasks. Pi-verified weighted EL0 preemption, nested process trees and secondary EL0 batches. Normal shell and ELF runs use all four CPUs in QEMU with the SMP interrupt routes enabled. [Core VM and SMP](core-vm-smp.md) | Pi verification of four-way scheduling and concurrent kernel work |
| Processes/system calls | Pi-verified descriptor lifecycle, seven-stage pipelines, named IPC, descriptor passing, signals, structured stop reports and shared VM. [Core OS milestones](remaining-os-roadmap.md#core-os-milestones) | Broader process credentials and access control |
| Programs/runtime | Bounded ELF loader, 94 userland ELF programs, 102 syscall IDs (0–101), 8 foreground process slots and 3 supervised service slots. The Pi verified named app lifecycle, signed packages, and a pinned HTTPS ELF download. [Storage and apps](storage-apps.md) · [Package download test](package-download.md) | General libc, dynamic linking, and repository catalog discovery |
| Filesystem namespace | RAMFS `/`, read-only FAT32 `/boot`, opt-in `/data`; 16 owned generation-checked file handles with path leases, exclusive creation, append, metadata, positioned I/O, and resize. [File handles](file-handles.md#phase-27-el0-file-handles) | General mounts, permissions, pseudo-filesystems and preemptive driver synchronization |
| Persistent files | The Pi verified v4 archive import into v5, cold-power retention and staged-write recovery on spare media. V5 supports 32 nodes and 32 KiB files. [Storage and apps](storage-apps.md) | Larger volumes, richer metadata and repair tools |
| Shell | Pi-verified redirection, command lists, PID selection, `source`, four background jobs and seven-stage binary pipelines. [Shell workflows](shell-workflows.md) | Pi verification of the integrated local console |
| Networking | WPA3/SAE, DHCP, ARP, IPv4, ICMP, DNS, UDP and outbound TCP. The Pi passed wired UDP echo, a checksummed 16 KiB TCP exchange with half-close, and a wired DHCP bind. [Background Wi-Fi](background-networking.md) · [TCP sockets](tcp-sockets.md) · [Wired DHCP](../proofs/pi5-wired-dhcp-bound-2026-11-27.txt) | Group key rotation, broader DHCP renewal/rebinding coverage and general HTTP |
| Security | Reservation-aware allocation, hidden volatile credentials, kernel/user page isolation, RW/NX and checked user copies. The Pi passed pinned certificate, TLS transcript and live TLS 1.3 encrypted echo checks. [Live TLS test](tls-live.md) | Credentials/permissions, broader access control, syscall/driver hardening, secure update/boot design and independent review |
| Multicore/concurrency | The Pi verified three EL1 workers, SGIs, cancellation, page leases, and TLB invalidation on four CPUs. The boot CPU brokers kernel work. [Secondary CPU workers](secondary-cpus.md) | Broader interrupt affinity, concurrent drivers and kernel synchronization |
| Diagnostics/testing | UART logs, self-tests, native fixtures, QEMU A53/A76/EL1/EL2 and image checks. A GitHub Actions workflow runs verification on pushes and pull requests. [Workflow](../.github/workflows/verify.yml) | Hosted CI result, longer hardware tests, coverage and debugging tools |
| Build/update/install | Windows-native build, FAT image and checked boot updates, explicit backup conversion, and an on-device signed multi-file package installer. [Storage and apps](storage-apps.md) | General OS installer, automated upgrade recovery and online catalog discovery |

The v4 format supports 16 persistent nodes and 16 KiB files. The v5 format
supports 32 persistent nodes and 32 KiB files. The volatile root `/` uses
RAMFS. Persistent files live under `/data`. A persistent root and general
device and process namespaces remain open work.

## Drivers and platform support

| Driver/interface | Status and scope |
| --- | --- |
| PL011 serial UART | Working on QEMU and the tested Pi 5 RP1 GPIO14/15 header console; firmware setup preserved on Pi |
| Architectural timer | Counter/frequency and 100 Hz NS physical timer IRQ via PPI30, `timerstart`/`timerstop` and asynchronous register-preservation test verified on Pi |
| Interrupt controller (GIC) | Pi-verified GICv2 timer, peripheral SPI, and banked SGI paths for secondary workers. The driver preserves full IAR/EOIR tokens and quarantines unexpected sources. |
| Device-tree reader | Bounded memory/reservation and selected device/property readers; not a general driver bus |
| RNG200 | Pi hardware source, conditioning and engineering failure checks implemented; used by SAE/DNS/ping |
| PCIe/RP1 | Uses validated firmware-selected Pi windows/DMA handoff; no general PCIe reset/enumeration/hotplug driver |
| xHCI USB host | Experimental polled controller ownership/DMA, direct USB2 enumeration and transfers; verified with the test pendrive |
| USB mass storage | BOT + SCSI sectors/capacity, WRITE10 and WCE=0 synchronous `/data` writes verified on Pi; no UAS or general USB3/hub support |
| QEMU virtio block | Read-only block path tested in QEMU; not a Pi hardware driver |
| Pi SDHCI storage | Pi-verified read-only microSD initialization and reads at the first and last sectors. [MicroSD capture](../proofs/pi5-microsd-tail-2026-11-24.txt) |
| Wi-Fi SDIO | Pi power/pins/400 kHz SDIO/F1/F2/backplane/firmware/FullMAC control implemented and verified |
| Wi-Fi station/authentication | WPA3-Personal SAE + required PMF and data key setup, foreground DHCP/DNS/ping verified on Jack's; not general long-lived station management |
| GPIO/pin control | Limited WLAN power/mux setup and firmware-initialized UART; no general user GPIO API |
| USB keyboard/mouse (HID) | Pi-verified direct boot-keyboard reports. Integrated shell input and reconnect passed QEMU with USB boot storage. General HID and mouse input remain open. [Keyboard capture](../proofs/pi5-usb-keyboard-live-sd-host-2026-11-24.txt) · [Local console](hdmi-framebuffer.md) |
| HDMI/display/framebuffer | Firmware framebuffer discovery, text rendering and keyboard-driven shell startup. Physical HDMI verification is pending. [HDMI test](hdmi-framebuffer.md) |
| Graphics/GPU acceleration | Optional later desktop capability; not implemented |
| Ethernet | Pi 5 PHY, GEM DMA, UDP, TCP, configurable IPv4 and inbound ARP and ICMP replies verified on a direct cable. [Platform driver plan](platform-driver-plan.md#phase-18-configurable-ethernet-ipv4-and-inbound-reachability) |
| Bluetooth | Optional; not implemented |
| Audio | Firmware media inventory and stereo tone generation passed on the Pi. The Phase 24 batch verified trim, gain, downmix, fade, and join by hash. Playback and capture are later work. [Pi capture](../proofs/pi5-serial-batch-pass-2026-11-24.txt) |
| I2C/SPI/PWM/general GPIO | Add for attached peripherals as needed; not general-purpose drivers yet |
| RTC/watchdog/power/reboot | Pi RTC write/readback, UTC sync, throttle flags, and storage remount after a firmware halt request were observed. The updated alarm query returned the disabled state on the Pi. [Phase 20 plan](platform-driver-plan.md#phase-20-rtc-and-power) · [Alarm capture](../proofs/pi5-media-pass-2026-11-24.txt) |
| Thermal/fan management | Firmware may manage hardware during boot; no OurOS management policy/driver |
| Camera, touch, other sensors | Optional board/device-specific work after core services |
| NVMe/SATA/other storage | Optional extra transports, not covered by the USB BOT or SD handoff driver |

A driver for one device class does not support every USB device. The Pi 5's
RP1/peripheral setup also does not automatically apply to Pi Zero 2 W, Jetson,
x86 laptops or RISC-V boards. Our shared C services are reusable; each platform
still needs its own boot, interrupt, peripheral and device implementations.

## Development order

The [extended roadmap](remaining-os-roadmap.md#extended-plan) tracks the
next stages for local hardware, system installation, networking and drivers.
