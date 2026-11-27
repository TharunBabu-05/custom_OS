# Remaining operating-system work

OurOS runs a serial shell, protected EL0 applications, and bounded persistent
storage. EL0 workloads can use four CPUs while the boot CPU handles syscalls
and drivers. The MMU, exceptions, GIC/timer IRQs, cooperative
kernel tasks, preempted user programs, USB writes, ELF loading, and checked
system calls have Pi evidence. The Pi verified secondary worker startup,
same-boot recovery, cancellation, shutdown, and interprocessor
interrupts. The Pi passed shared work queues and remaps with parked
workers. The Pi passed Phase 46 worker leases with three workers and one
parked core. The Pi verified Phase 47 constrained page allocation, trimming
and cleanup. The Pi passed Phase 48 system statistics and timer sampling.
The Pi verified Phase 49 process history, ring wraparound, and cleanup.
The Pi passed Phase 50 process timers and service cleanup. Phase 51 adds
TCP and UDP readiness to the same EL0 wait set. The Pi passed the wired UDP
test. Phase 52 adds a shared deadline-aware TCP runtime, verified on the
Pi with the wired TCP echo. The Pi passed Phase 53 TCP write half-close
and the final reply after FIN. Phase 54 adds transport session statistics,
owned socket snapshots and the `netstat` command. The Pi passed the idle
snapshot and permission checks. Phase 55 adds RP1 Ethernet carrier
monitoring, socket teardown on loss and automatic restart on reconnection.
The Pi passed carrier recovery and TCP half-close. Phase 56 adds bounded
UDP probes, retry reporting and a Windows batch echo listener. The Pi
matched all three probes. The Phase 57 hardware retest passed the UDP
batch with zero repeats or queue drops and the 16 KiB TCP half-close.
The Pi verified Phase 58 DHCP cancellation and cleanup. Phase 59 adds
ARP conflict detection and activation of a wired TCP/UDP lease for the
local subnet. The Pi timed out with three completed Discover frames and
zero RX frames. Phase 60 fixes shell cleanup accounting across DHCP
teardown and adds a bounded DHCP wait with explicit active IPv4 reporting.
The Pi verified the cleanup regression. Phase 61 adds wired renewal,
rebinding, and active lease counters. The Phase 61 hardware run booted,
negotiated at 100 Mbps, reached a DHCP timeout, and released Ethernet
resources. Storage checks passed. The Windows reply from `192.0.1.1`
has no matching OurOS lease or `ethaddr ip` output in that run.
[Phase 61 capture](../proofs/pi5-wired-renewal-timeout-2026-11-27.txt)
[Secondary CPU workers](secondary-cpus.md) · [System statistics](system-stats.md) · [Process history](process-events.md) · [Process timers](user-timers.md) · [Socket readiness](socket-readiness.md)

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
| Processes and ABI | The Pi passed Phase 63 multicore EL0 batches and the opt-in multicore shell. Secondary services and per-CPU run queues are later work. [Core phase](core-vm-smp.md) |
| Memory and concurrency | The Pi passed private demand-zero pages, broadcast TLB invalidation, and two cached RP1 DMA transfers. Production DMA driver adoption is tracked in Phase 66. [Core phase](core-vm-smp.md) |
| Data and recovery | The Pi verified Phase 31 file recovery, Phase 32 v5 retention after a cold power cycle, and Phase 65 v4 archive import into v5 with staged power-cut recovery and a successful subsequent write. [Storage and apps](storage-apps.md) |
| Apps and distribution | The Pi passed the named-app lifecycle and signed multi-file installation, dependency upgrade and removal. Remaining work includes repository discovery, key rotation, a general OS installer and boot-update trust. [Storage and apps](storage-apps.md) |
| Networking | The Pi bound a wired DHCP lease and Windows ACKed three T1 renewals. Capture Pi renewal counters and verify rebinding, NAK and expiry on the direct cable. Add route selection across Wi-Fi and Ethernet. Test Wi-Fi DHCP broadcast rebinding, AP group-key rotation, and same-boot manual restart after cleanup. [Wired lease capture](../proofs/pi5-wired-dhcp-bound-2026-11-27.txt) |
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

