# EL1 identity mappings and memory protection

**Pi MMU milestone verified:** the [latest capture](../proofs/pi5-mmu-usb-protection-2026-11-12.txt)
passes repeated MMU protection tests, heap/pages, RAMFS edits and USB/FAT
sector/file reads with MMU on. The new image and next test are
[exceptions, GICv2 and timer IRQs](interrupts.md). Measurements below identify
the previous MMU image; MMU-on Wi-Fi regression is not shown in this capture.

The [new Pi log](../proofs/pi5-heap-page-reclamation-2026-11-12.txt) verifies the
heap self-test repeatedly, 17/33-byte allocations rounding to 32/64 bytes,
double-free rejection and restoration to 2 PMM pages / 3 RAMFS buffers /
544 bytes capacity. It also verifies the packed seed files and `pagetest`.
It does not include this revision's MMU tests or new file-edit/storage/radio
regressions. The full [OS feature/driver checklist](os-status.md) distinguishes
what exists from what remains.

This phase implements the first **MMU protection profile**, enabled explicitly
with `mmuinit`. Addresses stay identical to physical addresses so existing
drivers retain their pointer/DMA model. The kernel does not automatically
enable it during this first hardware-validation phase. No process address
spaces or user-mode applications are added yet.

## Run on Pi 5

Extract the full [disk ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip)
and flash its `.img` to the USB pendrive, or flash the local full
[IMG](../image_files/OurOS-Pi5-header-experimental-sd.img). Use the same header
CP2102 UART at 115200 8N1, no flow control and PuTTY local echo/editing off.

Run separately at the normal `>` prompt, waiting for each result:

```text
mmuinfo
mmuinit
mmuinfo
mmutest
heaptest
pagetest
fsinfo
meminfo
write /tmp/mmu.txt memory-protection-ok
cat /tmp/mmu.txt
rm /tmp/mmu.txt
heapinfo
storageinit
cat /boot/CONFIG.TXT
storagetest
pagetest
mmutest
uptime
```

Expected activation marker: `MMU enabled: kernel code RO executable; ...`.
`mmuinfo` should say `on; verified identity profile`. `mmutest` intentionally
triggers **five caught faults** and prints PASS: null read, stack-guard read,
code write, constants write and data execution. `last_pass_mask` should be
`0x1f`. Those `probe=... caught` lines are expected tests, not a kernel crash.
Heap/PMM counts should be restored afterward.

Reissuing `mmuinit` reports that activation was already attempted; it does not
rewrite live tables. There is no `mmuoff` command. If preparation is refused,
the CPU registers are unchanged and the monitor remains usable with MMU off.
If the board stops after `Enabling EL1 MMU`, preserve that UART output and
power-cycle; this image starts with MMU off again.

