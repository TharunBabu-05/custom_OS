# Remaining operating-system work

The Pi now verifies the kernel boot path, serial console, physical memory and
heap, identity MMU protection, exception return, timer interrupts, cooperative
tasks, owned file handles and task file I/O. USB2 BOT reads/FAT mounting and the
WCE=0 data mount/write/read/sync path have hardware evidence. The latest task
capture is [pi5-file-task-io-verified-2026-11-13.txt](../proofs/pi5-file-task-io-verified-2026-11-13.txt).
Earlier networking captures verify WPA3 association, DHCP, DNS and Internet
ping. Each of these claims has a specific scope; none establishes a complete
desktop operating system.

The new [application phase](user-applications.md) implements a bounded static
ELF loader, private EL0 mappings, checked system calls, an original user runtime,
timer-preempted foreground batches and a small verified executable installer.
It is the foundation for broader applications. The available physical setup
currently has only a serial console. The [Pi EL0 capture](../proofs/pi5-el0-applications-2026-11-13.txt)
now passes execution, protection, preemption and local installation. The next
[user-tools phase](user-tools.md) adds arguments, private cwd, directory queries
and sleeping processes; its [Pi acceptance also passes](../proofs/pi5-user-tools-2026-11-13.txt).
The current [interactive EL0 shell](user-shell.md) adds terminal input and
filesystem builtins. [Physical shell acceptance now passes](../proofs/pi5-user-shell-2026-11-13.txt).
The [file tools](file-tools.md) add binary copy, SHA-256 and sync;
[physical acceptance now passes](../proofs/pi5-file-tools-2026-11-14.txt).
The [current foreground program stage](shell-programs.md) adds bounded child
spawn/wait and shell run/status; the [latest Pi capture](../proofs/pi5-shell-programs-2026-11-14.txt)
passes SPAWNCHK/TTYPAR, normal launches and timeout cleanup. The current
[file-stream stage](stdio.md) adds runio and binary stdin/stdout; IOCHK and file
capture/copy/cleanup now pass in the [Pi capture](../proofs/pi5-stdio-2026-11-14.txt).
The current [two-stage pipeline stage](pipelines.md) adds bounded concurrent
FIFO IPC and named path errors; [physical pipe acceptance now passes](../proofs/pi5-pipelines-2026-11-14.txt).
The current [v2 capacity stage](data-v2.md) adds 16 flat files up to 8192 bytes,
installed-shell support and kernel/user prompt hints; capacity now passes physically.
The first physical v2 test hit a USB transport error. The
[storage reliability stage](usb-storage-reliability.md) fixes a reproduced lifetime
stall-recovery cutoff and adds reusable command/control rings and retained update
diagnostics. The [new capture](../proofs/pi5-usb-v2-capacity-2026-11-15.txt) passes
capacity and 152 stall recoveries with USB online. Installed-shell and actual
power-cycle evidence remain pending. The next [backup/restore stage](data-backup-restore.md)
adds serial export, host conversion and validated empty-target restore; its new
physical acceptance is pending.
The [latest Pi excerpt](../proofs/pi5-data-restore-2026-11-15.txt) now verifies
demo restore, file sizes and text readback. New [file rename/stat](file-rename.md)
adds no-overwrite same-filesystem file moves, a checked EL0 call and metadata
commands without changing v1/v2 disk formats. Physical acceptance is pending.
The [latest capture](../proofs/pi5-file-rename-2026-11-15.txt) now passes MVCHK.
The new [v3 directory phase](persistent-directories.md) adds nested persistent
paths and explicit backup migration.
The [new Pi run](../proofs/pi5-persistent-directories-2026-11-15.txt) now passes
DIRCHK. A real reboot, nested installed ELF execution and serial export still
need their own physical captures.
General process trees/jobs, storage/recovery, packages
and hardware/UI remain outstanding.
The new [atomic update phase](atomic-file-updates.md) adds single-file
publication and validated local executable updates. Multi-file installation,
package authenticity and durable version history remain separate work.
The next [application exec phase](application-exec.md) now adds staged image
replacement, preserved process/file identity and fixed program lookup. It keeps
the existing three-slot, foreground and direct-child limits.
The [new Pi capture](../proofs/pi5-exec-lookup-2026-11-15.txt) now verifies
HELLO lookup and shell exec to ECHO with cleanup. The current
[environment phase](application-environment.md) adds private inherited
variables and configurable PATH/HOME; persistent profiles remain later work.

## Applications and multitasking

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Scheduling | Cooperative EL1 tasks; timer-preempted EL0 batches; sleep/wake, blocked terminal input and direct-child spawn/wait with Pi evidence | Broader physical lifecycle tests, process trees, longer-lived jobs, broader blocking I/O, priorities, fairness/soak tests and background services |
| Process memory | Private bounded user image/stack pages, kernel-only mappings, RX/RO/RW and NX permissions | Dynamic mappings, larger address spaces, memory accounting, copy-on-write if needed and robust OOM policy |
| Exceptions | Kernel diagnostics and new contained EL0 faults/timeouts | Rich per-process reporting, debugging, signals or another application fault policy |
| System calls | Checked files/console/time/args/cwd/sleep/mutations/spawn/wait/file streams; new two-stage PIPE_RUN | ABI evolution, general descriptors/IPC, longer pipes, sockets and broader blocking I/O |
| Executables | Static AArch64 ELF subset, 44 sample/test ELFs, shell/file tools/streams/pipes, Pi MVCHK/DIRCHK/exec lookup, atomic updates and private environments | Pi environment/broader exec/update validation, stored-shell persistence, larger files, persistent profiles, job control and broader toolchain tests |
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

1. Verify v3 directories and nested installed-shell execution on the Pi; separately
   capture a real reboot that runs an installed executable without reflashing.
2. Grow application ABI/process lifecycle and persistent filesystem/recovery
   together, preserving ownership and failure isolation.
3. Add local display and USB HID incrementally when those devices can be tested;
   keep serial as the diagnostic path.
4. Add package metadata, authenticated transport/repositories and transactional
   installation once the underlying runtime, storage and network paths support it.

The complete feature/driver status remains in [os-status.md](os-status.md).
Native tests establish bounded parsing/mapping behavior, emulators establish
their modeled boot/execution paths, and Pi captures establish only the hardware
behavior actually exercised. Outstanding drivers are work items, not stubbed
features or completed claims.
