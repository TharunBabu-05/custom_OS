# Development log

## 2026-11-12 - Cooperative task hardware evidence and persistent USB data

- Saved `proofs/pi5-cooperative-tasks-usb-2026-11-12.txt`: both task runs pass,
  all workers exit after four sleep/wake cycles, private-stack timer IRQs are
  observed and heap/PMM counters restore. Protected memory, RAM files and
  USB/FAT reads pass with the timer running; no unexpected IRQ/controller fault.
- Added bounded block write/flush callbacks and USB2 BOT WRITE10 bulk OUT,
  readback and SYNCHRONIZE CACHE10 with IMMED clear. Failed command/flush
  latches unavailable; CHECK CONDITION captures sense when transport permits.
  USB descriptor/control/read tests still pass. SD and virtio remain read-only.
- Added an original `/data` snapshot filesystem in a separate 1 MiB type-DA
  MBR partition. Boot FAT32 shrinks to 126 MiB but remains read-only; the whole
  image stays 128 MiB. Eight flat 4 KiB files support read/list/replace/touch/rm.
  Two SHA256-protected banks and ordered invalidation/payload/commit flushes
  preserve the previous complete snapshot. No guest formatting/raw writes.
- Native tests cover 136 I/O boundaries in two cache/failure models (272
  cases), bounds, name/size/node limits, generation overflow, corrupt metadata
  and fallback. The C reader mounts the actual PowerShell-generated disk seed.
  A53/EL1 and A76/EL2 QEMU tests persist changes over three boots, independently
  verify hashes/content/generation, preserve all non-data sectors and handle
  read-only media errors while remaining responsive. Task/IRQ/MMU/heap pass.
- Final disk SHA256:
  `a14b05efbf7cec463da8da447de3bd46dc0f52c4735c0b05f7f2a05210cf5b66`.
  Full disk 134217728 bytes, ZIP 816242 bytes, kernel 380588 bytes. Independent
  package/FAT/partition checks and corruption/profile rejection pass. New Pi
  writes/flush durability still require physical tests; see
  `docs/persistent-storage.md`. Earlier full IMG/ZIP backups remain intact.
- Next: owned file handles and task-safe I/O, then loaded applications/EL0 and
  preemption. No physical drive or Git remote was modified by this work.

## 2026-11-12 - Timer IRQ hardware evidence and cooperative kernel tasks

- Retained `proofs/pi5-timer-irq-usb-2026-11-12.txt`: physical Pi GICv2/PPI30,
  synchronous/asynchronous exception return, advancing IRQ counters, timer
  stop, MMU faults, heap/pages, RAMFS and USB/FAT reads all pass. The terminal
  special/spurious acknowledgement in each IRQ drain is expected, with zero
  unexpected sources and no controller fault.
- Added cooperative EL1 task context switches, private 16 KiB stacks with
  separate unmapped 4 KiB guards, a four-slot round-robin queue, explicit
  yield, timed sleep/wakeup, exit/cancel and monitor return. Task switches
  mask IRQs during queue/stack ownership transfer and restore each task's
  own saved mask. The timer ISR continues rearming/counting only.
- Added repeatable `schedtest` and `taskinfo`: three workers preserve live
  stack/heap patterns and callee-saved registers, sleep/wake four times each,
  and terminate through explicit exit or return. IRQ register sentinels also
  run on a private task stack and require a positive delivered count. Cleanup
  wipes contexts/stacks, checks canaries and restores heap/PMM counts.
- Native independent scheduling/deadline model and descriptor oracle cover
  policy limits, wake timing, fairness, overflow/refusal, cancellation/reuse,
  all four guard holes and stack permission/overlap boundaries. QEMU cases
  cover A53/A76 and firmware-style EL1/EL2, repeated task runs, memory tests,
  timer stop/restart and continued fatal behavior of unexpected BRK.
- This is a cooperative kernel foundation, not preemptive scheduling or
  isolated processes. Polling drivers/monitor remain synchronous. New Pi task
  hardware behavior is pending; see `docs/kernel-tasks.md`. Full IMG/ZIP
  preservation remains automatic. No physical device, commit or push.
- Final packaged disk is 134217728 bytes; verified ZIP is 811229 bytes and
  kernel 372396 bytes. SHA256:
  `8a2126d102c517368512ad30afbeb065dcb592c657b0e6859ccdc0a19ff2d0a9`.
  Reserved kernel end is `0x148000`, below DTB `0x200000`. Independent
  extraction, ZIP decompression/hash, 7-Zip integrity, corruption/profile
  rejection and final EL2 USB/FAT/firmware-file tests pass. Task runs before
  and after USB retain memory counts and positive private-stack IRQ counts.

## 2026-11-08 — Phase 1 preparation

- Read the original specification and applied the user's corrected Zero 2 W model.
- Chose shared AArch64 architecture code with separate Pi 5, Zero 2 W and QEMU
  board support. The original ARMv6 compatibility port is superseded.
- Inspected the initially empty workspace (only `.dist/` existed).
- Initialized a local Git repository on `main`; no commit, remote, or publication.
- Found Git, VS Code, MSYS2 host tools and CMake. AArch64 GCC and QEMU were not
  found on PATH. No target compiler success is inferred from host GCC availability.
- WSL status/list failed with "The system cannot find the path specified", both
  inside and outside the sandbox. Distribution availability remains unknown.
- Attempted the normal Ubuntu WSL install command; it failed with the same error.
  Found `WslService` stopped and registered to a missing
  `C:\Program Files\WSL\wslservice.exe`. Documented the official MSI repair path.
- Added architecture/setup documentation, Ubuntu installer, tool inventory check,
  and freestanding C/assembly/link probes with separate CPU flags.
- Current implementation: setup/check scripts and nonbootable probes only.
- Bash syntax validation passed for all three scripts using the existing MSYS2
  Bash. `make help` ran successfully. The compiler-check script exited with the
  expected missing-AArch64-GCC error. Positive compilation is still unverified.
- `make -n check-tools check-toolchain` resolved both recipes. Tool inventory
  correctly returned failure for missing required tools. The Ubuntu installer
  correctly rejected the MSYS2 host before running any package command.
- Windows reports AMD64; the current automation process is not an elevated
  administrator. WSL application repair/feature activation requires an
  administrator installation step and may require a Windows restart.
- Emulation: no guest boot tested. Hardware: no boot or driver tested.
- Physical storage: no formatting, partitioning, or flashing performed.

At the end of that initial pass, WSL repair blocked compilation. The following
entry supersedes that blocker and the earlier unimplemented/test status.

## 2026-11-08 - Native Windows toolchain and first emulated kernel

- User authorized proceeding without WSL. Preserved the optional Ubuntu guide;
  made native PowerShell scripts the default workflow.
- Downloaded portable Arm GNU Toolchain 13.3.Rel1 (GCC 13.3.1) and QEMU Windows
  11.1.0 into `.dist/`. Verified Arm's SHA256 and QEMU distributor's SHA512 before
  extraction. Used official 7-Zip 26.06 to extract, without running installers.
  Download hashes are recorded in `.dist/downloads.json`.
- Both Cortex-A53 and Cortex-A76 freestanding C/assembly/link probes pass.
  ELF inspection confirms ELF64 AArch64, no unresolved symbols or dynamic loader.
- Built a QEMU-only kernel at `0x40200000`. EL1 entry initializes a 64 KiB stack,
  clears BSS and installs fatal vectors. No Pi boot binary is claimed.
- Added a bounded QEMU DTB subset reader, PL011 polling I/O and generic counter
  reads. The kernel monitor supports help/info/cpuinfo/meminfo/uptime/echo/clear,
  halt, and deliberate fault diagnostics. It is privileged, not EL0 user space.
- Fixed a PowerShell case-insensitive ELF-check match that initially mistook
  "no dynamic section" for a dynamic segment. Added the freestanding memset
  implementation required by compiler-generated aggregate initialization.
- QEMU monitor tests pass on Cortex-A53 with 128 MiB and Cortex-A76 with 512 MiB:
  banner, hardware discovery, commands, editing, unknown commands, overlong line
  rejection and subsequent recovery. Tests pace input to avoid UART FIFO overrun;
  unlimited-speed host paste is not guaranteed by the polling driver.
- Deliberate `brk #0x42` passed: vector 4, ESR `0xf2000042`, halt with diagnostics.
- Native GDB test passed: breakpoint at `kernel_main`, 16-byte aligned SP, zero
  BSS console-ready flag after deliberately setting it before startup to verify
  the assembly clear loop. Emulator/debugger processes were stopped after tests.
- Native host DTB tests passed: correct discovery, malformed header/token/string
  rejection, truncation, and 5,000 deterministic mutations with a guard page.
- Hardware verification: none. MMU, allocators, IRQ dispatch, scheduling, user
  space, storage/filesystems, GPIO, USB, network and Pi firmware integration
  remain unimplemented. No physical storage device was modified.

Next: verify Pi 5 firmware handoff and its selected UART route against official
board references, implement that board port, and capture a real UART boot log.

## 2026-11-08 - Fix native launcher parameter conflict

- Renamed the launcher's custom `Debug` switch to `Gdb`: advanced PowerShell
  scripts already receive the common `-Debug` parameter from `CmdletBinding`.
- Updated both documented debugger launch commands.
- Tested the actual `run-qemu.ps1` entry point: normal launch reached the monitor
  and answered `info`; `-Gdb` paused at `_start`, confirmed through native GDB
  at PC `0x40200000`. Earlier integration tests launched QEMU directly and did
  not exercise this wrapper's parameter binding.

## 2026-11-08 - Pi 5 boot candidate and package