After successful storage mount, optionally regress Wi-Fi using the existing
[sequence](wifi-dns.md#image-and-fresh-boot), starting at `wififiles`:
firmware/start/control/capabilities, country IN, scan/target, `wificonnect Jack's`.
At `net>` run DNS and hostname/numeric pings, then `disconnect`. Memory/file
commands run at the normal `>` prompt. No password is stored or embedded.
Do not run `usbtest`/`usbenum` before `storageinit` on this boot.

Share the output from `mmuinfo` through both `mmutest` runs and storage checks,
plus any network failure if you test Wi-Fi. Do not run `fault` as part of the
normal Pi sequence: it deliberately halts the kernel and is still fatal.

## Mappings and implementation

The profile uses 4 KiB granules, a 39-bit lower VA range and TTBR0 starting at
level 1. TTBR1 walks are disabled. RAM from the validated DT memory map is
mapped Normal, inner/outer non-cacheable. The mappings use 1 GiB / 2 MiB blocks
where a region permits it, and 4 KiB pages at permission boundaries.

| Region | EL1 access | Execution |
| --- | --- | --- |
| Kernel text (including vectors) | Read-only | Executable |
| Constants/rodata | Read-only | XN |
| DTB and table-page pool | Read-only | XN |
| Other described RAM, heap and stack | Read/write | XN |
| Board's known device windows | Device-nGnRnE, read/write | XN |
| Address-zero page, stack-guard page, other holes | Unmapped | Unmapped |

Every mapped leaf denies EL0 data access and user execution. Writable-execute
protection is also enabled in SCTLR.WXN. This is kernel protection, not user
isolation: the rest of the kernel and drivers still share one address space.
Firmware-reserved RAM remains excluded from PMM allocation; mapping it does
not make it available for allocation or promise permission-level isolation
from trusted kernel code.

Pi device windows are the existing SoC window `0x1000000000..0x1080000000`
and captured RP1 firmware aperture `0x1c00000000..0x1c00400000`, covering our
UART, SDIO/SDHCI, power/pins, RNG, bridge and USB registers. QEMU maps its
GIC/UART/virtio window, the selected xHCI BAR and PCIe ECAM. These are explicit
board profiles, not a general device-tree bus translator. They do not
reconfigure PCIe or map the later Linux RP1 window speculatively.

The table pool is 32 pages (128 KiB) in aligned, kernel-reserved BSS. Pool space
is bounded, and no additional PMM pages are consumed by activation. Layout,
overlap, CPU granule/PA width and capacity checks run before register changes.
The prepared pool becomes read-only and is never rebuilt while active.
The linker page-aligns text/rodata boundaries and adds a reserved, unmapped
4 KiB page immediately below the existing 64 KiB boot stack.

**I/D caches stay off.** Keeping RAM and page-table walks non-cacheable avoids
introducing DMA cache-maintenance requirements into the existing driver path
in the same phase. USB still validates the firmware DMA window, additionally
accepts only MMU-off or our verified identity profile, and checks that DMA
buffers span writable Normal-NC mappings. Existing barriers remain in place.
This does not implement a cache-enabled DMA API or an IOMMU.

The synchronous EL1/SPx vector now saves/restores all 31 general registers for
the deliberately armed self-test path. Recovery requires the exact expected
PC, fault address, exception class and fault category/write direction. Only
that test's whitelisted resume label is used. Unexpected faults/BRK still
print fatal diagnostics and halt; there is no general signal, page-fault
repair or recovery from a real stack overflow. IRQs remain masked.

Architectural references: [Arm's memory-management examples](https://documentation-service.arm.com/static/64d5f38f4a92140bb31d7f03),
[Arm Trusted Firmware control-register definitions](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/arch/aarch64/arch.h)
and [EL1 activation ordering](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/xlat_tables_v2/aarch64/enable_mmu.S).
Our tables, policy, activation/probes and handlers are implemented in-tree;
no TF-A translation library or external kernel is linked.

## Verification

- Native policy tests cover Pi 8 GiB/QEMU maps, independent raw descriptor
  decoding and exact attributes, all three mapping levels, RO/RW/XN boundaries,
  device/DTB/table protection, guard holes and 40,000 sampled translations.
- Invalid/overlapping/out-of-RAM/overflow layouts preserve plan output. Table
  exhaustion does not publish a partial configuration. Bad descriptors/cycles,
  unsupported CPU controls and unrelated/unarmed exception recovery are rejected.
- Focused QEMU tests activate the MMU and trigger all five real faults twice,
  then test heap/pages/RAMFS and verify BRK remains fatal. Cortex-A53/128 MiB
  and Cortex-A76/4 GiB cases pass. Firmware-style EL1/EL2 entry is checked too.
- The comprehensive monitor/storage and USB2 BOT/FAT/firmware-file suites run
  with MMU enabled. They retain image-hash and ring-wrap checks.

The new profile needs physical Pi verification, including USB and optional
Wi-Fi regression. Native/QEMU tests cannot establish those hardware claims.
## Image and completed checks

The full disk is **134217728 bytes** (128 MiB), ZIP **801640 bytes**, and
kernel `OUROS.IMG` **351676 bytes**. Full-disk SHA256:

```text
62671a32af3a9683f81a8a6ddd5e1313f0722ea70441e65d5c0ce030ba6d1ac4
```

Kernel text is `0x80000..0xbb000`; rodata ends at `0xc8000` and the complete
kernel/BSS/guard/stack ends at `0x12c000`, below the unchanged DTB window
at `0x200000`. Prior full heap IMG/ZIP are preserved in `image_files` as
`OurOS-Pi5-header-experimental-sd-20261112-204234-603.img` and
`OurOS-Pi5-header-experimental-sd-20261112-204237-636.zip`.

Completed checks:

- Native PMM/heap/MMU: `build/phase-mmu-memory.log`.
- Focused actual protection faults on QEMU A53/128 MiB and A76/4 GiB:
  `build/qemu_virt/mmu-cortex-a53-128-off.log` and
  `build/qemu_virt/mmu-cortex-a76-4096-off.log`.
- Focused firmware-style EL1/EL2 protection/return tests:
  `build/phase-mmu-handoff-protection.log`.
- Comprehensive MMU-on monitor/heap/filesystem/virtio storage plus fatal fault
  regression: `build/phase-mmu-qemu.log` (used the prior heap disk as read-only media).
- MMU-on EL2-to-EL1 USB BOT/FAT/firmware files, host-matched sector hash and all
  DMA ring wraps: `build/phase-mmu-usb-qemu.log` (prior heap disk); final packaged
  media is checked again in `build/phase-mmu-final-usb-qemu.log`.
- Real packaged-image FAT/VFS/storage native tests: `build/phase-mmu-storage.log`.
- Independent MBR/FAT/file/config validation, extracted ZIP/disk SHA256:
  `build/phase-mmu-image.log`. `7z t` also passes.
- Corrupted MBR/FAT/kernel and wrong-console profile refusal, original image
  unchanged: `build/phase-mmu-image-negative.log`.

Native/QEMU checks do not mark the Pi MMU driver regression complete. No physical
device was written and no Git commit or push was performed. Known generated
objects/test disks are cleaned after verification; current images, dated backups,
ELF/maps and logs remain available.
