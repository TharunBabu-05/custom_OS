# Remaining operating-system work

Current state: **an early single-core OS with protected EL0 applications, a
serial shell and bounded persistent storage**. The MMU, exceptions, GIC/timer
IRQs, cooperative kernel tasks, preempted user programs, USB writes, ELF loading
and checked system calls are implemented and have Pi evidence.

The [latest Pi capture](../proofs/pi5-user-allocator-2026-11-15.txt) now passes
HEAPCHK and HEAPDEMO with exit 0, no remaining private pages and restored
resources. This accepts the default byte-allocation checks on real hardware.
The [status checklist](os-status.md) links the earlier protection, scheduling,
storage, shell and networking evidence.

Remaining work extends these foundations. It does not restart interrupts,
multitasking or writable storage from zero. Each table below separates what
exists from the missing capability. A passing same-boot file test is not yet
a physical power-loss recovery or reboot-persistence test.

The new [process-control phase](process-control.md) implements process snapshots,
nonblocking child polling and parent-only cancellation, with a PS viewer and
bounded interactive demo. A53/EL1 and A76/EL2 acceptance passes; Pi acceptance
is pending. General shell background jobs and indefinite services are still work.

## Applications and multitasking

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Scheduling | Cooperative EL1 tasks; preempted EL0, sleep/wake/input/spawn/wait with Pi evidence; new snapshots, nonblocking child poll and parent-only cancellation | Pi process-control/broader lifecycle tests, process trees, shell background jobs, blocking I/O, priorities, fairness and background services |
| Process memory | Private image/stack pages, kernel-only mappings, W^X, bounded dynamic RW/NX pages with quota/rollback/cleanup; Pi page/byte allocator examples pass | Broader Pi lifecycle/concurrent checks, larger VM, shared/file mappings, copy-on-write if needed and broader OOM policy |
| Exceptions | Kernel diagnostics and new contained EL0 faults/timeouts | Rich per-process reporting, debugging, signals or another application fault policy |
| System calls | Checked files/console/time/args/cwd/sleep/mutations/spawn/wait/file streams; new two-stage PIPE_RUN | ABI evolution, general descriptors/IPC, longer pipes, sockets and broader blocking I/O |
| Executables | Static AArch64 ELF subset, 50 sample/test ELFs, shell/file tools/streams/pipes/profiles and new PS; Pi memory/profile/exec/allocator examples pass | Pi PS/broader exec/update validation, stored-shell persistence, larger files, general scripts, job control and broader toolchain tests |
| Concurrency | Single active CPU; syscalls/drivers complete synchronously | Preemptive kernel synchronization, IRQ-safe driver queues, SMP startup/locking and multicore scheduling |

Acceptance should keep checking resource restoration, failure containment and
storage integrity while expanding the scheduler. A larger application runtime
should not assume that arbitrary EL1 driver code is already preemption-safe.

## Persistent files and installation

The writable root remains a volatile RAM filesystem. `/boot` is read-only
FAT32. `/data` is an original bounded snapshot filesystem: new v3 volumes have
sixteen shared file/directory slots and nested files up to 8192 bytes; v2 keeps
sixteen flat files of 8192 bytes and v1 keeps eight flat files of 4096 bytes.
All use two checked commit banks in a separate 1 MiB partition. The
kernel does not expose writable FAT metadata. The snapshot implementation
retains the previous complete bank and refuses further updates after an
ambiguous I/O failure. Physical persistence across an actual power cycle must
still be checked; successful same-session write/read/sync is narrower evidence.

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Writable storage | USB2 writes, bounded flush/write-through policy, snapshot commits/readback, v3 directories and atomic single-file publication | Larger files, richer metadata, capacity growth, general writable filesystem and more block-device drivers |
| Recovery | SHA256/two banks/rollback, native interrupted-I/O tests; new serial export and validated empty-target restore | Pi backup/restore and power-cut tests, richer recovery console, repair policy and data migration |
| Local installation | `appinstall` creates a validated ELF; `appupdate` validates before atomically updating an existing file, with full readback | General OS installer, layout planning, multi-file upgrades, uninstall/version rollback policy and preservation of user data |
| Packages | Executable files can be stored and loaded locally | Package format/manifests, versions, dependencies, integrity/authenticity checks, transactions and repository metadata |
| Downloads | Foreground Wi-Fi/IP/UDP DNS/ICMP foundations | Reliable background networking, TCP, HTTP/TLS, certificate/time policy, signatures and authenticated repositories |

The small executable installer is not a package manager or a network download
command. A package manager depends on storage limits/recovery, application ABI,
metadata and authenticity decisions. Those dependencies should be implemented
and tested before accepting remote packages.

Full-image reflashing replaces `/data`; persistence refers to boots of an
installed image. New dataexport/host conversion and explicit datarestore can
carry same-format files across images. This requires a saved backup and an
empty destination; automatic preservation, merging and migration remain pending.

## Local hardware and UI

The current supported interaction is the header UART. No HDMI framebuffer,
USB keyboard/mouse, GPU, audio or general peripheral driver is claimed complete.
Because the present Pi setup has serial only, new local-device work must keep
UART diagnostics and distinguish emulator checks from physical verification.

| Layer | Next implementation and acceptance |
| --- | --- |
| Display discovery | Establish the Pi 5 firmware/display handoff, pixel format, address translation and valid framebuffer ownership; verify hardware scanout |
| Framebuffer console | Bounded framebuffer writes, clipping/stride checks, font/text rendering, scrolling and serial/display console routing |
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

1. **Current: process control; next: bounded shell jobs.** Snapshot/poll/cancel
   and PS.ELF are implemented and pass local ownership, pointer, terminal and
   lifecycle acceptance. After this foundation, add bounded shell background
   jobs and broader process lifecycles. Preserve resource/terminal ownership;
   general process trees and indefinite services are not implemented yet.
2. Extend storage capacity, the VFS and startup/mount policy; strengthen backup,
   recovery and updates without losing user data. Separate physical reboot,
   power-cut and installed-shell evidence from existing DIRCHK and write tests.
3. Build background network handling and renewal/rekey/reconnect, then sockets,
   TCP and HTTP/TLS. Existing DNS/ICMP tests do not provide Internet downloads.
4. Grow the application runtime, shell scripts and tools, followed by package
   metadata, authenticity/dependencies, installer transactions and rollback.
5. Add display and USB HID incrementally when those devices can be tested;
   keep serial as the diagnostic path. GPU/audio are later capabilities.
6. Add platform shutdown/reboot, watchdog/RTC/thermal management and selected
   peripherals, then SMP, synchronization, hardening and additional board ports.

Open physical checks are evidence gaps, not a requirement to repeat every
accepted test before progressing. The process-control phase has a new kernel
and PS.ELF, plus a scoped boot-file update to preserve the preceding /data volume.

The complete feature/driver status remains in [os-status.md](os-status.md).
Native tests establish bounded parsing/mapping behavior, emulators establish
their modeled boot/execution paths, and Pi captures establish only the hardware
behavior actually exercised. Outstanding drivers are work items, not stubbed
features or completed claims.