- User requested the next milestone and confirmed there is no serial adapter
  available. Physical UART bring-up remains pending; no storage write authorized.
- Verified the dedicated BCM2712 UART10 route against Raspberry Pi's device tree
  and TF-A register map. Added a firmware-UART attach path that preserves clock,
  baud and pinmux state; it does not initialize RP1 or assume QEMU's clock.
- Added `arch/aarch64/boot/firmware.S`: AArch64 Image header, preserved DTB pointer,
  entry-EL reporting, guarded EL1/EL2 entry, EL2-to-EL1 transition, BSS/stack/vector
  initialization. The Pi 5 linker puts entry at `0x80000`, below the explicitly
  reserved DTB window starting at `0x200000`.
- Added `rpi5` and `handoff-test` native build targets. Pi build passes ELF,
  undefined-symbol and Image-header checks. Pi memory discovery is intentionally
  reported unavailable; the narrow QEMU DTB parser is not used on Pi firmware data.
- QEMU raw-Image tests passed for EL1 entry, EL2-to-EL1 entry and fatal exception
  after EL2 entry. The regular A53/A76 monitor suite, GDB startup check and DTB
  host checks also passed after shared interface changes. Logs remain under
  `build/qemu_virt/` and `build/handoff-test/`.
- Pinned official DTB/license assets at firmware revision
  `ab5c7f86283ceb58b7adb269cae1d16092a31bf6`, with SHA256 hashes in
  `boot/rpi5/assets.json`. No vendor firmware executable or Linux kernel bundled.
- `scripts/package-rpi5.ps1` rebuilds and produces a local ZIP, verifies staged
  contents, then verifies the extracted ZIP. A deliberately corrupted config
  copy was rejected. PowerShell syntax and asset JSON validation passed.
- Artifact: `build/rpi5/ouros-rpi5-boot-files.zip`. This is not a disk image.
  Kernel Image SHA256:
  `AAD6BB13C19EDC37EC23797152880E21E8BA91D643A6B7B79F5AAA1C20C38583`.
- Added `docs/rpi5-boot.md` with source references, entry/memory contract, file
  roles, UART wiring, expected logs and remaining hardware assumptions.
- Physical Pi firmware boot and UART attach have NOT been tested. The installed
  EEPROM revision/RAM variant are unknown. No physical storage or EEPROM changed.

Next hardware action: obtain a suitable UART adapter/cable, then identify and
explicitly confirm the test microSD preparation before any physical write.
Independent kernel work can proceed on QEMU while that hardware is unavailable.

## 2026-11-08 - Experimental whole-card image

- User requested a final image and complete OS. Clearly stated that the existing
  kernel is incomplete and hardware-unverified; delivered a testable image of
  its actual implemented functionality, not a claim of full OS completion.
- Added a native PowerShell image builder with a fixed project-local output.
  It rebuilds/verifies the Pi 5 package and creates a 128 MiB regular disk file:
  MBR, 1 MiB-aligned FAT32 partition, mirrored FATs, FSInfo and boot backups.
- Used explicit 8.3 filenames OUROS.IMG and PI5.DTB, with matching CONFIG.TXT.
  Kernel/DTB/license contents are unchanged; in-image README and source notes
  describe the missing features and hardware test requirements.
- Verified partition boundaries, FAT32 cluster count, BPB/backups, mirror equality,
  free-space bookkeeping, independent 7-Zip filesystem decoding/extraction,
  all extracted file hashes, pinned DTB, boot config and AArch64 Image header.
- Adjusted the verifier for 7-Zip's automatic descent from the single MBR
  partition into FAT; the initial verifier incorrectly expected an intermediate
  partition file. The final independent extraction succeeds.
- Negative tests passed: corrupted MBR signature, second FAT and kernel payload
  are rejected. Tests modify a separate regular-file copy and restore it; the
  delivered image is unchanged. No physical devices were accessed.
- Artifact: `build/rpi5/OurOS-Pi5-experimental-sd.img` (134,217,728 bytes).
  SHA256: `d35ba729f41b17279455e1d6f0bf10af1d2ddea355420338820f620abf7f9a76`.
- Whole-card image creation is implemented; physical Pi boot, timer interrupts,
  virtual memory, allocation, scheduling, storage/filesystem drivers, USB,
  networking and user-space applications remain unfinished. No UART adapter is
  available yet at that milestone. This is not a complete OS or a hardware-verified release.

## Pi 5 header-console candidate (2026-11-08)

- User has a CP2102 but its jumper wires do not fit the small UART connector.
  Added `-Console header` to native build/package/image scripts. Default remains
  the debug connector; outputs are separated under `build/rpi5-header/`.
- Header config requests firmware RP1 UART0 initialization and preserves PCIe
  with `enable_rp1_uart=1`, `pciex4_reset=0`. Board selection uses the existing
  polling PL011 attach path at CPU address `0x1f00030000`; board name is
  `rpi5-header`. No general PCIe or pinmux driver was added.
- Official firmware configuration and Raspberry Pi device-tree mappings were
  checked; implementation assumptions and limits are in `rpi5-header-uart.md`.
  Firmware version compatibility and physical UART operation remain UNTESTED.
- Header kernel compiled with warnings as errors; ELF/Image header checks and
  disassembly confirmed its selected address. Package round trip and complete
  disk/FAT/7-Zip extraction verification passed. Corruption rejection passed for
  MBR, FAT mirror, kernel bytes and mismatched console profile. Scripts parsed.
- Rebuilt default disk image retained its previous SHA256 exactly. Rebuilt
  QEMU firmware-handoff tests passed EL1, EL2-to-EL1 and deliberate EL2-entry
  fault handling. These do not emulate RP1 or validate its physical operation.
- Header artifact: `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img`,
  134,217,728 bytes; SHA256
  `20fe58d20ed55acadfada4e9a35d1e9ba95259db1196901bff591c364f667aa1`.
- No physical SD card or EEPROM was written. Pending: captured real-Pi banner,
  working commands, timer advancement and repeatable boot through header pins.

## First Pi 5 header capture and address correction (2026-11-08)

- User supplied two firmware captures from board revision `d04171`.
  Both report `RP1_UART 0000001c00030000`, read CONFIG.TXT (395 bytes),
  PI5.DTB (78,770 bytes) and OUROS.IMG (8,105 bytes), and load the image at
  `0x80000` and the device tree at `0x200000`. No OurOS banner is present.
  This verifies the firmware-to-PC serial transmit path and file reads,
  but does not yet verify kernel execution or PC-to-Pi serial reception.
- Identified an incorrect fixed address in our previous header build:
  `0x1f00030000` was inferred from a Linux DT PCIe window. The firmware
  capture provides direct evidence for `0x1c00030000` on this board/setup.
  Corrected that constant and documented its firmware-specific scope rather
  than attempting potentially faulting MMIO probes of alternate windows.
- Reviewed startup code; it still supports EL1/EL2 with MMU/data cache off,
  while unsupported entry state parks before console initialization. No
  entry-state measurement is available from this capture, so earlier startup
  problems are not excluded by the address correction.
- Missing cmdline is not an OurOS requirement; custom armstub is not included
  because the default is embedded in firmware. The absent D0 overlay is a
  separate incomplete DT packaging issue, documented in the header guide.
  The current polling console does not consume its GPIO/interrupt/DMA data.
- Rebuilt kernel disassembly confirms `0x1c00030000`. Package checks, image
  layout and independent FAT extraction/hash checks passed, as did negative
  MBR/FAT/kernel/profile tests. No shared startup/monitor code was changed.
- Updated whole-card artifact remains at
  `build/rpi5-header/OurOS-Pi5-header-experimental-sd.img` (134,217,728 bytes).
  SHA256: `175ad63ccf32c0d4e4e9be30057f88cdc5044c6d59a20c74cf4c07d04c5ab6d6`.
  Physical kernel boot remains pending a capture from this corrected image.
  No physical card or EEPROM was accessed by the build tools.

## Pi 5 header kernel boot observed (2026-11-08)

- User's subsequent CP2102 capture contains the OurOS banner, UART address
  `0x1c00030000`, `Board=rpi5-header entered EL2 running EL1`, and DTB pointer
  `0x200000`. The corrected header console now executes on the real Pi 5.
- `help`, `info`, empty `echo`, unknown-command handling and `echo pi5-ok`
  work. `uptime_seconds` advanced from 26 to 29. This establishes both serial
  directions, command dispatch and counter advancement, without claiming
  external timer calibration or comprehensive OS validation.
- Recorded verbatim kernel output and scoped hardware evidence in
  `docs/pi5-hardware-verification.md`; updated current documentation. No kernel
  or artifact bytes changed to record this observation, so no rebuild was needed.
- Remaining: `cpuinfo`, repeat power-cycle checks, EEPROM/model/RAM details,
  full D0 DT support and all unfinished OS subsystems. Dedicated UART10 and
  other board/firmware combinations remain untested.

## Hardware proof and GitHub presentation (2026-11-08)

- Expanded user capture confirms `cpuinfo` reports MIDR `0x414fd0b1` and a
  54 MHz architectural counter; `meminfo` reports its expected limitation.
  Preserved the three author-supplied proof screenshots unchanged under `proofs/`.
- Rebuilt the project README around the observed hardware milestone, original
  animated circuit SVGs, a 19-frame terminal GIF, the proof gallery, build
  instructions and an explicit implementation roadmap. The replay is labelled
  as an illustration, with abbreviated output and illustrative timing.
- Added source generators for all original README graphics; generated GIF
  frames are decoded for verification. Core assets are repository-local;
  optional live repository badges use Shields.io. No kernel code changed.
