# Remaining operating-system work

OurOS runs a serial shell, protected EL0 applications, and bounded persistent
storage on the boot CPU. The MMU, exceptions, GIC/timer IRQs, cooperative
kernel tasks, preempted user programs, USB writes, ELF loading, and checked
system calls have Pi evidence. The Pi verified secondary worker startup,
same-boot recovery, cancellation, shutdown, and interprocessor
interrupts. The Pi passed shared work queues and remaps with parked
workers. The Pi passed Phase 46 worker leases with three workers and one
parked core. The Pi verified Phase 47 constrained page allocation, trimming
and cleanup. Phase 48 exposes aggregate system statistics to EL0.
[Secondary CPU workers](secondary-cpus.md) · [System statistics](system-stats.md)

The [Pi allocator capture](../proofs/pi5-user-allocator-2026-11-15.txt) passes
HEAPCHK and HEAPDEMO with exit 0, no remaining private pages and restored
resources. This accepts the default byte-allocation checks on real hardware.
The [status checklist](os-status.md) links the earlier protection, scheduling,
storage, shell and networking evidence.

The [UART-first platform driver plan](platform-driver-plan.md) tracks
hardware discovery, driver ownership and extra devices for physical tests.

## Core OS milestones

Phase 29 and Phase 30 completed the planned Core OS milestones.
Both QEMU profiles and the Raspberry Pi 5 acceptance runs passed.

| Milestone | Implementation and acceptance |
| --- | --- |
| Process capacity and pipelines | Phase 29 implements five foreground EL0 slots, four-stage pipelines, and a five-entry process snapshot. Pi verification passed all three pipe rings, process teardown, and recovery of page and handle resources. |
| Descriptor passing and IPC | Phase 30 adds direct parent–child mailboxes and file-rights reduction. Pi checks cover queue limits, stale peers, close-on-exec, and queued descriptor cleanup. |
| Blocking I/O | Phase 30 adds timed blocking sends and receives with backpressure, interruption, and peer shutdown. |
| Background services | Phase 30 adds one supervised EL0 service that runs alongside foreground jobs and across shell sessions. The Pi check covers restart, heartbeat progress, and explicit shutdown. |
| Signals and fault reports | Phase 30 adds parent-targeted signal delivery and structured stop records. The Pi check distinguishes signal termination, data aborts, and deadlines. |
| Larger and shared virtual memory | Phase 30 adds larger anonymous regions and explicit permission-limited child mappings, with non-executable pages and owner cleanup. |

[Phase 29 Pi capture](../proofs/pi5-pipe-chain-2026-11-24.txt) · [Phase 30 Pi capture](../proofs/pi5-core-os-2026-11-24.txt)