Earlier [process control](process-control.md), [shell jobs](shell-jobs.md),
and [v4 capacity](data-v4.md) phases remain recorded in their guides.
The later process-tree, four-job and service runs above supersede the
initial single-job acceptance scope.

## Applications and multitasking

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Scheduling | Cooperative EL1 tasks, eight foreground EL0 slots, and three supervised service slots with startup dependencies. The Pi verified named services, pipelines, job controls, and four concurrent EL0 slices. | Local preemption timers and ordinary shell acceptance with workers online |
| Process memory | Private image, stack and heap pages, shared regions and private file snapshots, plus Pi-verified mapping controls | Broader lifecycle checks, copy-on-write and OOM policy |
| Exceptions | Contained EL0 faults and deadlines, targeted signals, and structured stop records | Debugger integration and extended signal policies |
| System calls | Checked file, terminal, process, and socket I/O, blocking IPC, local event waits, and Pi-verified inherited process permissions | Asynchronous storage and process credentials |
| Executables | Static AArch64 ELF subset and compact runtime. The current shell adds command lists and bounded text scripts. [Shell workflows](shell-workflows.md) | Pi broader exec/update validation, stored-shell persistence, larger VM and broader toolchain tests |
| Concurrency | The boot CPU brokers traps and drivers. Three secondary CPUs run EL0 slices and passed shared queues, cancellation, parked remaps and scoped page leases on the Pi. | IRQ affinity, subsystem locking and concurrent kernel execution |

Acceptance should keep checking resource restoration, failure containment and
storage integrity while expanding the scheduler. A larger application runtime
should not assume that arbitrary EL1 driver code is already preemption-safe.

## Persistent files and installation

The writable root is volatile RAMFS and `/boot` is read-only FAT32.
`/data` supports legacy snapshots and extent-backed v5. A v4 volume stores
16 nodes and files up to 16 KiB. A v5 volume stores up to 32 nodes and
32 KiB files. Both use two checked commit banks in the separate data
partition. The Pi verified v5 retention through a clean power cycle.

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Writable storage | USB2 writes with checked flushes, nested directories, atomic publication and Pi-verified v4/v5 retention | Larger volumes, richer metadata, a general writable filesystem and more block-device drivers |
| Recovery | SHA-256 and two commit banks, serial export and restore, plus Pi-verified v4-to-v5 archive import and staged power-cut recovery on spare media. [Physical capture](../proofs/pi5-storage-migration-power-cut-2026-11-27.txt) | Richer repair tools and broader interruption tests |
| Local installation | Pi-verified named app staging, launch, upgrade, rollback and uninstall. [Local app lifecycle](app-lifecycle.md) | A general OS installer, partition layout planning and an end-to-end recovery workflow |
| Packages | Signed local repository, digest checks, transactional multi-file install and exact-version dependency upgrades on the Pi | Network catalog discovery, signer rotation and broader dependency resolution |
| Downloads | Pi-verified pinned TLS and hash-checked ELF download. A signed archive can be fetched with the existing bounded transport. [Package download](package-download.md) | General repository transport, online trust policy and a Pi end-to-end signed catalog refresh |

Full-image reflashing replaces `/data`; persistence refers to boots of an
installed image. New dataexport/host conversion and explicit datarestore can
carry same-format files across images. This requires a saved backup and an
empty destination; automatic preservation, merging and in-place migration remain pending.

## Local hardware and UI

The header build can start a local shell on a firmware framebuffer with
a direct USB2 boot keyboard. QEMU verified keyboard input and reconnect
alongside boot storage. Physical HDMI scanout and the integrated local
shell are ready for Pi testing. [HDMI test](hdmi-framebuffer.md)

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

## Extended plan

Reviewed on 2026-11-28. The original Core OS milestones, clean v5 retention,
named-app lifecycle, signed packages, and v4-to-v5 recovery on a spare
drive have Pi evidence. A four-process EL0 batch now ran concurrently on
the boot CPU and all three workers. [Four-CPU capture](../proofs/pi5-four-cpu-el0-2026-11-28.txt)