- Checked SVG XML and README local references, inspected the hero in Chrome,
  and used GitHub's Markdown renderer to check HTML sanitization/layout.
- Publication requested to `TharunBabu-05/custom_OS`, with the exact commit
  message `OS working perfectly on raspberry pi-5`. That user-selected message
  does not change the documented limits of this early kernel.

## RAM discovery and physical page allocator (2026-11-08)

- User requested further OS implementation starting at meminfo, alongside an
  explanation of Arm, open hardware/ISA, Linux origins and multi-platform images.
  Added `docs/os-and-cpu-architectures.md` with primary references and a diagram.
- Added a separate bounded v17 memory DTB reader: root reg banks/tuples with
  1/2 address/size cells, overflow checks, normalization, memreserve, fixed
  reserved-memory and paired initrd bounds. Invalid/unsupported maps preserve
  output and leave allocation disabled rather than guessing capacity.
- Examined the pinned Pi DTB: its dynamic CMA request has an alloc-ranges
  window. The initial implementation conservatively excludes the entire window.
  Unbounded dynamic requests, translated/nested reservation layouts and kexec
  usable-memory restrictions fail closed. This can leave small boards with no
  available pages; memmap documents the excluded region.
- Added single-core 4 KiB physical pages with a static 1 MiB bitmap covering
  addresses below 32 GiB. Protects all parsed reservations plus kernel/BSS/stack/
  bitmap, complete DTB and a first-2-MiB boot guard. Rejects reserved, unaligned,
  out-of-range and already free pages. No heap, virtual memory or processes yet.
- Added memmap, pagealloc, pagefree <slot> and pagetest; meminfo now reports
  described RAM and managed/free/allocated pages. Monitor handles prevent
  arbitrary-address frees. pagetest touches only its own four pages and restores
  its counters. Added the freestanding memcpy helper required by struct copies.
- Host tests passed cell widths, multiple/high banks, overlaps, fixed/dynamic
  reservations, initrd, arithmetic/capacity rejection, all truncations, 5000
  guard-page mutations, allocation exhaustion/reuse/double-free and the 32 GiB
  cap. Parsed the QEMU DTB and pinned Pi template (not an actual RAM measurement).
  A synthetic 16 GiB map excludes its low 1 GiB dynamic window and allocates only
  above it, with kernel/DTB reservations also recorded.
- Updated QEMU suites passed A53/128 MiB and A76/512 MiB monitors, balanced
  allocation counters, four-page pattern tests, invalid frees, EL1 and EL2 raw
  Image handoff, and fatal exceptions. Native GDB stack/BSS and the original
  5000-mutation QEMU UART-DTB tests also passed during this change.
- On QEMU A53/128 MiB the final ELF reports 31,976 managed/free pages before
  monitor allocations (130,973,696 bytes); reserved/alignment bytes are excluded.
- Preserved the older hardware-observed whole-card image as
  `build/rpi5-header/OurOS-Pi5-header-boot-verified.img` with its existing hash
  and manifest. New header image: 134,217,728 bytes at the existing experimental
  path; SHA256 `06cf730bab6a4b3c43f712061c43390417ee126ec911442b18745e5d52b3671b`.
  New kernel file is 20,488 bytes; its full BSS/stack footprint still passes
  the linker/Image boundary below the DTB window.
- Package/MBR/FAT32/extraction/hash checks and MBR/FAT/kernel/profile corruption
  tests passed. No physical card was written. This revision's physical Pi memory
  validation is pending; the previous UART/monitor hardware proof remains scoped
  to its older kernel. Changes are local; no new GitHub publication was performed.

## Memory hardware evidence, readable sizes and RAMFS (2026-11-08)

- User supplied a physical Pi screenshot confirming the memory revision: two
  firmware RAM banks totaling 8,585,740,288 bytes, 1,835,008 managed pages
  (7,516,192,768 bytes), six reservations and a passing four-page pattern test.
  Allocations at 0x40000000/0x40001000 reduced free pages by two; invalid repeated
  free was rejected and both releases restored the original counters.
- Added shared console formatting for decimal MB and binary MiB, with three
  truncated fractional digits and overflow-safe 64-bit arithmetic. meminfo shows
  reported/managed/free/allocated/unavailable summaries and retains exact counts.
  For the screenshot, reported RAM is 8585.740 MB (8188.000 MiB) and managed RAM
  is 7516.192 MB (7168.000 MiB).
- Implemented a real, bounded RAM root filesystem with static node metadata and
  physical-page-backed file contents. Initial tree: /etc, /home/user, /tmp,
  /README.txt, /etc/hostname and /etc/board. Its three nonempty files own three
  pages, reflected in meminfo. No heap, general VFS, EL0 shell or SD driver added.
- Added pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir and fsinfo to the kernel
  monitor. Supports absolute/relative paths, dot/dot-dot and directory traversal
  checks. Limits: 64 nodes, 31-byte names, 255-byte paths and 4096-byte files.
  Node/allocator exhaustion and invalid writes preserve existing contents;
  failed initialization releases acquired pages. Files are lost on reset.
- Host tests passed decimal/binary size formatting through UINT64_MAX, initial
  contents, full-page binary writes, overwrite/truncate, path errors and exact
  boundary paths, page ownership, out-of-memory rollback, node exhaustion/reuse
  and 5000 guard-page path mutations. Existing memory tests still pass.
- QEMU A53/A76 and raw Image EL1/EL2 handoff suites passed filesystem command
  sequences, expected failures, cleanup and balanced page/node/file counters.
  Fatal exception checks and native GDB stack/BSS startup check also passed.
- Preserved the hardware-observed memory image as
  `build/rpi5-header/OurOS-Pi5-header-memory-verified.img` (SHA256 06cf730b...).
  New whole-card image at the usual experimental path: 134,217,728 bytes;
  SHA256 `62574b624ebebf2b363aca6be50467694d01364326f3509c3e112f5f0c726ea6`.
  New kernel file: 28,680 bytes; full BSS/stack footprint remains below the DTB.
- Package, FAT/MBR, independent extraction, hashes and corruption/profile
  rejection checks passed. New RAMFS/MB display still needs physical Pi capture.
  No physical card or EEPROM was written; no new GitHub publication performed.

## Read-only storage mount and Wi-Fi groundwork (2026-11-08)

- User supplied the original memory/RAMFS screenshots under proofs/ and a new
  PuTTY capture. The RAMFS/MB display milestone is now physically observed:
  /home/user/notes/hello.txt can be created/listed/read, cwd navigation works,
  and fsinfo/page counters reflect the additional file. Updated scoped evidence.
- Expanded RAM root layout with /boot, /dev, /proc, /sys, /var/log and /mnt.
  Empty namespace directories do not implement device nodes/procfs/sysfs.
  Initial state is 12 directories and three data-page-backed files.
- Added a 512-byte read-only block API and a modern virtio-mmio block driver
  for real QEMU I/O. Split queue, one outstanding request, reserved static DMA
  buffers and bounded waits; no write request path. Failed requests disable
  attachment; buffers are not reused after timeout.
- Added an original read-only FAT32 subset: one MBR FAT32 partition, bounded
  geometry, short names, subdirectories, fragmented chains, FAT mirror checks,
  exact file-chain termination and traversal limits. Two-route VFS maps writable
  RAM root and the protected /boot mount, with correct file-versus-directory
  traversal and read-only mutation rejection. cat streams small FAT files and
  sanitizes non-text bytes; no FAT writes or general mount/fd layer yet.
- Pi SD candidate uses mmc0 at 0x1000fff000, confirmed against the pinned DT's
  register/ranges/aliases. It attempts only firmware-handoff SDHC/SDXC CMD17 PIO
  reads. Preserves bus width, power/voltage, clock and tuning; rejects absent,
  inhibited, unclocked or CQE-enabled state. No full card initialization, card
  capacity/CSD discovery, writes/erase, hotplug, USB storage or NVMe. Physical
  handoff remains untested and can legitimately fail with current firmware.
- Added storageinfo/storageinit (manual, never automatic on boot), and FAT32
  ls/cd/cat via /boot. An attachment failure leaves the kernel monitor available.
- Wi-Fi: actual bounded DT alias/compatible/MAC inventory via wifiinfo, plus an
  opt-in zero-argument CMD5/R4 query on separate SDIO host 0x1001100000. Native
  tests cover the query and unclocked rejection. No power/clock setup, CMD52/53,
  runtime chip ID, firmware loading, association, scan or IP networking is
  implemented; no credentials requested or firmware blobs downloaded. Physical
  wifiprobe is pending. Documented the remaining sequence in docs/wifi.md.
- Host tests passed actual-image FAT reads, nested fragmented files, FAT/chain
  corruption and I/O failures, VFS mount boundaries, read-only writes, exact
  SDHCI register engine/error/timeouts/CQE handling, SDIO CMD5 and actual Pi DT
  Wi-Fi alias lookup with 2000 mutations. Updated RAMFS and prior memory tests
  also passed. Host mutation tests use only an in-memory image copy.
- Real QEMU virtio reads passed A53/128 MiB, A76/512 MiB and raw Image EL1/EL2
  handoff: mounted the FAT image, listed actual files, read CONFIG.TXT/SOURCE.TXT,
  crossed mount boundaries, rejected writes and preserved the input image hash.
  Monitor/page/RAMFS/fatal exception tests and native GDB startup also passed.