Phase 33 adds nested ordinary processes, descendant cleanup, and serial
terminal handoff across waiting parents. The Pi passed subtree cleanup and
redirected input. Phase 34 adds a longer terminal retest, per-child scheduler
priorities, and tick accounting. The Pi passed the priority swap and nested
terminal input. The Pi passed Phase 35's seven-stage pipeline and four-job
run and Phase 36's private file snapshots, protection, revocation, and mapped
binary transfer. The Pi passed Phase 37's named IPC, including connections
across process trees, descriptor transfer, and echo before and after a shell
exit. The Pi passed Phase 38's multi-source waits and concurrent echo
clients. The Pi passed Phase 39's service dependencies and independent
restart. The Pi passed Phase 40's inherited and irreversible limits on
four classes of process operations. Phase 41 brings up secondary CPUs
as opt-in EL1 workers. The Pi verified initial startup and fault
isolation. Firmware left a secondary pending on restart. The Pi
passed Phase 42's in-kernel worker recovery and cancellable batches.
The Pi passed Phase 43 targeted and broadcast GICv2 interrupts.
The Pi passed Phase 44's TLB invalidation and shared kernel remap
with all three workers online. The Pi passed Phase 45's shared
job queue, cancellation and remaps with parked workers. Phase 46
adds a boot-CPU broker for secondary physical memory requests.
[Process trees](process-control.md#phase-33-process-trees)
· [EL0 scheduling](process-control.md#phase-34-el0-scheduling) · [Process capacity](process-control.md#phase-35-process-capacity)
· [File mappings](user-memory.md#phase-36-file-snapshots-and-shared-views)
· [Named IPC](process-control.md#phase-37-named-ipc)
· [Event waits](process-control.md#phase-38-event-waits)
· [Named services](process-control.md#phase-39-named-services)
· [Process permissions](process-control.md#phase-40-process-permissions)
· [Secondary CPU workers](secondary-cpus.md)

## Other open work

This check uses the November 22 implementation plan, its later platform
increments, and the recorded Pi captures. The Phase 27 file-handle test
passed on Pi using RAMFS. Phase 28 passed its Pi descriptor and exec run.
[Implementation plan](implementation-plan.md) · [Pi proof index](../proofs/README.md)

### Core OS and storage

| Area | Open tasks |
| --- | --- |
| Processes and ABI | Eight foreground slots, four background jobs, a process snapshot with scheduler counters, and Pi-verified named IPC channels. Scheduling across CPUs remains open. |
| Memory and concurrency | Phase 36 adds eager private file mappings and shared-view revocation. Phase 46 adds aligned runs and bounded owned leases on all secondaries. Pi lease acceptance, demand paging, out-of-memory policy, ownership beyond worker leases, DMA cache maintenance, and shootdown for general mapping updates remain open. Verify exclusive operations on a cacheable Pi mapping. |
| Data and recovery | The Pi verified Phase 31 file recovery and Phase 32 v5 retention after a cold power cycle. Physical v4-to-v5 migration and interrupted-write recovery on spare media remain open. [Storage and apps](storage-apps.md) |
| Apps and distribution | The Pi passed named-app install, upgrade, rollback, and uninstall, plus signed multi-file installation and dependency upgrade. [Storage and apps](storage-apps.md) |
| Networking | Test Wi-Fi DHCP broadcast rebinding, AP group-key rotation, same-boot manual restart after cleanup, and intentional UDP timeout and recovery. Add wired DHCP and route selection across Wi-Fi and Ethernet, and verify link recovery and interface changes. Direct wired ARP, ICMP, UDP, and TCP have passed on the Pi. |
| Platform maturity | Broaden the libc and ELF/toolchain support, process credentials and filesystem permissions, update and boot trust, CI coverage, driver soak tests, and board bring-up beyond the Pi 5 and QEMU profiles. |

### Hardware and local UI

| Plan phase | Open tasks |
| --- | --- |
| 11 and 22 · Display and media | Verify the HDMI pattern, scroll, and UART mirror once a cable is available. Extend local terminal input and rendering. Add playback and capture for a selected audio device, camera capture, DSI scanout, and GPU integration as hardware becomes available. |
| 13–14 · Bus and IRQ | Extend driver binding and clock/reset/regulator ownership. Complete RP1 endpoint MSI-X mapping and live IRQ delivery, and test DMA coherency with caches enabled. |
| 15 · Header buses | Repeat the wired GPIO loopback with the series resistor. Add GPIO edge delivery, pinmux, extra UART loopback, and device-backed I²C, SPI, PWM, and PIO tests. |
| 16 · USB | Extend the verified direct-port boot-keyboard report to shell input. Add detach/reconnect acceptance, mouse/report support, hubs, and USB3/UAS. |
| 17 · SD and Bluetooth | Extend the verified read-only microSD path with removal/reinsertion tests, full cold initialization, and write/flush checks on a spare card. Add Bluetooth HCI discovery and controller readback. |
| 19 · External PCIe | Train the link with an attached endpoint, enumerate it, allocate BARs, confirm DMA mapping, and implement NVMe discovery and read-only queues before testing writes on spare storage. |
| 20 · RTC and power | Verify battery-backed RTC retention, implement and test alarm programming and wake, and add watchdog and fan controls with ownership and thermal checks. |

The sections below retain the original milestones and detail the
subsystem foundations for those follow-up tasks.

Remaining work extends these foundations. It does not restart interrupts,
multitasking or writable storage from zero. Each table below separates what
exists from the missing capability. A passing same-boot file test is not yet
a physical power-loss recovery or reboot-persistence test.

The [process-control phase](process-control.md) now has Pi PS check/demo evidence.
The [shell-job phase](shell-jobs.md) implements one bg/bgio child, jobs,
fg and stop in the normal shell. Both emulator profiles pass; the
[new Pi excerpt](../proofs/pi5-shell-jobs-2026-11-15.txt) now verifies the base
bg/jobs/stop/exit workflow with a responsive parent and restored resources.
fg/bgio and broader physical lifecycle/reboot checks remain open. The
shell-job kernel/API and data formats stay unchanged. The next
[v4 storage phase](data-v4.md) adds 16 KiB files, mount queries and explicit
legacy backup conversion. Native and QEMU checks pass. The Pi now verifies
a v4 mount and mount queries. Multiple
jobs, process trees and indefinite services are still work.

## Applications and multitasking

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Scheduling | Cooperative EL1 tasks, eight foreground EL0 slots, and three supervised service slots with startup dependencies. The Pi verified named services, pipelines, and job controls. | Multicore scheduling |
| Process memory | Private image, stack and heap pages, shared regions and private file snapshots, plus Pi-verified mapping controls | Broader lifecycle checks, copy-on-write and OOM policy |
| Exceptions | Contained EL0 faults and deadlines, targeted signals, and structured stop records | Debugger integration and extended signal policies |
| System calls | Checked file, terminal, process, and socket I/O, blocking IPC, local event waits, and Pi-verified inherited process permissions | Asynchronous storage and process credentials |
| Executables | Static AArch64 ELF subset and compact runtime. The current shell adds command lists and bounded text scripts. [Shell workflows](shell-workflows.md) | Pi broader exec/update validation, stored-shell persistence, larger VM and broader toolchain tests |
| Concurrency | The boot CPU runs the scheduler and drivers. Three EL1 workers passed shared queues, cancellation and parked remaps on the Pi. Phase 46 adds mailboxes for worker page leases. | Phase 46 Pi acceptance, IRQ affinity for drivers, concurrent drivers and multicore scheduling |

Acceptance should keep checking resource restoration, failure containment and
storage integrity while expanding the scheduler. A larger application runtime
should not assume that arbitrary EL1 driver code is already preemption-safe.

## Persistent files and installation

The writable root remains a volatile RAM filesystem. `/boot` is read-only
FAT32. `/data` is an original bounded snapshot filesystem: new v4 volumes have
sixteen shared file/directory slots and nested files up to 16384 bytes; v3 keeps
the same node count and nested 8192-byte files; v2 keeps
sixteen flat files of 8192 bytes and v1 keeps eight flat files of 4096 bytes.
All use two checked commit banks in a separate 1 MiB partition. The
kernel does not expose writable FAT metadata. The snapshot implementation
retains the previous complete bank and refuses further updates after an
ambiguous I/O failure. Physical persistence across an actual power cycle must
still be checked; successful same-session write/read/sync is narrower evidence.

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Writable storage | USB2 writes with checked flushes, nested directories, atomic publication, Pi-verified v4 retention, and Pi-verified v5 startup with 32 KiB file limits | Pi v5 migration and recovery, larger volumes, richer metadata, a general writable filesystem and more block-device drivers |
| Recovery | SHA-256 and two commit banks, native interrupted-I/O tests, serial export and restore, and explicit v4-to-v5 migration | Pi backup/export and power-cut tests, richer recovery console, repair and automatic preservation policy |
| Local installation | Path-based ELF install and update, plus named app staging, listing, rollback and uninstall tested on v4 and v5. [Local app lifecycle](app-lifecycle.md) | Pi managed-app checks, a general OS installer, layout planning and multi-file upgrades |
| Packages | Executable files can be stored and loaded locally | Package format/manifests, versions, dependencies, integrity/authenticity checks, transactions and repository metadata |
| Downloads | Foreground Wi-Fi/IP/UDP DNS/ICMP foundations | Reliable background networking, TCP, HTTP/TLS, certificate/time policy, signatures and authenticated repositories |

The small executable installer is not a package manager or a network download
command. A package manager depends on storage limits/recovery, application ABI,
metadata and authenticity decisions. Those dependencies should be implemented
and tested before accepting remote packages.

Full-image reflashing replaces `/data`; persistence refers to boots of an
installed image. New dataexport/host conversion and explicit datarestore can
carry same-format files across images. This requires a saved backup and an
empty destination; automatic preservation, merging and in-place migration remain pending.

## Local hardware and UI

The header UART provides input and diagnostics. The opt-in HDMI text
mirror is ready for physical scanout testing. Keep UART available
during that test and the later USB input work. [HDMI test](hdmi-framebuffer.md)

| Layer | Next implementation and acceptance |
| --- | --- |
| Display discovery | Validate the Pi 5 firmware handoff and reserved scanout memory on hardware |
| Framebuffer console | Verify the color pattern, font, scrolling and UART mirror on HDMI |
| USB input transport | Extend host transfers/enumeration for hubs and interrupt-IN endpoints without breaking boot storage; bounded disconnect/error recovery |
| Keyboard/mouse HID | Descriptor/report parsing, boot-protocol fallback where appropriate, key/button transitions, keyboard layout and input event queue |
| Terminal/UI | Console input discipline, cursor/selection policy, text shell integration, later a window/compositor model if desired |
| GPU | Research the actual Pi 5 graphics hardware/firmware interface, command/memory validation, synchronization and display integration; framebuffer output alone does not implement this |
| Audio | Identify the chosen output path/device, then clocks, DMA, buffering and audio stream API; no universal Pi audio interface is assumed |
| General peripherals | GPIO/pinctrl, clocks/reset/power, I2C/SPI, DMA and device-specific bindings as actual use cases require |

Ethernet, USB3/UAS, SD cold initialization/write support, removable-device
handling, RTC/time, Bluetooth and other peripherals are additional driver
projects. The existing USB2 mass-storage path does not imply that arbitrary
USB classes or topologies are supported.

## Work order and evidence

The Pi passed safe reboot with file retention, two shell jobs, and bounded
command lists. The Phase 5 storage prototype passed native and emulator
tests and booted on Pi with the expected v5 mount limits. The Pi also verified
the Phase 6 app listing, the Phase 7 worker tests, background gateway pings,
three DHCP renewals, link-loss cleanup, and automatic reconnection to a
changed BSSID. A manual same-boot join after cleanup still awaits Pi
verification. The native model passes group-key rotation during a session.
Pi rotation still needs an AP that rotates keys while the Pi stays online.
[Storage prototype](data-v5.md) · [Local app lifecycle](app-lifecycle.md) · [Background Wi-Fi](background-networking.md) · [Implementation plan](implementation-plan.md)
