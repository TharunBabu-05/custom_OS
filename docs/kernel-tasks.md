# Cooperative kernel tasks

The [latest Pi capture](../proofs/pi5-timer-irq-usb-2026-11-12.txt) verifies the
previous exception/interrupt phase: synchronous and asynchronous register
return, GICv2/PPI30, advancing timer IRQ counts, start/stop, protected memory,
RAMFS edits and USB/FAT reads with the timer running. `special/spurious`
includes the empty acknowledgement at the end of the IRQ drain loop; matching
the delivered count with `unexpected=0` and `fault=no` is normal in this test.

This revision adds **cooperative EL1 kernel tasks**: private stacks, saved call
contexts, round-robin yield, timed sleep/wakeup, explicit/implicit exit and
return to the monitor. Native policy/mapping tests and QEMU A53/A76/EL1/EL2
checks cover the behavior. The [new Pi capture](../proofs/pi5-cooperative-tasks-usb-2026-11-12.txt)
now passes both task runs before/after USB/FAT, private-stack IRQ return,
memory tests and timer stop. The next phase is [persistent storage](persistent-storage.md).

## Image and hardware test

Use the complete [disk ZIP](../image_files/OurOS-Pi5-header-experimental-sd.zip),
extract its `.img` and flash that image to the USB pendrive as before. The local
full image is `image_files/OurOS-Pi5-header-experimental-sd.img`. Earlier full
images and ZIPs remain under dated names in the same folder. Build scripts
write regular files only; no physical disk or Git remote is modified.

At the ordinary `>` prompt, run each command separately and wait for its result:

```text
mmuinit
irqinit
timerstart
schedtest
taskinfo
timertest
heaptest
pagetest
write /tmp/tasks.txt tasks-ok
cat /tmp/tasks.txt
rm /tmp/tasks.txt
storageinit
cat /boot/CONFIG.TXT
storagetest
schedtest
taskinfo
timerinfo
timerstop
uptime
```

`schedtest` should finish in roughly a fraction of a second plus UART output,
without asking for Wi-Fi credentials or running the slower crypto tests. Expect:

```text
PASS: three guarded kernel tasks yielded/slept/exited; callee registers/stack/heap data preserved; PMM/heap counts restored.
```

`taskinfo` should report `last_result=passed`, three `state=exited` records,
each with `sleeps=4 wakes=4 completed=8 exit=0`, and a positive
`private_stack_timer_irqs` count. Dispatch counts can vary slightly with wake
timing. The task stack usage measure is a fill-pattern estimate, not a precise
maximum. A second `schedtest` creates fresh contexts and runs the same checks
again; no task remains in the background. `timerinfo` should still show
`fault=no`; `timerstop` masks CPU IRQs as before.

The first `mmuinit` must precede `timerstart`. Calling `schedtest` before MMU
activation or with the timer stopped is refused without creating tasks.
Do not run `usbtest` or `usbenum` before `storageinit` on that boot. The ordinary
`fault` command is still fatal and is not part of the physical test sequence.

Please share output from `schedtest` through `taskinfo`, plus the storage and
timer results. Include earlier output if initialization fails.

## Implementation and scope

- The pure [queue policy](../kernel/scheduler/policy.c) tracks ready, running,
  sleeping, exited and cancelled states. Dispatch chooses the next ready task;
  deadline processing wakes sleepers when the architectural counter reaches
  their deadline. Overflow and invalid-state operations are rejected.
- Each of four reserved task slots owns a 16 KiB stack and a separate unmapped
  4 KiB guard page. The 80 KiB arena is part of the reserved kernel BSS and is
  excluded from free physical pages. Stack mappings are RW/XN. Guard pages
  become inaccessible when `mmuinit` activates the immutable identity tables.
- The [assembly switch](../arch/aarch64/context/switch.S) saves/restores
  x19..x30 and SP at explicit procedure calls. AAPCS64 requires preservation of
  the callee-saved registers and 16-byte SP alignment; caller-saved registers
  and NZCV may change across a yield call. FP/SIMD remain disabled by the
  kernel build. See the [Arm procedure-call ABI](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst).
- Context ownership and queue transitions occur with CPU IRQs masked. Each
  resumed task restores its own saved mask. Timer entry still uses the full
  existing exception frame and returns to the interrupted stack. No allocation,
  driver work or task dispatch runs inside the timer ISR.
