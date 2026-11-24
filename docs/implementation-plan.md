# OurOS implementation and verification plan

[Core OS milestones](remaining-os-roadmap.md#core-os-milestones) · [Other open work](remaining-os-roadmap.md#other-open-work)

**Current platform milestone on 2026-11-24.** The Pi booted from USB,
mounted extents v5 and passed the binary shell tools. Phase 11 stays
deferred until an HDMI cable is available. The Pi verified the corrected
RTC read, device inventory, Phase 13 resource ownership and Phase 14
RP1 interrupt and DMA observation. The Pi verified the MSI receiver,
the guarded DMA copy and the GIC software edge. The endpoint MSI-X
preflight stopped at bridge validation and reported an unmapped
doorbell. Phase 15 verified GPIO header readback, pin ownership, the
isolated RIO test and input pulls on GPIO 17 and 27. The wired loopback
retry is deferred until the series resistor is available. Phase 16
verified USB device, interface and endpoint inventory on the Pi.
Phase 16 adds live root-port snapshots and a one-key USB boot keyboard
test on a spare root port of the storage controller.
The November 24 Pi capture confirms USB boot alongside a keyboard on the
other RP1 controller and two successful key reads. Phase 17 adds `sdinfo`
and `sdprobe` to inspect the onboard microSD host, card detect and power
signals. The Pi verified GPIO card detection, read the first and last
eight sectors, and restored the SD host after each scan. Phase 18 verified
the RP1 Ethernet PHY responds after a reset pulse. The latest candidate
holds the PHY out of reset for link negotiation and restores it on `ethdown`.
The audit below records the earlier baseline.
[Driver roadmap](platform-driver-plan.md) · [Pi RP1 observation](../proofs/pi5-rp1-observation-2026-11-22.txt)

**Baseline on 2026-11-21.** OurOS is a serial-first AArch64 OS for the tested
Raspberry Pi 5 header UART, with a separate QEMU `virt` target. Its kernel
already runs protected applications, boots into an interactive shell and
persists files on the tested USB drive. The next milestone is to make the
current installation repeatable across updates and cold boots. The first
new implementation after that is memory accounting and pressure handling.

## Commit audit

All 23 reachable commits were reviewed in parent order. Local `main` and
GitHub `main` both pointed to `a2358ac`. The numbers count changed paths in
the OS source directories and in the tests or test scripts, respectively.
They show the scope of each diff. The implementation column includes work
bundled into a commit beyond its subject. Hardware evidence can come from
a later capture of that feature. [Development log](development-log.md) · [Pi proof index](../proofs/README.md)

| Commit | Source / test paths | Implementation delivered | Verification and limits |
| --- | ---: | --- | --- |
| `b6a6fd2` | 43 / 8 | AArch64 entry, Pi 5 and QEMU boot paths, PL011 monitor, timer counter, fatal exception diagnostics and Windows build/image pipeline. | Header UART boot and monitor on Pi. QEMU and image checks cover their corresponding paths. [Initial evidence](pi5-hardware-verification.md) |
| `da66c8d` | 8 / 3 | DTB RAM discovery and reservation checks, 4 KiB physical page bitmap, allocate/free and `meminfo`, `memmap`, `pagetest`. | Pi verified memory reporting and page reuse. [Memory phase](memory.md) |
| `33f33d7` | 7 / 3 | Volatile RAMFS root, directories and file operations, readable memory sizes. | Pi RAMFS evidence. Durable storage came later. [RAMFS phase](ramfs.md) |
| `641ba65` | 19 / 4 | Read-only block and FAT32/VFS layer, QEMU virtio block, Pi SD handoff and initial Wi-Fi diagnostics. | Native/QEMU FAT32 checks. The Pi SD path remains a limited firmware handoff. [Storage phase](storage.md) |
| `6a031b7` | 10 / 3 | Device-tree USB discovery and read-only xHCI register/port probes, with early controller test support. | Pi discovery observed; this phase had no controller-owned disk transfers. [Pi USB discovery](../proofs/pi5-usb-discovery-2026-11-08.txt) |
| `7deb77e` | 13 / 7 | Pi and QEMU xHCI command/event DMA tests and initial SDIO identification and data reads. | Native/QEMU DMA tests and Pi DMA preflight. Full USB transfers and Wi-Fi session support followed. [Pi preflight](../proofs/pi5-cold-wifi-and-dma-preflight-2026-11-09.txt) |
| `67a20a2` | 49 / 22 | USB2 descriptors, BOT/SCSI reads and FAT attach. Wi-Fi chip/core discovery, pinned firmware upload and verification, startup/control, country preparation and passive scan. Added RNG and crypto foundations. | Pi USB FAT/ring-wrap and passive WPA3 beacon scan. [USB capture](../proofs/pi5-usb-fat-and-ring-wrap-2026-11-09.txt) · [Scan capture](../proofs/pi5-wifi-scan-wpa3-hotspot-2026-11-09.txt) |
| `aa21226` | 24 / 17 | WPA3 target checks, auth-mask probes, P-256, SAE H2E and an offline SAE state machine. Added verified ZIP archiving. | Math and capture-based diagnostics. Live WPA3 session support came later. [SAE phase](wifi-sae.md) · [Image archive](image-archive.md) |
| `d695dd2` | 3 / 3 | Firmware event-mask reply-header normalization and regression fixtures. | Pi reproduced the reply shape addressed by the fix. [Reply capture](../proofs/pi5-auth-mask-header-2026-11-09.txt) |
| `0e6b5d6` | 53 / 36 | Live SAE and association work, EAPOL/key setup and port authorization, SDPCM receive handling, IPv4/DHCP/ARP/ICMP foundations. | Pi received and decrypted a DHCP ACK through the verified station path. [DHCP capture](../proofs/pi5-dhcp-ack-port-verified-2026-11-11.txt) |
| `c2585e5` | 15 / 10 | Foreground `wificonnect` session with numeric routed ping and controlled disconnect. Moved the physical-page bitmap into separately reserved RAM. | Pi gateway and Internet ICMP replies. Networking was foreground and session-bound. [Session capture](../proofs/pi5-live-session-internet-ping-2026-11-12.txt) |
| `819b5e4` | 39 / 28 | Kernel heap, W^X identity MMU with null/stack guards, exception return, GICv2, 100 Hz PPI30 timer IRQ and UDP DNS/hostname ping. | Pi protection, heap reclamation, timer and DNS captures. General peripheral dispatch was still open. [MMU capture](../proofs/pi5-mmu-usb-protection-2026-11-12.txt) · [DNS capture](../proofs/pi5-dns-hostname-ping-clean-disconnect-2026-11-12.txt) |
| `24d0975` | 11 / 7 | Cooperative EL1 tasks with guarded stacks, context switching, yield, sleep/wake and exit. | QEMU entry/context tests and later Pi scheduler plus USB run. [Task capture](../proofs/pi5-cooperative-tasks-usb-2026-11-12.txt) |
| `635215a` | 14 / 10 | USB write/flush and dual-bank snapshot v1 in a dedicated 1 MiB partition, eight files of 4096 bytes. | Native/emulator snapshot recovery. Physical write/flush evidence followed. [Persistent storage phase](persistent-storage.md) |
| `2b367f4` | 21 / 10 | Owned file handles with cursors, leases, stale-handle checks and exit cleanup. USB BOT stall recovery and cache-policy handling. | Pi task-owned I/O and physical USB write/read/sync. [Task I/O](../proofs/pi5-file-task-io-verified-2026-11-13.txt) · [USB write capture](../proofs/pi5-usb-data-write-sync-2026-11-13.txt) |
| `a9d6797` | 26 / 7 | Bounded ELF loader, isolated EL0 mappings, timer-preempted users, checked console/time/file calls, validated local ELF installation. | Pi preemption, fault containment and local install. Three user process slots. [EL0 capture](../proofs/pi5-el0-applications-2026-11-13.txt) |
| `f7d12e7` | 17 / 5 | User `ls`, `cat`, `pwd`, `echo`, `sleep`, quoted args, private cwd, metadata calls and scheduler idle/resume. | Pi tools and sleeping-process workflow. [Tools capture](../proofs/pi5-user-tools-2026-11-13.txt) |
| `2b69127` | 22 / 3 | Interactive EL0 shell, binary copy/hash/sync, foreground child spawn/wait, file-stream redirection and process-owned cleanup. | Pi shell, child and stdio workflows. The initial storage version limited output files to 4 KiB. [Shell programs](shell-programs.md) · [Streams capture](../proofs/pi5-stdio-2026-11-14.txt) |
| `62a6176` | 33 / 16 | Two-stage pipes, storage v2/v3, USB lifetime recovery, serial backup/restore, nested dirs, rename/stat, atomic publication and app update, plus `exec` and program lookup. | Pi pipes, v2 capacity, restore, v3 directories and exec lookup. The publication transcript leaves a specific overwrite check open. [Development log](development-log.md) · [Publication capture](../proofs/pi5-file-publication-2026-11-15.txt) |
| `0eda25b` | 10 / 3 | Process environments and configurable PATH/HOME inherited through spawn, pipelines and exec. | Pi variable and exec evidence. [Environment capture](../proofs/pi5-environment-exec-2026-11-15.txt) |
| `9298a99` | 22 / 9 | Shell expansion and saved profiles, private dynamic pages, byte allocator, process snapshots and owned child poll/cancel. | Pi profile reload, allocator tests and process-control demo. [Private pages](user-memory.md) · [Process capture](../proofs/pi5-process-control-2026-11-15.txt) |
| `b82f199` | 13 / 9 | One background job in the shell, snapshot v4 with 16 nodes and 16 KiB files, mount-query syscall 43, `df`, larger ELF installation tests and explicit legacy backup conversion. | Pi base job control, v4 mount and `V4CHK`. QEMU verifies a larger installed ELF after reboot. That Pi retention test remains open. [Shell jobs](shell-jobs.md) · [v4 phase](data-v4.md) |
| `a2358ac` | 11 / 2 | Firmware-selected automatic shell startup, reusable monitor `startup`, plus Windows boot-file preview/apply, SHA-256 readback and rollback. | Pi cold startup to shell and exit. The user's Windows run verified the matching-files path. Scratch tests cover applying and rolling back changes. [Pi startup](../proofs/pi5-startup-v4chk-2026-11-21.txt) · [Updater](boot-updates-tharun_verified.md) |

### Current working tree after `a2358ac`

| Delivered phase | Evidence |
| --- | --- |
| Live snapshot checks and previous-generation export | Pi confirmed both banks valid at generations 11 and 10, with generation 11 matching the mounted data. Native and QEMU tests cover export, invalid banks and fallback. Serial export of the earlier bank on Pi is pending. [Pi capture](../proofs/pi5-datacheck-2026-11-21.txt) · [Recovery guide](data-recovery.md) |
| Open-ended serial shell | Pi answered after an idle interval, returned a prompt after two Ctrl+C presses and exited cleanly. QEMU covers background-job retention and bounded replacement through `exec`. [Pi capture](../proofs/pi5-open-shell-2026-11-21.txt) |
| In-place private heap resizing | Syscall 44 and the allocator can grow or shrink a dedicated run. Native tests cover allocation and publication failure. Both QEMU profiles pass resize and lifecycle tests. Pi `HEAPCHK.ELF resize` passed with child exit 0. [Pi capture](../proofs/pi5-heap-resize-2026-11-21.txt) · [Memory ABI](user-memory.md) |

These three phases had working-tree changes when the audit began. The last
verified boot-update ZIP contained the kernel and matching applications. Its
recorded SHA-256 was
`28c1c6cca4fc8da558d306bba1cadb1408a37299dc272815c56f26c195b24941`.
The Pi captures record behavior and terminal output. The pasted logs carry
no independent hash readback from the USB drive.

The present limits shape the next phases. The scheduler has three EL0 slots
and supports two shell background jobs. Each process has a 16-page private
heap, and v4 storage has 16 shared file/directory slots and a 16 KiB
per-file limit. The Pi has timer IRQs and a foreground Wi-Fi service.
Larger storage, application sockets and local display/input each need
their own implementation.

## Phase order

Complete the acceptance checks for one phase before starting implementation
of the next. A hardware check can stay explicitly pending if the necessary
device is unavailable and the following phase has no dependency on it.

| Phase | Build or verification scope | Acceptance before moving on |
| --- | --- | --- |
| 0. Confirm the working baseline — complete | Record the current source state and update manifest. Install and run `BIGAPP.ELF` on the SanDisk, save the 16 KiB fixture, and export the previous snapshot. | The matching hash and installed ELF passed after a physical power cycle. Windows verified generation 10's archive. [Pi evidence](../proofs/pi5-v4-persistence-2026-11-21.txt) |
| 1. Memory accounting and pressure — complete | `meminfo` and `heapinfo` report allocation pressure. Syscall 45 distinguishes a full private quota, fragmentation and backing-page failures. [Commands, API and tests](memory-pressure.md) | Native failure models and both QEMU profiles passed. The Pi distinguished quota from fragmentation, recovered all private pages and restored page, heap and handle baselines. [Pi evidence](../proofs/pi5-memory-pressure-2026-11-21.txt) |
| 2. Peripheral IRQ dispatch — complete | Bounded SPI registry, deferred callbacks and `irqtest`. The Pi probe uses BCM compare channel 3 and QEMU uses the PL031 alarm. | Native failure models and both QEMU profiles passed. The Pi verified repeat delivery and deferred counts, timer preemption, UART and storage reads. [Pi capture](../proofs/pi5-peripheral-irqs-2026-11-22.txt) · [Implementation and verification](peripheral-irqs.md) |
| 3. Safe reboot — complete | The monitor `reboot` command flushes `/data` and quiesces storage before requesting a watchdog or PSCI reset. | Native refusal tests and both QEMU reboot paths passed. The Pi booted again into the shell, remounted USB storage, and confirmed generation 14 and the file hash. [Pi proof](../proofs/pi5-safe-reboot-2026-11-22.txt) · [Implementation and verification](safe-reboot.md) |
| 4. Process and shell workflows — complete | Two independently owned jobs with PID selection, followed by command lists and bounded text scripts. | Pi passed help, handoff, cancellation, deadlines, parent teardown, quoting and status propagation. [Pi proof](../proofs/pi5-safe-reboot-2026-11-22.txt) · [Implementation and checks](shell-workflows.md) |
| 5. Storage capacity and recovery | The v5 prototype stores 32 nodes and 32 KiB files in a shared extent pool. It accepts explicit v4 backup import. [Format and migration](data-v5.md) | Native and both QEMU profiles passed. The Pi booted v5 and reported the expected limits. Physical migration, retained-hash and interruption checks remain open. [Pi boot capture](../proofs/pi5-data-v5-boot-2026-11-22.txt) |
| 6. Local application lifecycle | Install, list, launch, upgrade, roll back and uninstall named apps on existing data volumes. [Commands and storage](app-lifecycle.md) | Native and QEMU checks passed. The Pi booted the update and verified `applist`. Pi install, rollback and uninstall checks remain open. [Pi capture](../proofs/pi5-app-lifecycle-boot-2026-11-22.txt) |
| 7. Background networking | The worker maintains DHCP leases, supports opt-in reconnect, and accepts a fresh `netup` after verified teardown. Live group key rotation passed native association tests. [Worker and commands](background-networking.md) | The Pi passed worker checks, gateway pings, three renewals, link-loss cleanup, and automatic reconnection to a changed BSSID. Broadcast rebinding and on-air group key rotation remain open. [Pi renewal](../proofs/pi5-dhcp-renewal-2026-11-22.txt) · [Pi reconnect](../proofs/pi5-hotspot-reconnect-2026-11-22.txt) |
| 8. Local UDP sockets | Process-owned IPv4 UDP sockets queue bounded datagrams over background Wi-Fi. [ABI and commands](udp-sockets.md) | Native tests and both QEMU profiles passed ownership, buffer, timeout and cleanup checks. The Pi passed the Windows echo test. Physical timeout and recovery remain open. [Pi and Windows capture](../proofs/pi5-udp-echo-2026-11-22.txt) |
| 9. TCP | Bounded EL0 TCP client connections support reads, writes, retransmission and orderly close over background Wi-Fi. [ABI and test commands](tcp-sockets.md) | Native tests passed loss, reordering, duplicate data and reset. Both QEMU profiles and the physical Pi-to-Windows transfer passed the 16 KiB file echo with a matching SHA-256. [Pi and Windows capture](../proofs/pi5-tcp-echo-2026-11-22.txt) |
| 10. Time, TLS and package retrieval | Operator-set UTC, boot-local SHA-256 pins, pinned live TLS 1.3, and downloads checked against the expected SHA-256 and ELF loader rules. [UTC and trust guide](trust-time.md) · [Live TLS test](tls-live.md) · [Package download test](package-download.md) | The Pi passed live TLS, downloaded a verified ELF from Windows, refused a duplicate download and ran the installed app with exit code 0. [Pi package capture](../proofs/pi5-package-download-2026-11-22.txt) |
| 11. Local display and input, HDMI deferred | The text console can opt in to firmware HDMI scanout and mirror UART output. USB keyboard input can proceed over UART in the platform roadmap. [HDMI test](hdmi-framebuffer.md) | Verify the test pattern and text mirror when the cable is available. |
| 12. Platform expansion | Pi 5 device-tree resource checks and read-only thermal, power status and RTC commands. The RTC accepts the firmware's four-byte result count in the eight-byte buffer. [Driver roadmap](platform-driver-plan.md) | The Pi passed thermal and power status. The corrected RTC seconds advanced from 25 to 33. UTC and data generation stayed unchanged. [Pi readbacks](../proofs/pi5-device-inventory-2026-11-22.txt) |
| 13. Device inventory and ownership | Inspect device-tree resources and track active MMIO, DMA memory and IRQ claims. Register the console, GIC and xHCI and claim the IRQ probe's resources during each run. [Commands](platform-driver-plan.md#phase-13-resource-ownership) | Pi inventory and resource ownership passed. Three IRQ probes returned the same six claims, with data generation 9 and the shell available. [Pi capture](../proofs/pi5-resource-ownership-2026-11-22.txt) |
| 14. RP1 interrupts and DMA | Report RP1 routes, PCIe DMA windows and BCM2712 MSI receiver state. Require owned DMA mappings, test a guarded 64-byte copy, verify GIC edge dispatch and observe the PCIe endpoint MSI-X capability. [Commands](platform-driver-plan.md#phase-14-rp1-endpoint-msi-x-preflight) | The Pi passed DMA copies and GIC software edges. The MSI-X preflight preserved USB storage and resource state. Endpoint readback and delivery remain pending. [Pi DMA and IRQ capture](../proofs/pi5-rp1-dma-edge-2026-11-22.txt) · [Pi MSI-X capture](../proofs/pi5-rp1-msix-2026-11-22.txt) |
| 15. RP1 GPIO and peripheral buses | GPIO readback, boot-time pin claims, the isolated RIO test, input-pull tests and GPIO 17 to 27 loopback with pad and RIO diagnostics. [Commands](platform-driver-plan.md#phase-15-gpio-pad-and-input-diagnostics) | The Pi passed isolated RIO and both `010` pull tests. The wired retry is deferred. [Pi pad capture](../proofs/pi5-rp1-gpio-pads-2026-11-22.txt) |
| 16. RP1 USB device inventory and class binding | Report USB2 configuration, interfaces and endpoints. Observe live root ports and read a USB HID boot keyboard report from a temporary slot alongside storage. [Commands](platform-driver-plan.md#phase-16-usb-boot-keyboard-input) | Three Pi inventories matched across two storage reads. Verify the live port, key report, slot reuse, and intact storage on the Pi. [Pi inventory capture](../proofs/pi5-usb-inventory-2026-11-22.txt) |

### First Pi verification run

Use the existing resize build and USB drive. A workspace folder under
`backups/` can hold the private serial export. At the monitor, run
`datacheck` and capture `dataexport previous` before writing any test files
if an older valid generation is available. Convert a complete capture on
Windows and record the archive generation and SHA-256. [Export commands](data-recovery.md)

From the automatic shell, type `exit` to reach the monitor. Enter the
following commands one at a time. Use a fresh directory if `/data/phase0`
already exists. This check needs three free v4 nodes.

```text
mkdir /data/phase0
appinstall /boot/BIGAPP.ELF /data/phase0/big.elf
appshell
run /data/phase0/big.elf
cp /boot/LARGE.BIN /data/phase0/large.bin
sha256 /boot/LARGE.BIN
sha256 /data/phase0/large.bin
stat /data/phase0/large.bin
exit
sync
datacheck
```

Wait for the installed program's PASS line and `child exit=0`, a file size
of 16,384 bytes, matching hashes and a successful cache flush. The fixture
in the verified PC image has SHA-256
`a8c7ef11b44f99d24ca05a81a5cee1be9ed05f4ea2f12df3b83bc2b90e851aa3`.

Power off and power on with the same USB drive. Capture the fresh boot
banner and run these commands from the new shell.

```text
run /data/phase0/big.elf
stat /data/phase0/large.bin
sha256 /data/phase0/large.bin
exit
datacheck
```

Record the matching hash, file size, program exit code and bank status.
[v4 details](data-v4.md#pi-commands)

### Phase 0 baseline results

- Base commit `a2358ac` and the three working-tree phases listed above.
  The existing update ZIP matched all 55 boot payloads in the verified
  image manifest. The disk image SHA-256 was
  `a97cec888bb2ca5ac2ae0167534a966e53f171edefea05827ca11591b911e19a`.
- `scripts/test-persist-v4.ps1` passed the native snapshot validity,
  previous-generation export and write interruption checks.
- `scripts/test-shell-qemu.ps1 -Data4` passed v4 file and installed ELF
  retention after reopening scratch disks under Cortex-A53 and Cortex-A76.
  `-DataRecovery` passed export, host archive conversion and fallback
  from a corrupted newest bank on both profiles. Logs are under
  `build/handoff-test/`.
- On the Pi before power-off, the installed 10,664-byte ELF passed with
  exit 0. The saved 16,384-byte file matched the fixture hash. The cache
  flush succeeded and mounted generation 14 matched its bank. Windows
  verified the earlier generation 10 archive, and the native restore test
  recovered its payload byte for byte. The archive stays under `backups/`.
  After the power cycle, COM3 was initially silent. The successful
  capture began at the running shell. The installed ELF again exited
  with code 0, the 16 KiB file hash matched and `datacheck` verified the
  same mounted generation 14. Phase 0 passed on the Pi.
  [Pi persistence capture](../proofs/pi5-v4-persistence-2026-11-21.txt)

## Verification for every update

1. **Before editing**, define the feature, the covered board and the expected
   success and failure results. Record the base commit and any working-tree
   changes included in the build.
2. **On the host**, run the focused native tests, including a failure path
   appropriate to the feature. Build the Pi header kernel and its matching
   applications. Check the ELF, image or boot-file manifest as applicable.
3. **In QEMU**, use the Cortex-A53/EL1 and Cortex-A76/EL2 profiles for shared
   kernel behavior. Test the changed path plus boot/shell and relevant
   regressions. Use scratch images for writes and faults. Check resource
   counts and compare disk regions when the phase touches storage.
4. **For delivery**, write down the SHA-256, changed files and rollback path.
   Use the Windows boot updater for compatible kernel/app changes. Use a
   complete image for initial installation or an explicit layout migration,
   with backup and restore steps as part of that phase.
5. **On the Pi**, capture the board/profile, precise commands, success result,
   relevant exit codes and cleanup. Use a physical power cycle when the claim
   is retention across power-off. Use a spare drive for destructive recovery
   tests. Place an excerpt in `proofs/` and record any remaining hardware
   evidence in the phase notes. Native/QEMU and Pi results are recorded at
   their respective scopes.

Each phase record should contain the base commit, included changes, artifact
hash, exact test commands, native/QEMU/Pi results, and the specific remaining
evidence. The [status checklist](os-status.md) and [device roadmap](remaining-os-roadmap.md)
track the subsystem details.