- Preserved previous hardware-observed RAMFS image as
  `build/rpi5-header/OurOS-Pi5-header-ramfs-verified.img` with its hash/manifest.
  New header whole-card image: 134,217,728 bytes; SHA256
  `7d65349b28968020d80e07e5b3707c5db1a5d1099342814639ce2dc5a636120c`.
  Raw kernel: 45,376 bytes; full BSS/stack footprint passes the DTB-window bound.
  Package/FAT/MBR/extraction/hash and corruption/profile rejection checks passed.
- Updated docs/storage.md with the physical capture procedure, exact driver
  contract and limitations. No physical card or EEPROM was written, and no
  new commit/push was performed. Pi SD/FAT and SDIO tests remain pending.

## USB boot clarification and USB controller discovery (2026-11-08)

- User confirms every Pi hardware test used a 32 GB USB pendrive, with no
  microSD card. New supplied text capture verifies startup, expanded RAM root
  navigation and firmware Wi-Fi inventory. It shows SD attachment failing with
  control1=0 and no response/interrupt. This is expected without an active SD
  card; it is not a failed kernel boot or evidence of USB disk access. /boot
  remains unmounted and CONFIG.TXT is unavailable. Wi-Fi association is absent.
- Clarified boot/README/storage messages: firmware USB loading and kernel USB
  storage are separate. Pi storageinit still targets microSD; the commands no
  longer promise a mount. Added USB boot/discovery instructions in docs/usb.md
  and updated the current hardware priorities/evidence in related documents.
- Added usbinfo: bounded DT inventory of the two RP1 snps,dwc3 host aliases,
  strict Pi 5/reg/cell checks, DT state and CPU-window candidates derived from
  the already working header UART. It performs no USB MMIO.
- Added opt-in usbprobe: an original callback-based read-only xHCI observer
  for capability, command/status/page-size and root-port snapshots. Bounds ports
  to 32 and forward extended capabilities to 64; rejects malformed offsets,
  protocol ranges, all-ones/error reads and unsupported layouts. No write
  callback, controller/port reset, DMA allocation, pointer following, USB/SCSI
  commands or disk I/O. Disabled DT nodes and unconfirmed UART mappings are
  skipped. QEMU refuses Pi MMIO. Bare-metal bus faults still use fatal vectors.
- Native USB tests pass actual DT aliases/registers, malformed descriptions,
  truncations and 2000 DT mutations; mocked xHCI capabilities/ports/protocols,
  unchanged registers, read errors, bounded malformed chains and 10000 register
  mutations. Command tests verify QEMU refusal, UART mapping guard, DT-only
  inventory and CPU candidates 0x1c00200000 / 0x1c00300000.
- RAMFS host tests, existing real-image FAT/SDHCI/SDIO host tests, QEMU A53/A76
  monitor/fault suites and raw Image EL1/EL2 handoff/fault suites pass. QEMU uses
  virtio for storage; its USB command tests exercise platform refusal, not RP1.
  Both QEMU storage runs preserve the image hash. Package/MBR/FAT/extraction/hash
  and corruption/profile rejection tests pass. USB hardware capture is pending.
- Preserved previous whole image as
  build/rpi5-header/OurOS-Pi5-header-usb-boot-observed.img with manifest/checksum.
  Its hash remains 7d65349b...; this is the local build associated with the
  user's capture, not an independently supplied physical-drive readback hash.
- New header image: build/rpi5-header/OurOS-Pi5-header-experimental-sd.img,
  134,217,728 bytes. SHA256:
  80d84ba2e22f1b38e1d5471ff6dacf4322f6267b5f9f1d0fbb4feeb672b55bbe.
  Kernel file: 53,568 bytes; full BSS/stack footprint still fits before the DTB.
  The retained -sd.img filename also works as a firmware USB boot disk image.
- Next USB work: establish hardware controller state, then ownership, RP1 DMA
  translation and command/event rings, enumeration, mass-storage/SCSI reads,
  and reuse the existing read-only FAT/VFS layer. Persistent writes and Wi-Fi
  connections remain later work. No physical device, EEPROM, or Git remote
  was modified by this phase.

## USB command DMA and Wi-Fi SDIO prerequisites (2026-11-08)

- Recorded the user's physical discovery capture in
  proofs/pi5-usb-discovery-2026-11-08.txt. Both firmware RP1 windows respond with
  xHCI 1.10, 64 slots, 4 interrupters, 3 ports, 64-bit DMA and 64-byte contexts.
  usb0 port 1 is connected/enabled/powered; other ports are disconnected.
  Both controllers were running, and uptime advanced after probing. This proves
  register access, not device identity, USB storage or Wi-Fi association.
- Preserved the associated local discovery build as
  build/rpi5-header/OurOS-Pi5-header-usb-discovery-observed.img, SHA256
  80d84ba2e22f1b38e1d5471ff6dacf4322f6267b5f9f1d0fbb4feeb672b55bbe.
  No physical-drive readback hash was supplied.
- Added manual usbtest: validate the live BCM2712 BAR2/UBUS DMA window and RP1
  DMA description, stop/reset usb0, install aligned reserved DCBAA/scratchpad/
  command/event/ERST buffers, start the controller and submit four No-ops.
  Check each event's exact pointer and completion code, then halt the controller.
  No PCIe reset/window changes, port resets, USB transfers or disk commands.
  BIOS ownership, unsupported maps, MMU/cache-on and unsupported layouts are
  refused. All waits are bounded. Buffers remain pinned even after a timeout;
  once controller changes start, reboot before reuse. usb1 and UART are untouched.
- Added a QEMU-only known-device PCI bootstrap for qemu-xhci, allowing real
  command/event DMA tests instead of just mocks. EL1 and raw Image EL2 cases
  pass four completions, halt, repeat refusal, uptime, seeded RAMFS reads,
  physical-page test and filesystem counters after DMA. This does not emulate
  RP1 or prove the live Pi DMA mapping. Also fixed the observer to allow valid
  extended capabilities in reserved bytes before CAPLENGTH, exposed by QEMU.
- Native USB tests pass the actual ring engine, mapped scratchpad/DCBAA/ERST
  wiring, command-pointer/code errors, timeout/cleanup, BIOS ownership, one-shot
  refusal, live BAR2/remap decoding, endian and mapping-bound rejection, plus
  existing DT/register mutation tests.
- User selected WPA3-Personal for external-network connections. Actual WPA3
  association remains unimplemented. Added wifiinit for guarded SDIO transport:
  require an already powered/clocked 3.3V legacy host, reset the SDIO device,
  select 1-bit host width, negotiate OCR, obtain/select RCA, read CCCR/CIS with
  CMD52, and exercise read-only byte-mode CMD53. No GPIO/regulator/clock-source
  setup, function enablement, backplane chip ID, radio firmware, FullMAC or IP.
  Failure diagnostics record phase and raw host/response/interrupt values.
  The D0 GPIO/pin-description packaging gap still needs resolution before
  adding board power/pin setup. The next physical capture must establish state.
- wifi now aliases wifiinfo; wifiinfo reports DT and cached transport status
  without new MMIO. wifiprobe/wifiinit are manual hardware operations. No
  credentials, RF association, transmit frames or false connect command added.
  Documented SAE offload versus external authentication, PMF, secure entropy,
  firmware capabilities, country configuration and IP/DHCP/DNS dependencies.
- Native Wi-Fi tests pass exact CMD0/5/3/7, CCCR/CIS identity, CMD52 reads,
  1/64/512-byte CMD53 reads, bounds, timeout/CRC/R5 failures and power/clock/CQE
  rejection. These mocks do not demonstrate real radio operation. Existing
  storage/FAT tests and QEMU A53/A76 plus EL1/EL2 monitor/fault/virtio-FAT
  regressions pass. Test disk hashes stay unchanged. Final package/MBR/FAT/
  independent extraction/hash checks pass; corruption/profile tests reject
  bad images. The final rebuild only clarifies packaged README wording.
- New header whole-disk image: 134,217,728 bytes; kernel 61,840 bytes. Full
  BSS/stack footprint passes the DTB-window assertion. Final image SHA256:
  23f8f40caab0fa78bc07e1e5aa5fc8055e21f2f0efc7ebbd86068bc50e1682c9.
  Path: build/rpi5-header/OurOS-Pi5-header-experimental-sd.img (USB boot usable).
  Pi usbtest/wifiinit captures, USB enumeration/storage and WPA3 connectivity
  remain outstanding. No physical drive, EEPROM or Git remote was written by
  the agent. No WPA2 fallback or assumed firmware SAE support was introduced.

## Firmware identity DMA and WLAN cold preparation (2026-11-09)

- Read and preserved the user's new attachment as
  proofs/pi5-cold-wifi-and-dma-preflight-2026-11-09.txt. It confirms the kernel
  still boots from USB, the wireless host is unclocked/unpowered, and the prior
  candidate refuses Wi-Fi before commands. USB is now connected on usb1 port 1.
  usbtest refuses BAR2=0x15 / UBUS_REMAP=1 before controller changes; no live
  Pi command/event DMA or Wi-Fi connection has been demonstrated.
- Fixed the DMA decoder to accept the captured 64 GiB inbound identity profile
  in addition to the prior relocated profile, while retaining bounds, alignment,
  enable and endian checks. RP1's documented direct outbound aperture supports
  the selected address profiles; no PCIe window is programmed. Added a native
  regression using the exact captured values. usbtest selects an enabled host
  with a connected root port, or usb0 if none is connected, and prints the choice.
- Added narrow manual WLAN cold preparation behind wifiinit. Validate the Pi 5
  DT, WLAN GPIO/regulator/host/clock layout, 200 MHz source/capabilities, SDHCI
  version, inactive host, CQE-off and exact BCM2712 stepping before any writes.
  Handle C0/C1 0x27120020/21 and D0 0x27120030 mux/pull tables directly. This
  handles the WLAN subset without assuming the template's C0 description fits
  D0; general D0 overlay packaging remains a separate incomplete feature.