| Next phase | Deliverable | Verification |
| --- | --- | --- |
| 62 · Wired DHCP acceptance | The single-client Windows fixture is implemented and passed loopback and OurOS packet interoperability. Use the existing direct cable and Phase 61 kernel. [Fixture](socket-readiness.md#phase-62-direct-cable-dhcp-acceptance) | Match Windows replies with the Pi's lease and address. Confirm echo, renewal, rebinding, and rejection or expiry, then release the Ethernet claims and check storage. |
| 63 · Core OS foundations | Move the USB DMA workspace out of the early image and add secondary EL0 batches, demand-zero pages, broadcast TLB invalidation, and a cached RP1 DMA transfer. | Passed on the Pi, including multicore jobs, four-core remaps, two cached copies, and USB and `/data` checks after cleanup. [Core phase](core-vm-smp.md) |
| 64 · Network interfaces and routing | Add explicit interface and route selection, local and default routes, then wired gateway and DNS support. Track socket behavior when the selected path disappears. | Test an on-link peer and a routed peer. Verify link change, stale route removal, socket outcomes, and Wi-Fi coexistence on the Pi. |
| 65 · Storage acceptance and recovery | Run the v4-to-v5 archive import and staged interruption on spare media. | Passed on the Pi. The import matched the fixture, the physical cut preserved generation 4, and the next write committed generation 5. [Storage capture](../proofs/pi5-storage-migration-power-cut-2026-11-27.txt) |
| 66 · Multicore and driver integration | Normal foreground and shell sessions use the boot CPU and routed secondary workers, with ready services eligible during those rounds. IRQ affinity, broader kernel and driver concurrency, and production cache-aware DMA remain open. | QEMU passed pipelines, faults, and cleanup. The Pi recorded four simultaneous slices and released the worker resources. The ordinary shell with workers enabled still needs its Pi run. [Scheduler](core-vm-smp.md#current-scheduler) |
| 67 · Release and installer | Add an install/upgrade recovery path, repository catalog refresh and signer rotation. Extend process credentials and filesystem permissions around that workflow. | Install on a spare drive, verify user isolation and signature failures, and recover a known-good boot after an interrupted or refused update. |
| 68 · Local input and display | Startup enables firmware HDMI scanout and USB boot-keyboard console input on a spare direct port of the USB storage controller. | QEMU passed shell typing and reconnect, framebuffer rendering, and storage regression checks. Pi acceptance requires a connected display and keyboard. [Local console](hdmi-framebuffer.md) |
| 69 · Device drivers | Continue the platform sequence with an attached peripheral. Candidate increments include GPIO IRQ and buses, USB hubs and mouse, SD write/flush, Bluetooth HCI, PCIe/NVMe, and RTC alarms, fan and watchdog. | Use the relevant device on the Pi. Exercise its success, detach or failure path and report restored claims. [Platform driver plan](platform-driver-plan.md) |
| 70 · Media and additional boards | Add an actual audio path, then evaluate camera and DSI/GPU work against available hardware. Expand beyond the Pi 5 and QEMU after the relevant boot and driver paths are defined. | Play and capture known samples, verify display or camera frames, and repeat boot, timer, memory and storage tests on each new board. |

Storage and peripheral acceptance can proceed while the network and
scheduler tracks advance. Each increment records the command output,
native or emulator checks where applicable, and the Pi result for its
hardware-facing behavior.

## Core OS expansion sequence

The current kernel switches roots with a full local stage-1 invalidation.
RAM is mapped non-cacheable except in the scoped RP1 DMA test. The bundled
ELF build emits one RX load segment. The loader already accepts disjoint
RX, RO, and RW segments and zero-fills backed pages within a fixed arena.
Timer readiness already reaches the event wait API through the scheduler's
checks. Keep these working interfaces and measure gains on specific Pi
workloads and hardware configurations.

| Order | Work | Acceptance |
| --- | --- | --- |
| 1. Memory routines and baseline | Share integer-only copy and fill between the kernel and EL0. Use aligned 64-bit transfers and byte fallbacks. Add a counter-based benchmark. Record boot and ordinary shell measurements on the same Pi revision before comparing later changes. | Guarded host alignment and length checks, unchanged ELFs' load contract, QEMU monitor and app checks, Pi `membench` results. |
| 2. Tagged address spaces | Start with the broadly supported 8-bit ASID path. Reserve the kernel tag, define process tag allocation and reuse, and distinguish switch, map update, and teardown invalidation. Retain broad invalidation for shared/global mappings. | Different processes use the same virtual address without aliasing. Stress ASID reuse, exec, process exit, remap and permission revocation across CPUs. Compare switch cost under the same workload. |
| 3. Cacheable RAM and DMA | Inventory DMA mappings and every ownership transfer. Keep shared controller descriptors and buffers in dedicated non-cacheable pools where practical. Add explicit clean/invalidate and ordering for cacheable DMA buffers. Enable cacheable CPU RAM in a separate test profile before changing the default, with consistent attributes for every alias and core. | Exercise USB BOT read, write, flush and recovery, GEM traffic and link recovery, and Wi-Fi transfers. Compare end-to-end checksums and repeat SMP and power-cut tests on the Pi. |
| 4. Timers and kernel concurrency | Add a local scheduler timer per CPU, then one-shot wakeups and a deadline queue behind the existing timer and event wait APIs. Define lock order and IRQ rules before moving PMM, process and selected syscall work to workers. Keep drivers under their existing owner until their locking is verified. Test RTC alarms and fan control as separate platform increments. | Verify expiry, cancellation, timer races, IRQ nesting and process faults under concurrent load. Measure wake latency and idle interrupts. The counter tick period is distinct from interrupt delivery latency. |
| 5. VM and applications | Allocate page-table levels on demand. Add a static build profile exercising the loader's RX, RO and RW support and BSS zeroing. Retain compatibility with old ELFs. Add bounded stack growth and reference counts before copy-on-write. Measure allocator scans before choosing a buddy allocator or per-CPU caches, and add larger kernel allocations with quotas. Introduce unified file descriptors with compatibility wrappers, then grow process capacity and the userspace libc subset. | Load old apps and an ELF with writable globals. Reject overlapping segments and writable code, verify read-only sharing and CoW under memory pressure, and stress descriptor lifetime and large process trees. |
| 6. Terminal | Store screen cells in RAM and render dirty cells to HDMI. Introduce a bounded ANSI parser and then cursor editing, recall and completion. Add input rings with independent UART and USB producer ownership. Extend canonical and raw modes and add Ctrl+Z after process suspension is verified. | Verify old serial sessions, HDMI scroll/rendering, input bursts, reconnect, line editing and escape parser bounds. Confirm both UART and local use on the Pi. |
| 7. Storage | Add a reusable block cache with explicit flush semantics. Prototype a larger extent-backed format alongside v5 on spare media, with migration and recovery. Test microSD writes on a supported spare card. Develop NVMe after external PCIe enumeration and DMA are stable and an adapter is available. | Exercise large files, full disks, write ordering, interrupted flushes and recovery. Retain a v5 read/import path and measure verified throughput on each transport. |

Pi acceptance can proceed alongside the implementation increments. The
next boot capture should show the EEPROM's DTB load address and the kernel's
retained address. `fbinfo` and `usbkbd` identify the display and input paths
after the keyboard-reconnect check. Wired DHCP rebinding and route selection
remain in the networking track and do not depend on the memory changes.
[Arm memory management](https://developer.arm.com/documentation/101811/latest/) ·
[Arm Generic Timer](https://developer.arm.com/documentation/102379/latest/) ·
[AArch64 ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)

### Memory benchmark

Run `membench` at the Pi monitor. It compares byte and word paths over
128 passes of one 4 KiB page per case. It reports the architectural counter
frequency and elapsed ticks, checks the results, then frees both scratch
pages. Repeat on the same boot configuration before comparing results.
QEMU checks verify correctness and cleanup. Use physical counter readings
for speed claims.
