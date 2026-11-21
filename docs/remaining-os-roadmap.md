# Remaining operating-system work

Current state: **an early single-core OS with protected EL0 applications, a
serial shell and bounded persistent storage**. The MMU, exceptions, GIC/timer
IRQs, cooperative kernel tasks, preempted user programs, USB writes, ELF loading
and checked system calls are implemented and have Pi evidence.

The [Pi allocator capture](../proofs/pi5-user-allocator-2026-11-15.txt) passes
HEAPCHK and HEAPDEMO with exit 0, no remaining private pages and restored
resources. This accepts the default byte-allocation checks on real hardware.
The [status checklist](os-status.md) links the earlier protection, scheduling,
storage, shell and networking evidence.

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
| Scheduling | Cooperative EL1 tasks; preempted EL0, sleep/wake/input/spawn/wait/process control and base shell bg/jobs/stop with Pi evidence | Pi fg/bgio/broader lifecycle tests, multiple jobs/process trees, blocking I/O, priorities, fairness and indefinite services |
| Process memory | Private image/stack pages, kernel-only mappings, W^X, bounded dynamic RW/NX pages with quota/rollback/cleanup; Pi page/byte allocator examples pass | Broader Pi lifecycle/concurrent checks, larger VM, shared/file mappings, copy-on-write if needed and broader OOM policy |
| Exceptions | Kernel diagnostics and new contained EL0 faults/timeouts | Rich per-process reporting, debugging, signals or another application fault policy |
| System calls | Checked files/console/time/args/cwd/sleep/mutations/spawn/wait/file streams; new two-stage PIPE_RUN | ABI evolution, general descriptors/IPC, longer pipes, sockets and broader blocking I/O |
| Executables | Static AArch64 ELF subset, 53 sample/test ELFs and compact runtime; 8162-byte shell; 10664-byte installed program cold-boots in QEMU; Pi PS/memory/profile/exec examples pass | Pi v4/broader exec/update validation, stored-shell persistence, larger VM, scripts, multiple jobs and broader toolchain tests |
| Concurrency | Single active CPU; syscalls/drivers complete synchronously | Preemptive kernel synchronization, IRQ-safe driver queues, SMP startup/locking and multicore scheduling |

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
| Writable storage | USB2 writes, bounded flush/write-through policy, snapshot commits/readback, nested directories and atomic publication; new v4 16 KiB files pass native/QEMU | Pi v4 acceptance, larger volumes/files beyond fixed snapshots, richer metadata, general writable filesystem and more block-device drivers |
| Recovery | SHA256/two banks/rollback, native interrupted-I/O tests, serial export/empty-target restore and explicit v1-v3 to v4 backup conversion | Pi backup/export/power-cut tests, richer recovery console, repair and automatic preservation policy |
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
empty destination; automatic preservation, merging and in-place migration remain pending.

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

1. **Current phase** delivers verified boot-file updates and rollback for an
   existing header-console drive. Automatic startup, shell exit and `V4CHK`
   pass on Pi. [Boot updates](boot-updates.md) · [Pi capture](../proofs/pi5-startup-v4chk-2026-11-21.txt)
2. Extend storage beyond fixed snapshots and strengthen backup and recovery.
   Exercise a larger installed ELF and a retained 16 KiB file across power-off
   before testing interruption during a physical commit.
3. Build background network handling and renewal/rekey/reconnect, then sockets,
   TCP and HTTP/TLS. Existing DNS/ICMP tests do not provide Internet downloads.
4. Grow the application runtime, shell scripts and tools, followed by package
   metadata, authenticity/dependencies, installer transactions and rollback.
5. Add display and USB HID incrementally when those devices can be tested;
   keep serial as the diagnostic path. GPU/audio are later capabilities.
6. Add platform shutdown/reboot, watchdog/RTC/thermal management and selected
   peripherals, then SMP, synchronization, hardening and additional board ports.

Open physical checks are evidence gaps, not a requirement to repeat every
accepted test before progressing. The shell-job phase is an application-only
update of SHELL.ELF on the process-control kernel; existing /data can be retained.

The complete feature/driver status remains in [os-status.md](os-status.md).
Native tests establish bounded parsing/mapping behavior, emulators establish
their modeled boot/execution paths, and Pi captures establish only the hardware
behavior actually exercised. Outstanding drivers are work items, not stubbed
features or completed claims.