- Reset only Wi-Fi SDHCI, select SD routing and non-removable presence, set
  GPIO28 WL_ON and GPIO30..35 SD2 mux/pulls with masked writes. Hold WLAN low
  10 ms, enable 3.3V host power, raise WLAN and wait 150 ms, then establish a
  400 kHz divided clock. No parent-clock/mailbox programming, RP1/PCIe/UART,
  Bluetooth GPIO29, SD-card host or storage writes. Failures disable the WLAN
  clock, power and enable signal; changed state requires reboot before reuse.
- Corrected SDIO's UHS/1.8V preflight to inspect Host Control 2 in the upper
  half of the aligned 0x3c word rather than low-half Auto CMD Error Status.
- Native Wi-Fi tests pass both stepping layouts, pin/pull masks and unrelated
  bit preservation (including all-input direction registers), startup delays,
  routing/presence, exact 400 kHz setup, reset/clock timeout shutdown, DT/step
  rejection and retry refusal. SDIO selection/CCCR/CIS/CMD52/CMD53 and upper-half
  voltage rejection tests pass. Native USB tests cover the captured identity
  profile, relocated/invalid maps and prior command-ring tests.
- QEMU A53/A76 and raw Image EL1/EL2 monitor, RAMFS/page, fault and virtio-FAT
  regression suites pass, preserving input disk hashes. Real qemu-xhci EL2
  No-op tests pass with a working monitor, RAMFS reads and page test afterward.
  These do not emulate the physical Wi-Fi block or prove Pi DMA operation.
- Preserved the previous local image as
  build/rpi5-header/OurOS-Pi5-header-dma-preflight-observed.img, hash 23f8f40c...
  New header image is 134,217,728 bytes, kernel 65,976 bytes, with full footprint
  still below the reserved DTB window. SHA256:
  245d6176174dddac8c3100122c9f8bba568ce1951640a3b926190c799f8edc95.
  Package/MBR/FAT/independent extraction/hash checks pass. New physical captures
  must verify board setup, actual SDIO identity and No-op completions.
- WPA3-Personal remains the requested target. Firmware, backplane access,
  FullMAC, SAE/PMF, secure credentials and IP networking remain unfinished.
  No credentials requested, firmware blob downloaded, physical device written
  or Git remote modified by the agent.

## Wi-Fi function-1 identity and USB2 descriptor reads (2026-11-09)

- Preserved the new user capture as
  proofs/pi5-wlan-selected-usb-dma-pass-2026-11-09.txt. D0 WLAN preparation now
  succeeds: configured 400 kHz clock, RCA 1, three SDIO functions, CCCR 3,
  SDIO revision 4, vendor 0x02d0/device 0x4345 through CMD52. The later optional
  function-0 CMD53 CIS request fails with R5=0x1100 (OUT_OF_RANGE). The monitor
  remains alive, and usb1 completes all four No-ops with two scratchpads and
  identity DMA at command=0x195000/event=0x196000, followed by a clean halt.
- Removed the optional common-CIS probe. Initialization already parses the CIS
  through CMD52; there is no reason to read past its end or attempt unsupported
  function-0 extended access. Generic common reads now use CMD52 as well.
- Added CMD52 writes and a guarded Broadcom 0x02d0:0x4345 function-1 candidate:
  preserve other IO_ENABLE bits, enable function 1, poll IO_READY, request ALP,
  wait for availability, force ALP with hardware clock requests disabled, wait
  65 us and verify availability again. Set the ChipCommon window to 0x18000000
  and read four bytes through function-1 CMD53 with the backplane access flag.
  Report the raw word, chip ID and revision; refuse unknown devices before
  writes. No firmware upload, core reset, association, network or disk commands.
- Added usbenum as an alternative one-shot session to usbtest. It performs the
  same No-ops, resets one directly attached USB2 port, enables a slot, supplies
  32/64-byte input/device contexts and EP0 ring, issues Address Device and reads
  device/configuration descriptors. Handles default EP0 size and Evaluate
  Context when full-speed packet size changes. Reports real VID/PID/class and
  a matching Bulk-Only Storage interface's endpoints and packet sizes. Does
  not select the configuration, activate bulk endpoints, issue SCSI/block I/O
  or mount the pendrive. Controller halts afterward; memory stays pinned.
- Context/TRB/buffer mappings, commands/events, descriptor sizes/chains and
  interface/endpoint counts are bounded. No ring wrap, hub traversal, USB3/UAS,
  hotplug or general recovery in this phase. usbtest/usbenum share one-shot state;
  reboot before reuse. Enumeration requests are explicitly distinguished from
  the No-op path in diagnostics.
- Native USB tests pass malformed/truncated descriptor chains and 2000
  mutations, 64-byte context layout, slot/address/control TRBs, full-speed
  packet evaluation, pointer/code matching and prior DMA error paths. Real
  qemu-xhci USB2 storage tests from EL1 and EL2 read descriptors and BOT endpoints
  (fixture VID 0x46f4/PID 1, endpoints 0x81/0x02, 512-byte packets), halt cleanly,
  reject reuse, and retain a working UART/RAMFS/page allocator. The backing image
  is read-only and its hash is unchanged. Empty supported-protocol ranges,
  exposed by a USB2-only QEMU host, are now accepted by the observer.
- Native Wi-Fi tests pass the captured function-0 failure regression, function-1
  enable/ALP/window/ID sequence, unknown-device gating and readiness timeouts,
  alongside the existing power/SDIO cases. Native chip IDs are mock fixtures,
  not live chip identification. New Pi enumeration and backplane ID captures
  are pending. WPA3-Personal, firmware/FullMAC, secure credentials and IP remain
  unfinished; no password requested or connection success claimed.
- Preserved associated previous image as
  build/rpi5-header/OurOS-Pi5-header-wlan-dma-observed.img (245d6176...). This
  is a local build associated with the capture, not a physical-drive hash.
  New header whole image: 134,217,728 bytes; raw kernel 74,184 bytes; full
  footprint fits before the DTB. SHA256:
  d873a1df2e7ee68a0b657ea78af19c67c4f99de4fd903af02b4b1ebed62fde8d.
  Package/FAT/MBR/independent extraction/hash checks pass. No physical drive,
  EEPROM or Git remote was modified by the agent.

## USB BOT sector reads and FAT32 mount (2026-11-09)

- Preserved the latest physical capture as
  proofs/pi5-chip-id-and-usb-descriptors-2026-11-09.txt. It confirms function-1
  ALP/ChipCommon identification (word 0x15264345, chip 0x4345 revision 6) and
  USB2 enumeration on usb1 (VID 0781/PID 5567, BOT interface 0, IN 81/OUT 02,
  512-byte packets). Uptime and the page self-test still pass afterward.
- Added configuration selection and xHCI bulk endpoint contexts, independent
  IN/OUT transfer rings and cycling event consumption. Persistent session
  metadata, DMA buffers and scratchpads outlive initialization. One request
  runs at a time; controller waits are bounded. Failed active transports are
  disabled and halt is attempted, with buffers retained even on halt failure.
- Added explicitly read-only BOT/SCSI: INQUIRY, TEST UNIT READY, REQUEST SENSE,
  READ CAPACITY(10), READ(10). Validate CBW/CSW framing, tag, residue, status,
  capacity, actual transfer size and bounds; reject unsupported block sizes
  and capacities. LUN 0, direct USB2 BOT only; no hub/UAS/USB3/hotplug/recovery.
- Pi storageinit retains active SD handoff or uses USB when the SD host is
  absent/unclocked/unpowered. QEMU retains virtio preference, falling back to
  optional qemu-xhci. Both reuse the existing FAT/VFS read-only /boot route.
  A successful USB session keeps the controller running; subsequent storageinit
  reuses it. usbtest/usbenum consume the same one-shot controller state, so run
  storageinit first on a fresh boot. No storage probing is automatic.
- Added storagetest: read 512 sectors, report FNV-1a32, no writes. USB storageinfo
  reports SCSI identity, capacity, sense and bulk/event ring wrap counters.
  Native tests cover SCSI identity/capacity/sense retries, allowlisted opcodes,
  bounds, repeated reads and malformed/short/tag/residue/status failures with
  no retry after a read transport failure. Prior USB/SD/FAT/Wi-Fi tests pass.
- Real qemu-xhci EL1 and EL2 tests mount FAT32, list/read actual files, repeat
  storageinit, match the 512-sector hash to the host, wrap both bulk rings and
  the event ring, then read files and pass the page test again. Input disk
  SHA256 stays unchanged. A53/A76 and raw EL1/EL2 monitor, virtio-FAT and fatal
  exception regressions pass. One earlier regression correctly rejected an
  image rebuilt concurrently by the host; final runs used a fixed copy.
- Updated hardware evidence and the networking sequence: USB firmware-file
  source, core discovery/upload, firmware/FullMAC, WPA3 SAE/PMF, then ARP/IP,
  DHCP/routing, ICMP and DNS. Wi-Fi firmware startup, hotspot association,
  ping, downloads and program installation are not implemented or claimed.
- Preserved the previous associated local image as
  build/rpi5-header/OurOS-Pi5-header-chip-usb-enum-observed.img (d873a1df...).
  New header image: 134,217,728 bytes; raw kernel 83,016 bytes, complete
  footprint below the DTB. Whole-image SHA256:
  5238ff93142e7dfa855421e9965e50ac3c64cecd1ef88a7d70603675e929d16f.
  Package/MBR/FAT/independent extraction/hash checks pass; corruption and
  mismatched-profile tests reject bad copies. Pi USB sector/file reads need
  a new physical capture. No physical drive, EEPROM or Git remote was written.

