# Phase 63 Core OS foundations

## Current scheduler

With routed secondary workers online, normal foreground launches and the
shell use a shared ready queue across the boot CPU and up to three secondary
CPUs. Ready services participate in those rounds. A timer drives preemption
on the boot CPU and SGIs stop worker slices. Workers and the boot CPU return
to kernel mappings before the boot CPU handles the round's traps and syscalls.
Each process appears at most once per round. When there are no routed
workers, the normal launch path uses the existing boot-CPU scheduler.

The kernel reports both assigned `peak` and `measured_peak`, the maximum
number of slices with at least one millisecond of simultaneous execution.
On the Pi, four `SPIN.ELF` processes reached `measured_peak=4`. All ended
with the expected timeout code 124, and shutdown released the GIC routes
and worker stacks. [Four-CPU batch capture](../proofs/pi5-four-cpu-el0-2026-11-28.txt)

## Phase 63 multicore EL0 batches

`appmulti` runs two to eight ELF paths within the existing two-second
noninteractive batch budget. It requires at least two online workers with
verified GICv2 SGI routes. Up to three workers execute distinct EL0
contexts in one round. The architectural counter sets each worker's
quantum and the boot CPU sends the preemption SGI. Workers return their
integer registers, stack pointer and thread pointer, then restore the
canonical root before the boot CPU processes the traps. System calls, driver operations and page
ownership changes remain serialized on the boot CPU. `appsmpshell` uses the
same scheduler for interactive jobs and pipelines. Supervised services run
on the boot CPU between worker rounds.

The batch reports the maximum number of simultaneous assignments and the
number of pairs whose measured slices overlapped for at least one
millisecond. The context-switch count measures dispatches. The preemption
count measures workers stopped by their quantum SGI. The kernel retains
mappings and halts if a worker cannot be confirmed quiescent.

## Demand-zero pages

`uos_vm_reserve(pages, rights, &token)` reserves up to 32 private pages as
read-only or read/write, with execution disabled. Eager and demand-backed
regions share the existing 48-page address-space budget. A first EL0 data
access to an eligible unmapped page allocates zeroed physical RAM and retries
the instruction. Checked syscall buffers validate the whole range before
allocating the missing pages. A failed page allocation leaves the range
unpublished. `uos_vm_info` exposes `UOS_VM_DEMAND` and `resident_pages` on
reservation rows. Protection changes govern both current and future pages.
Unmap, exec, and process teardown release resident pages.

## Mapping updates and TLB invalidation

The boot CPU clears changed user leaves, completes `VMALLE1IS`, and
publishes the replacements. It invalidates translations before withdrawing
backing memory during teardown. All roots use the same inner-shareable
stage-1 regime and the implementation invalidates all ASIDs. Worker roots
are quiescent during process mapping changes. The `smptlb map` test warms
the same alias on four cores, withdraws it, installs another physical page
and withdraws it again. The workers observe the broadcast result without a
local invalidate at the alias. [Arm stage-1 TLB invalidate instruction](https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Instructions/TLBI-VMALLE1IS--TLB-Invalidate-by-VMID--All-at-stage-1--EL1--Inner-Shareable)

## Cache-aware DMA

`rp1dmatest cached` uses one dedicated write-back scratch page for its
descriptor, source, destination and guards. It enables the boot CPU data
cache for the transfer, cleans and invalidates the scratch page before
device ownership, and invalidates it again before inspecting the result.
The test restores the cache setting and DMA claims on normal completion.
Other RAM and live USB/Ethernet DMA areas retain their Normal-NC mappings.
The Pi USB controller workspace now obtains a contiguous PMM run inside
the live inbound DMA window. [Arm cache coherency fundamentals](https://developer.arm.com/community/arm-community-blogs/b/soc-design-and-simulation-blog/posts/extended-system-coherency---part-1---cache-coherency-fundamentals)

## Raspberry Pi acceptance

The Cortex-A53 and Cortex-A76 QEMU profiles passed the multicore batches,
interactive seven-stage pipeline and four-job run, demand paging, and the
four-core remap. The native MMU and RP1 fixtures passed. The verified boot
update replaces `OUROS.IMG` and adds `PAGECHK.ELF` on the Phase 61 profile.

Run one command at a time over serial. The first two commands run in the
startup shell and the rest run at the kernel monitor.

```text
run /boot/PAGECHK.ELF
exit
resinfo
smpstart
smpirq on
smptlb map
appmulti /boot/BUSY.ELF /boot/BUSY.ELF /boot/PAGECHK.ELF
appmulti /boot/SPIN.ELF /boot/SPIN.ELF /boot/FAULT.ELF
appsmpshell
source -e /boot/PROCS35.OSH
exit
rp1dmatest cached
rp1dmatest cached
smpirq off
smpstop
fileinfo
resinfo
storagetest
datacheck
```

The Pi passed the demand-zero check from the startup shell and from a
secondary. The three-worker batch reported 26 overlapping pairs. The fault
batch reported 98 overlapping pairs, two 124 timeouts and a contained fault
with code 125. All four CPUs passed every remap stage. The multicore shell
completed the seven-stage pipeline and reaped all 23 children. Both cached
DMA copies passed. The workers released their routes and stacks, open files
returned to zero, and resource claims returned to 8/16. USB reads and the
mounted v4 data bank passed after cleanup. [Pi serial capture](../proofs/pi5-core-smp-vm-dma-2026-11-27.txt)
