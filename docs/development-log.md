# Development log

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