## Wi-Fi AI core discovery and CR4 RAM query (2026-11-09)

- Preserved the new capture as proofs/pi5-usb-fat-and-ring-wrap-2026-11-09.txt.
  The physical Pi now activates usb1 BOT, measures the SanDisk drive's capacity
  (30,765,219,840 bytes), mounts read-only FAT32 /boot and reads actual files.
  The 512-sector FNV check matches the host at 0x6a004021; bulk IN/OUT and event
  rings wrap 4/2/6 times. CONFIG.TXT reads again afterward, wifiinit identifies
  chip 0x4345 revision 6, and pagetest passes. No full-drive hash was supplied.
- Added aligned function-1 CMD53/PIO word writes, including buffer-write-ready
  status enable/acknowledgment, R5 checking and bounded command/buffer/transfer
  completion waits. Errors disable the attachment. New backplane accessors
  map 32 KiB windows with the 32-bit access flag, require the identified 4345
  rev6 AI chip and reject misaligned/out-of-range addresses before window writes.
- Added wificores after wifiinit. Bounded EROM parser handles component pairs,
  ports, register/wrapper descriptors and wide/explicit-size payloads, with
  1024-word and 16-core limits. Requires ChipCommon, SDIO, D11 and unique ARM CR4
  before any bank writes. Failed maps print raw ROM samples for diagnosis.
- Queries CR4 wrapper/reset/capability, refuses to release reset, saves BANKIDX,
  selects each TCM bank and reads BANKINFO, caps RAM at 4 MiB, then restores and
  verifies the selector. Reports live bank sizes separately from the chip-map
  RAM base 0x198000 and the Pi's main RAM. Production adapter permits writes
  only to the discovered CR4 BANKIDX. No CPU/D11 reset, chip RAM upload, firmware
  startup, RF packets, WPA3 handshake or IP connection in this phase.
- Repeated wificores reports cached results; no repeated device writes. Native
  tests cover framing/PIO/window remaps, R5/CRC/command/buffer/transfer failures,
  core/wrapper parsing, truncation, count bounds, missing/duplicate cores,
  unsupported chip/type, sizing units/overflow, reset refusal, restoration
  failures and 10,000 ROM mutations. Mock sizes/addresses are fixtures, not
  physical measurements. QEMU routes the command to an explicit no-radio stub.
- A53/A76 and raw Image EL1/EL2 monitor, RAMFS/page, virtio-FAT and fatal-fault
  regressions pass; real qemu-xhci EL2 USB/FAT reads and ring wrap pass with
  unchanged backing hashes. Native Wi-Fi tests pass. The new Pi core/RAM path
  needs a fresh serial capture before firmware loading work.
- Preserved the previous associated image as
  build/rpi5-header/OurOS-Pi5-header-usb-fat-observed.img (5238ff93...).
  New header image: 134,217,728 bytes, raw kernel 87,612 bytes; complete
  footprint still precedes the DTB. SHA256:
  97db6c6220891049f01205f5bef3cea7ba7185da68551911269bde9c0cca07a7.
  Package/FAT/MBR/extraction/hash checks pass. No physical drive, EEPROM or
  Git remote was written by the agent.

## Captured EROM raw-size fix and native test assertion reporting (2026-11-09)

- Preserved proofs/pi5-erom-size-field-rejection-2026-11-09.txt. The latest
  physical run still mounts USB/FAT, identifies Wi-Fi chip 0x4345 rev6 and
  passes the sector/ring/page tests. wificores stops at EROM 0x1810a000 after
  six words, last 0x01000000, with cores=0 and SDIO still selected. RAM sizing
  never began. Repeated discovery correctly prints cached state; active USB
  storage refuses usbtest/usbenum takeover while uptime reaches 105 seconds.
- Corrected the EROM parser: an explicit size is a raw payload, not an address
  descriptor with a type tag. Consume it and the optional upper size word, then
  continue past that extra address region. Reference brcmfmac chip.c and BCMA
  scan.c both consume this field without a descriptor-type check. Kept address,
  traversal, core-count and bank-query guards.
- Native regression replays the captured six words with synthetic wrappers
  and later cores (not claimed as physical evidence), then reaches RAM sizing.
  Added wide raw-size and truncated-table cases; mutated both captured-prefix
  and previous fixtures across 10,000 bounded runs. Full Wi-Fi native suite passes.
- User also supplied a Windows test-sdio.exe fatal-exit screenshot. This is
  the native project test, not a Pi exception. Current rebuild passes; earlier
  local assertion failures make a stale abort dialog plausible, but the exact
  failed run cannot be established from that screenshot. Windows labels
  0x40000015 STATUS_FATAL_APP_EXIT. Replaced CRT-aborting assertions only in the
  three Wi-Fi host tests with stderr file/line/expression reporting and exit 1.
  Test checks remain enabled under NDEBUG. A bounded child-process probe confirms
  the intentional failure exits 1 within three seconds, with diagnostic output;
  it is checked separately from tests required to pass. No global Windows error
  reporting settings or Pi assertion behavior were changed.
- Preserved prior associated image as
  build/rpi5-header/OurOS-Pi5-header-core-erom-size-observed.img (97db6c62...).
  Corrected image: 134,217,728 bytes; raw kernel remains 87,612 bytes, with full
  footprint before the DTB. Whole-image SHA256:
  01074e919a4f931251670eedcfa0f65c321be1168e3680abe59705615c3489c1.
  Package/MBR/FAT/extraction/hash validation passes. Full physical core discovery
  and RAM sizing still need a new capture; no firmware startup/WPA3/IP success
  is claimed. No physical drive, EEPROM or Git remote was written.

## Pinned radio assets and halted-CPU firmware upload/readback (2026-11-09)

- Preserved proofs/pi5-wifi-cores-800k-ram-2026-11-09.txt. Physical wificores now
  passes: EROM 0x1810a000, 88 words, five cores (CC51/D11-54/CR4-9/PCIe2-14/
  SDIO21), matching register/wrapper addresses. CR4 IOCTL=0x21, RESET=0,
  capability=0x0b44, eight measured banks totalling 819200 bytes / 800 KiB,
  selector restored. USB/FAT, sector hash, pagetest and uptime still pass.
- Pinned resolved vendor firmware/NVRAM/CLM inputs and full notices from
  RPi-Distro/firmware-nonfree commit c91cd2804cf7463aab913e7247c176049f16bbd6
  in boot/wifi/assets.json. Verified download Git blob IDs and SHA256 values.
  Native network download needed sandbox escalation; only project-local files
  were downloaded. Pi 5 aliases resolve to shared 43455 settings/CLM and a
  standard/minimal firmware alternative; selected the standard binary.
- Complete disk image includes FW43455.BIN (609309 bytes), WIFINV.TXT (2074),
  WIFICLM.BIN (2676) and WIFILIC.TXT (full unchanged upstream copyright, 313331).
  Runtime checks pin all three inputs; image checks pin all four plus revision.
  Source provenance is in SOURCE.TXT. The boot-files-only ZIP excludes the
  radio assets; this distinction is documented. No Linux kernel is linked.
- Added a bounded FAT opened-file API: validate/store up to 4096 clusters once,
  then read binary chunks without repeated full-chain scans. Rejected opens
  invalidate handles. Existing 64 KiB monitor cat limit remains. Native tests
  cover fragmented/boundary/EOF/corrupt-chain/I/O cases and compare every byte
  of the large firmware to the pinned cache. No heap or disk writes.
- Added wififiles for no-radio file/SHA256 verification, also exercised through
  real QEMU USB from EL1 and EL2. Added wifiload after storageinit/wifiinit/
  wificores. Require the exact observed profile, correct hashes and board MAC
  before writes. Verify CR4 already halted; preserve its state and disable only
  D11 through its AI wrapper with bounded waits. No CPU reset or release.
- Added aligned 4..512-byte function-1 PIO RAM reads/writes, split at 32 KiB
  windows and limited to [0x198000,0x260000). Upload code at RAM base with a
  padded final transfer; compare each chunk read back. Rehash source during
  upload. Pack bounded NVRAM with the DT MAC and length/complement token.
  Clear the token first, verify NVRAM payload, then hash the entire persistent
  code in chip RAM before committing/verifying the token. This detects a
  deliberately aliased window even when immediate chunk comparisons pass.
- CR4 remains halted and D11 in reset. CLM is checked on disk for a later
  FullMAC stage, not uploaded now. No reset-vector activation, firmware
  execution, RF scan, WPA3 association or IP success is claimed. A repeated
  load after device writes returns cached status and requires reboot.
- Native tests pass SHA256 vectors/million-a and real pinned assets, NVRAM/MAC/
  token formatting, exact profile, halted CPU, D11 reset, padded code/full RAM
  verification, file/hash/mismatch/timeout/cleanup and repeat refusal. All prior
  Wi-Fi/power/SDIO regressions pass. A53/A76, raw EL1/EL2, virtio-FAT, page/RAMFS
  and fault suites pass. QEMU USB file hashes/ring wraps preserve backing hashes.
  These are not physical radio-upload evidence; a new Pi capture is required.
