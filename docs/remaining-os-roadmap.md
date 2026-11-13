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
and sleeping processes; its physical acceptance remains pending.

## Applications and multitasking

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Scheduling | Cooperative EL1 tasks; timer-preempted EL0 batches of up to three processes; new sleep/wake and all-asleep idle/resume | General creation/wait, longer-lived jobs, blocking I/O, priorities, fairness/soak tests and background services |
| Process memory | Private bounded user image/stack pages, kernel-only mappings, RX/RO/RW and NX permissions | Dynamic mappings, larger address spaces, memory accounting, copy-on-write if needed and robust OOM policy |
| Exceptions | Kernel diagnostics and new contained EL0 faults/timeouts | Rich per-process reporting, debugging, signals or another application fault policy |
| System calls | Checked exit/console/yield/time/file calls plus arguments, PID, cwd, stat/readdir and sleep | ABI evolution, wait/spawn, terminal input, IPC, pipes, sockets and blocking I/O |
| Executables | Strict static AArch64 ELF subset, 19 programs including user LS/CAT/PWD/ECHO/SLEEP and quoted launch arguments | User C library/runtime growth, environment, interactive shell, executable metadata and broader toolchain tests |
| Concurrency | Single active CPU; syscalls/drivers complete synchronously | Preemptive kernel synchronization, IRQ-safe driver queues, SMP startup/locking and multicore scheduling |

Acceptance should keep checking resource restoration, failure containment and
storage integrity while expanding the scheduler. A larger application runtime
should not assume that arbitrary EL1 driver code is already preemption-safe.

## Persistent files and installation

The writable root remains a volatile RAM filesystem. `/boot` is read-only
FAT32. `/data` is an original bounded snapshot filesystem: eight flat files up
to 4096 bytes, two checked commit banks in a separate 1 MiB partition. The
kernel does not expose writable FAT metadata. The snapshot implementation
retains the previous complete bank and refuses further updates after an
ambiguous I/O failure. Physical persistence across an actual power cycle must
still be checked; successful same-session write/read/sync is narrower evidence.

| Area | Implemented foundation | Work remaining |
| --- | --- | --- |
| Writable storage | USB2 single-sector writes, bounded flush/write-through policy, synchronous snapshot commits and readback | Larger/nested files, metadata, capacity growth, general writable filesystem and more block-device drivers |
| Recovery | SHA256 corruption detection, two commit banks, rollback selection and native interrupted-I/O tests | Physical power-cut tests, export/backup tools, recovery console, diagnostics/repair policy and data migration |
| Local installation | `appinstall` validates a small static ELF, creates a new `/data` file and verifies full readback | General OS installer, layout planning, upgrades, uninstall/rollback policy and preservation of user data |
| Packages | Executable files can be stored and loaded locally | Package format/manifests, versions, dependencies, integrity/authenticity checks, transactions and repository metadata |
| Downloads | Foreground Wi-Fi/IP/UDP DNS/ICMP foundations | Reliable background networking, TCP, HTTP/TLS, certificate/time policy, signatures and authenticated repositories |

The small executable installer is not a package manager or a network download
command. A package manager depends on storage limits/recovery, application ABI,
metadata and authenticity decisions. Those dependencies should be implemented
and tested before accepting remote packages.

Full-image reflashing currently replaces `/data`; persistence refers to boots
of an installed image. Data export/migration must precede a future installer
that promises to preserve files across full upgrades.

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

1. Verify new user tools/arguments/cwd and sleep/wake on the Pi; separately
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