- Three test workers keep stack locals and heap buffers alive through yields
  and sleeps. Assembly sentinels check x19..x29/SP across context transfers;
  a separate asynchronous sentinel test verifies timer IRQ return on a private
  stack. Explicit exit and return-to-exit both run. Final checks require all
  workers to exit, validate progress and restore heap/PMM counts. Stack/context
  contents are wiped afterward; exited status remains visible in `taskinfo`.
- Scheduler idle processing polls the architectural counter with IRQs enabled.
  A five-second/iteration budget cancels pending test tasks and reclaims their
  recorded buffers if progress stops between cooperative calls. It cannot
  interrupt a task that never yields. Unexpected stack/context corruption
  remains fatal; a guard fault is not recoverable task termination yet.

These are kernel tasks in one EL1 address space, not isolated processes. The
monitor and polling storage/network drivers remain synchronous. Preemption,
background networking, task-safe driver ownership, blocking I/O, EL0/system
calls and per-process memory are later stages in the [OS checklist](os-status.md).

## Verification

`scripts/test-tasks.ps1` tests limits, fairness, exact wake boundaries, zero
delay, all-sleep idle, exit/cancel, reset/reuse and failure atomicity. A separate
50,000-decision model checks states, deadlines and dispatch counters.
`scripts/test-mmu.ps1` checks all four guard holes and stack permissions through
an independent descriptor oracle, including overlap and boundary rejection.

`scripts/test-tasks-qemu.ps1` runs the actual assembly on Cortex-A53/A76, then
`-Handoff` covers firmware-style EL1 and EL2 entry. Three repeated task runs,
timer IRQs on private stacks, monitor recovery, heap/pages/protection tests,
stop/restart/refusal and ordinary fatal BRK behavior are checked. The USB
regression runs tasks before and after BOT/FAT reads while IRQs remain active.
These emulated checks do not establish the new task behavior on physical Pi.

## Packaged image

Full disk: **134217728 bytes** (128 MiB). ZIP: **811229 bytes** (793 KiB).
Kernel `OUROS.IMG`: **372396 bytes**. Full-disk SHA256:

```text
8a2126d102c517368512ad30afbeb065dcb592c657b0e6859ccdc0a19ff2d0a9
```

The reserved kernel/BSS/stack ends at `0x148000`, below the unchanged firmware
DTB at `0x200000`. The task arena begins at `0x100000` and occupies 80 KiB,
including four 4 KiB guard holes. The prior IRQ disk and ZIP are retained in
`image_files` as `OurOS-Pi5-header-experimental-sd-20261112-215603-298.img`
and `OurOS-Pi5-header-experimental-sd-20261112-215625-578.zip`.

Checks/logs:

- `build/phase-tasks-memory.log`, `phase-tasks-final-mmu.log` and
  `phase-tasks-final-policy.log`: memory/heap regression, independent mapping
  oracle and scheduling/deadline model.
- `build/phase-tasks-final-context.log`: A53/A76 actual task stacks, register
  switches, timer IRQs on a private stack, repeated runs and failure/refusal
  regression. `phase-tasks-final-handoff.log` covers firmware EL1/EL2 entry.
- `build/phase-tasks-monitor-qemu.log`: full A53/A76 monitor, crypto, memory,
  RAMFS and read-only virtio/FAT regression with task and IRQ tests; media hash
  unchanged. This uses the previous full disk as read-only input.
- `build/phase-tasks-image.log`: Pi kernel, package, MBR/FAT/config/file hashes
  and independent disk extraction. Decompressed ZIP SHA256 matches the full
  image; separate `7z t` passes.
- `build/phase-tasks-image-negative.log`: corrupt MBR/FAT/kernel and mismatched
  console rejected; delivered image unchanged.
- `build/phase-tasks-final-usb-qemu.log`: final disk through EL2 entry, tasks
  before and after USB BOT/FAT reads, firmware-file pins, host-matched
  512-sector hash and all DMA ring wraps with timer IRQs active. Disk hash
  remains unchanged; both task runs and final timer/register tests pass.

The archive and current image names are regular project files. No Git commit,
push, firmware/EEPROM update or physical-device write was performed.