- Preserved previous associated local image as
  build/rpi5-header/OurOS-Pi5-header-wifi-cores-ram-observed.img (01074e91...).
  New complete header image is 134217728 bytes; raw kernel 118620 bytes, complete
  footprint before the DTB. Whole-image SHA256:
  cd89d288476ab8a8fe05486ec45d3cf4d64ee9b33ea4febc9bf66c9beb8048ca.
  Package/MBR/FAT/mirrors/extraction/file-pin/header checks pass, as do negative
  corruption and console-profile tests. No physical drive, EEPROM or Git remote
  was written by the agent. See docs/wifi-firmware.md for the next Pi procedure.

## First-upload card rejection and conservative F1 transport correction (2026-11-09)

- Preserved proofs/pi5-firmware-first-transfer-failure-2026-11-09.txt. All
  runtime asset pins, core map and 800 KiB RAM query pass. wifiload verifies
  CR4 halt/D11 reset and the initial token clear/readback, then fails at the
  first code transfer, phase 4, address 0x198000, with no code bytes verified.
  NVRAM is packed to 1748 bytes at 0x25f92c. USB/page tests and uptime still pass.
- Requested optional wifiinfo from the same failed prompt. User supplied
  last_irq=0x11 and last_response=0x1100; the relevant lines are preserved in
  proofs/pi5-first-upload-r5-out-of-range-2026-11-09.txt. R5 has OUT_OF_RANGE
  (0x100), without host CRC/timeout IRQ bits. Thus a card rejection is observed;
  an exact physical byte-mode maximum is not established.
- Corrected F1 setup to write/read back 64-byte FBR size before enabling it,
  matching brcmfmac. Gate subsequent register/RAM accesses on that setup.
  Explicitly enable the SDHCI block counter for single-block CMD53 read/write
  PIO, without DMA/multi-block/AutoCMD. RAM calls remain <=512 bytes logically,
  but split into <=64-byte wire requests and at 32 KiB windows. No zero-count
  512-byte CMD53 is emitted by this RAM path.
- Poll error bits during each PIO word and stop on first error. Retain the first
  command/argument/direction/R5/IRQ/present/block-count/command-mode/requested/
  FIFO-byte/stage snapshot, including through cleanup and repeat refusal. Add
  decoded host timeout/CRC and card OUT_OF_RANGE messages plus verified F1 size.
- Native tests pass verified/mismatched FBR setup, exact block-count mode,
  mid-FIFO error stop, retained error stages and the captured R5=0x1100/IRQ=0x11
  on an oversized request before any FIFO writes. A translating RAM model writes
  then reads all 800 KiB across every window with <=64-byte requests, including
  a modeled over-64 rejection. That constraint is a regression fixture, not a
  physical measurement. All existing firmware/SHA/NVRAM/core/power/SDIO tests pass.
- Upload success is still unverified on the Pi. CR4 release/firmware startup,
  FullMAC, WPA3 and IP remain gated on a successful upload/readback capture.
  Reboot/reflash for the next test; do not retry the disabled failed attachment.
- Preserved associated prior image as
  build/rpi5-header/OurOS-Pi5-header-wifi-upload-first-chunk-observed.img
  (cd89d288...). New complete image is 134217728 bytes; raw kernel 118700 bytes,
  full footprint before DTB. Whole-image SHA256:
  46ea45ad2bb1070aa13404943ce2c902e12832745f39dcfe87d50ffe5ceabf77.
  Package/MBR/FAT/extraction/pins/header checks pass. No physical drive, EEPROM
  or Git remote was written. See docs/wifi-firmware.md for the retry procedure.

### 2026-11-09 — Physical upload verified; controlled startup candidate

- Saved proofs/pi5-firmware-upload-verified-2026-11-09.txt. The 118700-byte
  kernel passes file pins, D0/SDIO identification and core/RAM discovery, then
  wifiload verifies all 609309 firmware bytes and 1748 packed NVRAM bytes at
  0x25f92c. CR4 is halted and D11 reset. USB/page tests still pass; uptime 210.
- Preserved the associated 46ea45ad... image and sidecars as
  build/rpi5-header/OurOS-Pi5-header-wifi-upload-verified.img.
- Added explicit wifistart after successful staging: recheck exact profile,
  live halt/reset state, vector and NVRAM token; clear old SDIO interrupt status;
  install/read back one address-zero vector word; reset/release CR4 with bounded
  waits; request HT and validate the firmware-published shared pointer/flags.
  Reject out-of-RAM pointers, unsupported versions, ASSERT/TRAP and FWHALT.
- The normal RAM/register APIs still reject address zero. A separate operation
  permits only the vector word. Startup write callbacks whitelist CR4/D11 wrappers
  and SDIO W1C registers; clock writes permit only the HT request.
- Invalidate cached CPU/D11 passive-state claims before removing CPUHALT.
  Failures after that point report possible execution and require reboot;
  no token cleanup writes or automatic retries can race running firmware.
- Native tests cover individually injected I/O failures and invalid boot
  states, delayed readiness and repeat refusal. SDIO PIO tests cover vector
  remapping/readback mismatch while ordinary APIs retain their bounds.
  Increased the bounded delay spin budget to allow real 5/10 ms startup waits
  on Cortex-A76, using the architectural counter as the time source.
- Reference-sequence review added completion of D11's AI reset while CR4 is
  still halted: release wrapper reset with bounded retries and leave PHY clock
  enabled. Test the distinct failure state with D11 release uncertain and CR4
  still halted. The radio firmware takes ownership only after CR4 release.
- Function 2 remains disabled. Shared-memory readiness is not a FullMAC
  control response or network connection. Physical startup remains UNTESTED;
  FullMAC, country/CLM, WPA3 SAE/PMF and DHCP/IP/ICMP/DNS are subsequent stages.
  See docs/wifi-startup.md for the new hardware sequence.
- Validation: native Wi-Fi suite passes, including 38 individually injected
  startup I/O failures and 22 invalid boot states. QEMU A53/A76 monitor, fault
  and read-only virtio/FAT checks pass; EL2 USB BOT/FAT/file-pin checks exercise
  the startup refusal on a machine with no Pi radio. Native storage checks
  and image corruption/profile rejection checks pass.
- New header-console disk image: 134217728 bytes, raw kernel 122852 bytes,
  in-memory end 0x1d0000 below the firmware DTB at 0x200000. Image SHA256:
  df26b379821a50d3925a7c26152bd783a683f61f41dc631b10233ad7edc47a24.
  Independent FAT extraction, all file pins, MBR/backups/mirrors and Image
  header checks pass. No physical device, EEPROM or remote was written.

### 2026-11-09 — Physical firmware startup; read-only control candidate

- Saved proofs/pi5-firmware-startup-verified-2026-11-09.txt. The Pi boots the
  122852-byte kernel, repeats successful upload/readback, then wifistart reaches
  phase=7 shared_memory=ready: HT CSR 0xd0, shared 0x201cc0, flags 1, console
  0x25debc, trap 0, assert line 0, mailbox 0 before F2. USB/page tests still pass;
  uptime reaches 144 seconds. This is firmware boot evidence, not association.
- Preserved associated df26b379... disk image and sidecars as
  build/rpi5-header/OurOS-Pi5-header-wifi-startup-verified.img.
- Added one-shot wificontrol after successful startup: recheck HT/F1 state,
  force HT, configure/read back F2 block size 512, advertise SDPCM protocol 4,
  enable F2, poll IO_READY and firmware DEVREADY/FWREADY, then honor transmit
  credits. Poll/ack core interrupts and mailbox; no CPU IRQ handler needed.
- The observed SDIO ID is 0x4345. The reference 43455-specific tuning uses
  0xa9bf, so this path uses the reference default watermark 8. No guessed
  43455 watermark/MESBUSY settings, clock save/restore or main PCIe reset.
- Added F2 CMD53 byte-mode PIO with incrementing TX and fixed-address RX via
  the ChipCommon window, aligned calls up to 512 bytes. F1 RAM upload still
  splits at 64 bytes. Preserve existing first-failure/R5/IRQ/FIFO diagnostics.
- Added bounded SDPCM/BCDC framing and two GET-only queries: ioctl ABI and
  firmware version string. Validate frame extent, complements, offsets,
  channels, credit window, command/ID/GET/interface flags, response extent,
  status and version text. Drain bounded event/data frames without delivering
  them. No glom, SET, radio-enable, scan, credentials, association or IP.
- Retain protocol phase, mailbox, counters, request/status and last RX header.
  One-shot cached repeats issue no device I/O; failures require reboot. Old
  CR4 discovery registers are now explicitly labelled as a snapshot.
- Native Wi-Fi suite passes: 37 injected control-operation failures, 28
  negative states, delayed F2/mailbox readiness, multi-part reply reads,
  stale IDs and sequence resynchronization. PIO tests cover F2 direction,
  512-byte encoding, R5 refusal and mid-FIFO CRC stop. Prior upload/startup,
  full 800 KiB F1 RAM, core parser, SHA/NVRAM and power tests still pass.
- New complete header image: 134217728 bytes; raw kernel 133716 bytes;
  in-memory end 0x1d3000 before the DTB at 0x200000. Image SHA256:
  f5ba5932dc934e6229e55b8825ebd6c318cc8d2ec927c74650cc10e0c6ebd11c.
  Package/image headers, MBR/FAT/backups/mirrors, independent extraction and
  all file hashes pass. Physical control replies remain UNTESTED. See
  docs/wifi-control.md. No physical disk, EEPROM or remote was written.
- Final regression checks pass: QEMU A53/A76 monitor and fault handling with
  read-only virtio/FAT; EL2 firmware entry with USB BOT, firmware file pins,
  ring wraps and explicit wificontrol refusal. Both test image hashes remain
  unchanged. Native storage/DT checks and MBR/FAT/kernel corruption/profile
  rejection tests also pass. No physical control-channel result is inferred
  from these host/QEMU checks.

