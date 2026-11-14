# Foreground programs from the EL0 shell

**Update:** the [Pi capture](../proofs/pi5-shell-programs-2026-11-14.txt) passes
SPAWNCHK, TTYPAR, normal shell launches, timeout and resource cleanup. Child
Ctrl+C and ORPHAN/CAPCHK are not in that capture. Current images add
[file standard input/output](stdio.md); artifact details below describe the
earlier foreground-program release.

The [Pi file-tools capture](../proofs/pi5-file-tools-2026-11-14.txt) now passes
binary copying, matching SHA-256 digests, synchronous data flush and cleanup.
The `irwinit` typo was rejected normally; `irqinit` then succeeded. No second
boot appears, so physical power-cycle persistence still needs testing.

This stage adds `run <ELF> [arguments]` and `status` to the user shell.
The shell remains alive while a private child runs, waits for its completion,
then prints `child exit=<code>`. `status` prints the last launch result.
Arguments support the same quotes and escapes as builtins. Paths resolve from
the shell's cwd. Use explicit executable paths; there is no PATH search.

## Pi acceptance

Flash the latest full image, then enter these commands one at a time:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
appterm 30 /boot/SPAWNCHK.ELF
apprun /boot/ORPHAN.ELF
apprun /boot/CAPCHK.ELF /boot/CAPCHK.ELF /boot/CAPCHK.ELF
appterm 10 /boot/TTYPAR.ELF
terminal-ok
appshell
run /boot/HELLO.ELF
run /boot/ECHO.ELF "hello from child"
cd /etc
run ../boot/PWD.ELF
pwd
run /boot/FAULT.ELF
status
run /boot/TTYCHK.ELF
terminal-ok
run /boot/SPIN.ELF
```

At the spinning child, press **Ctrl+C**. The shell should print `child exit=130`
and show its prompt again. Continue:

```text
status
run /boot/LEAK.ELF
run /boot/CONFIG.TXT
run /boot/HELLO.ELF
exit
fileinfo
heaptest
pagetest
timerstop
```

SPAWNCHK must PASS with 11 started and 11 collected children; its deliberate
invalid-ELF and missing-file tests print loader errors. Each capacity test must
PASS. TTYPAR must PASS after `terminal-ok`; it forces the child to request input
before its parent waits. HELLO/ECHO/PWD, TTYCHK and LEAK return zero; FAULT returns 125, and the
shell stays usable. The invalid CONFIG.TXT launch fails with shell status 127;
the following HELLO must succeed. Final handles must be zero and heap/page
tests must PASS. `status` records child launches, not builtin results.

SPAWNCHK intentionally uses a 30-second parent session; it includes a one-second
child timeout. Do not run it under the monitor's default two-second batch budget.
The normal shell `run` command gives each child ten seconds. To test that limit,
run SPIN and wait for `child exit=124` instead of pressing Ctrl+C.

## Lifecycle and ABI

- SPAWN=24 takes a readable `struct app_args` pointer in x0 and a 1..30-second
  child budget in x1. The 552-byte argv is copied and validated before loading.
  Success returns a positive PID; errors return the existing negative codes.
- WAIT=25 takes that PID and returns its exit status, blocking the parent if
  necessary. Only the direct parent can wait; a child is collected once. Reused
  slots receive fresh IDs, so stale IDs never name a later child.
- Three total process slots remain. Each original/root process may have one
  outstanding direct child. Nested spawning is refused. A finished child keeps
  its slot until wait or parent termination collects it.
- Children inherit copied arguments and cwd, with newly allocated image/stack
  pages and private page tables. File handles are not inherited. Parent handles
  remain owned by the parent, including their cursors and filesystem leases.
- The waiting shell transfers the serial terminal to its child. An unwaited
  child cannot consume serial input: an early readline blocks until wait grants
  ownership. Ctrl+C targets the terminal owner: a waiting
  child stops, is collected and returns 130; at the shell prompt it ends the
  shell session. Input buffered at ownership changes is discarded.
- The child's budget starts at spawn. Timeout returns 124, contained fault 125,
  and a kernel cleanup/IRQ failure uses 126. The enclosing session budget still
  applies to both parent and child. Parent termination cancels and collects any
  outstanding child. All parent handles close when that parent finally exits.

The kernel switches to its own page-table root for all lower-EL syscall/IRQ work,
so allocation and DMA checks see the verified kernel policy. It restores the
selected private root before returning to EL0 and never frees an active child
page table. Child exit, fault,
timeout, cancellation, failed load and final session teardown have explicit
cleanup paths. The kernel and storage drivers still complete synchronously with
IRQs masked; only EL0 frames are preempted.

This provides foreground launch/wait. Background jobs, arbitrary process trees,
pipes, redirection, signals, credentials, environment variables, general writable
storage, packages and local UI remain on the [roadmap](remaining-os-roadmap.md).

## Image and validation

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
Verified full-image ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip`
(844051 bytes). Extract the image before flashing. Reflashing replaces `/data`.
Prior complete images/ZIPs remain archived directly in `image_files`.

Image SHA-256:
`fa7361aa1903e0cde318f40983d8cbc2370225674faf1359f5d89fa8065aaaf1`.

The Pi kernel is 417508 bytes with reserved end `0x188000`, below the firmware
DTB at `0x200000`. All 29 ELF files fit the 4096-byte persistent-file limit;
SHELL.ELF is 3728 bytes. ELF fixture checks and 25000 deterministic parser
mutations pass, as do the full disk/ZIP verification and strict cross-builds.

QEMU Cortex-A53/EL1 and Cortex-A76/EL2 pass the expanded shell/child suite and
the previous application/protection/preemption/sleep/installation regressions.
The tests include early terminal reads, a child shell with refused nested spawn,
slot exhaustion/reuse, stale IDs, parent termination, child faults/timeouts,
Ctrl+C, enclosing-session expiry, resource restoration, checksums and fresh-boot
data reads. Independent snapshot decoding verifies hashes/bytes and unchanged
boot/superblock/unused sectors; delivery image hashes remain unchanged.

Logs: `build/phase-spawn-{build,image,elf,smoke,qemu,regression}.log`.
Physical acceptance for the subset listed above now passes. No physical media was written and
no Git commit or push was performed.