### 2026-11-09 — Physical control replies; capability/regulatory candidate

- Saved proofs/pi5-wifi-control-verified-2026-11-09.txt. The 133716-byte kernel
  passes upload/startup and wificontrol phase 8. Actual replies report ioctl
  ABI 2, firmware 7.45.265 (28bca26 CY), FWID 01-b677b91b. F2 block size 512,
  enable/ready 6, HT 0xd2, mailbox 0x40002, TX 2/RX 4, two drained events and
  zero sequence resync. USB/page tests still pass; uptime 192 seconds.
- Preserved associated f5ba5932... image and sidecars as
  build/rpi5-header/OurOS-Pi5-header-wifi-control-verified.img.
- User confirmed country IN. Added wificaps for a bounded 768-byte cap string,
  actual firmware MAC, PMF and supplicant properties. Match SAE feature tokens
  exactly. A zero GET result is a supported property at current value zero,
  not absence of the feature. Explicit UNSUPPORTED is distinct from other
  firmware rejections, which leave support unconfirmed with the status retained.
- Added wifiprepare <CC>: verify the pinned 2676-byte CLM before device SETs,
  upload exact 1400/1276-byte chunks with version/type/BEGIN/END/length/CRC
  headers, require clmload_status 0 and version text, set country with revision
  unspecified, then require matching abbreviation/ccode and resolved revision.
  Preserve raw country response; no silent fallback or guessed mapping.
- Extended the existing SDPCM session with restricted IOVAR GET/SET operations.
  Only clmload and country may be SET. Preserve IDs, sequences and credits;
  distinguish firmware rejection from transport/protocol failure. A failed
  transport poisons further exchanges. SET acknowledgements may echo input
  length without returning input bytes; validate matching header/status and
  consume no output, following the reference SET completion behavior.
- Larger TX frames use <=512-byte CMD53 requests with incrementing addresses
  continuing across chunks; RX remains fixed at the frame port. No block mode,
  DMA or retry. F1 upload retains its tested <=64-byte wire limit.
- Native suite passes four capability and thirteen regulatory per-operation
  failure injections, hash/file failures, exact CLM bytes/flags, country
  mismatch/fallback, optional-property rejection, larger IOVAR framing,
  bounded output copies, SET reply variants, ID/sequence wrap and transport
  poisoning. PIO tests stop before a rejected second TX fragment. Previous
  upload/startup/control/power/parser/SHA tests still pass.
- Complete image: 134217728 bytes; raw kernel 148412 bytes; in-memory end
  0x1d7000 before DTB 0x200000. SHA256:
  bc3f693557eeb3b6e4a302fddd5845942b1da40d33c80b8b315f08f341c42eab.
  Package, MBR/FAT/backups/mirrors, independent extraction, hashes and Image
  header validation pass. QEMU A53/A76 monitor/fault/virtio-FAT regressions,
  EL2 USB file-pin/ring-wrap checks and native storage/corruption checks pass.
  Physical capability/regulatory operations remain UNTESTED. No interface-up,
  scan, association, credentials or IP commands were added. No physical drive,
  EEPROM or remote was written. Procedure: docs/wifi-prepare.md.

### 2026-11-09 — Physical CLM/country verified; passive scan candidate

- Saved proofs/pi5-wifi-prepare-verified-2026-11-09.txt. The 148412-byte kernel
  passes capability discovery: actual MAC, sae_ext, PMF GET value 1 and a
  valid sup_wpa UNSUPPORTED response. Firmware SAE offload is not advertised;
  host authentication/key handling remains future work, not a transport fix.
- CLM upload passes all 2676 bytes/two chunks with status 0. CLM version is
  API 12.2 / Data RPi.4; country and ccode IN revision 0 read back correctly.
  USB/page tests still pass; uptime 150 seconds. Preserved bc3f6935... image
  and sidecars as build/rpi5-header/OurOS-Pi5-header-wifi-prepare-verified.img.
- Added one-shot wifiscan after verified preparation/ioctl ABI 2. Recheck live
  country, select escan v1/v2 using scan_ver availability, preserve/extend
  event mask for result 69 and verify it. Request UP, a passive wildcard scan
  using the firmware's legal default channel list, then bounded event polling.
  No association, credentials, key or IP requests.
- Add event callbacks to the existing SDPCM session, including events arriving
  during a command acknowledgement. Check BCDC/Ethernet/Broadcom event framing,
  interface, event version/type, length and sync ID. Distinguish command ACK
  from terminal success/abort. Raise RX frame bound to 4096 bytes; scan session
  has an explicit 1024-frame budget and 3000-poll/30-second wait bound plus I/O.
- Parse only BSS109, including the 126-byte wire prefix and bounded IE offsets.
  Validate IE/RSN counts/extents; decode primary channel/RSSI, advertised AKMs
  and PMF. Retain 32 BSSID/band records, merge duplicates and count overflow.
  Escape arbitrary SSID bytes for the terminal. Malformed results are counted,
  not treated as a successful connection or evidence of an empty radio area.
- On successful terminal completion, require DOWN acknowledgement and original
  event-mask restoration/readback before reporting overall success. Preserve
  uncertainty after a failed UP/scan/cleanup; no speculative retry/reset.
- Native suite passes 13 scan operation-failure injections, v1/v2 requests,
  synchronous and delayed events, timeout/abort/cleanup failures, country/mask
  mismatches, BSS/RSN/channel/RSSI/duplicate/overflow cases and 5000 packet
  mutations. Integrated SDPCM/CDC + scan test handles an event larger than
  2 KiB before the SET ACK, then completion afterward. Existing Wi-Fi tests pass.
- Complete header image: 134217728 bytes, raw kernel 160372 bytes, in-memory
  end 0x1da000 below DTB 0x200000. SHA256:
  99a7a861b6958ee8befa2a392f3bce6fd4fc33912b6b6ff57d4c0aed9cd3ef41.
  Package/image header, MBR/FAT/backups/mirrors, independent extraction and
  all file hashes pass. Native storage and image-corruption/profile rejection
  checks pass. Physical scan remains UNTESTED. See docs/wifi-scan.md.
  No physical disk, EEPROM or remote was written.
- Final QEMU A53/A76 monitor/fault/virtio-FAT tests and EL2 USB file-pin,
  ring-wrap and explicit wifiscan refusal checks pass. Both test-image hashes
  remain unchanged. These emulated checks do not establish physical RF success.

### 2026-11-09 — WPA3 hotspot discovery; authentication foundations

- Saved proofs/pi5-wifi-scan-wpa3-hotspot-2026-11-09.txt. The 160372-byte
  kernel finds five networks with passive escan v1, malformed=0, overflow=0,
  and successful DOWN/mask restoration. Jack's: channel 6, 2.4 GHz, -28 dBm,
  RSN+SAE and PMF required. USB/page tests still pass; uptime 303 seconds.
- Preserved the associated 99a7a861... disk image and sidecars as
  build/rpi5-header/OurOS-Pi5-header-wifi-scan-verified.img. The supplied
  credential is not consumed, saved in source/docs/proofs or embedded in images.
- Implemented original HMAC-SHA256 vector input/long-key handling and HKDF
  extract/expand with RFC4231/RFC5869 known-answer tests. Add cryptotest,
  bounded APIs, bytewise comparison, explicit volatile erasure of temporaries
  and SHA message schedule. No claim of a completed SAE or side-channel audit.
- Added a narrow Pi DT description gate for RNG200 at translated physical
  0x107d208000: BCM2712, root/SoC cell counts, exact ranges, compatible,
  status and 0x28-byte reg span. Manual rngtest only; no automatic boot access.
- Follow the DT's BCM2711-compatible mode: preserve enabled configuration or
  configure warm-up/FIFO thresholds and sample divider with control readback.
  No generator resets, error-status clearing or interrupt handlers. Require
  warm-up progress; discard sixteen words; stop on health/lockout errors,
  FIFO timeout or a consecutive repeated word. Failure latches unavailable.
- Condition each 64 fresh hardware bytes into 32 output bytes with SHA256;
  cap requests at 256 bytes. Erase all raw/temp state and all caller output
  on partial failure. No clock/MAC/PRNG fallback or claimed entropy count.
  rngtest compares two 64-byte outputs, erases them and prints status/counters
  only. Engineering checks are not entropy certification.
- Native suite passes RFC vectors, key/length/output/erasure boundaries,
  actual DT translation/mismatch tests, inherited/cold RNG setup, repeated
  values/errors/timeouts and 584 individual register-I/O failure injections.
  Existing firmware/scan/SDIO/PIO/crypto integrity tests still pass.
- New complete image: 134217728 bytes, raw kernel 168564 bytes, in-memory end
  0x1dc000 before firmware DTB 0x200000. SHA256:
  d2da3d384c7eb2ff4f53cb0c1106865f891ceaf0b25a08a44b457823606004c0.
  Package, Image header, MBR/FAT/backups/mirrors, independent extraction and
  file hashes pass. Native storage and image corruption/profile checks pass.
  Physical RNG remains UNTESTED. Host SAE/group math, external-auth frame
  exchange, association, EAPOL/key/PMF handling and IP remain unfinished.
  No physical drive, EEPROM or remote was written. See docs/wifi-auth-foundation.md.
- Final QEMU checks pass: A53/A76 monitor and known-answer crypto tests,
  explicit RNG200 refusal without the Pi source, fault handling and read-only
  virtio/FAT. EL2 USB firmware-file pins and ring-wrap checks also pass; both
  test-image hashes remain unchanged. Emulation does not verify physical entropy.
