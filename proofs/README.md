# Real Raspberry Pi 5 proof

The SanDisk boots directly into the EL0 shell, `df /data` reports v4,
and `exit` returns to the monitor. `V4CHK.ELF` passes its 16 KiB file
checks with exit 0 and restored resources. [Pi capture](pi5-startup-v4chk-2026-11-21.txt)

The [v4 mount capture](pi5-data-v4-mount-2026-11-21.txt) shows the SanDisk
Cruzer Blade, a read-only FAT boot mount, the v4 data partition and a successful
`DF.ELF` run with restored resources. It records a manual boot.

The latest [shell-job excerpt](pi5-shell-jobs-2026-11-15.txt) verifies one
background SPIN child, responsive parent input/echo, stop/collection with 130,
an empty job record and shell exit with code 0/resources restored. Its scope
does not include fg/bgio or reboot/power-loss behavior.

The latest [process-control capture](pi5-process-control-2026-11-15.txt) passes
PS check (12 children collected) and the interactive stop/start demo, with
code 0 and resources restored. Shell jobs use that foundation; their narrower
physical evidence is recorded above.

The latest [byte-allocator capture](pi5-user-allocator-2026-11-15.txt) passes
HEAPCHK and HEAPDEMO with code 0, zero live pages and restored resources.
Its scope is these two runs. The earlier
[dynamic-page capture](pi5-user-memory-2026-11-15.txt) passes MEMCHK and MEMDEMO.
The [current status](../docs/os-status.md) links earlier MMU, timer, process,
filesystem and networking evidence and tracks the remaining implementation.

Original PuTTY screenshots supplied by the project author on 8 November 2026.
They are preserved unchanged and displayed in the main README.

| File | Evidence |
| --- | --- |
| [15:43:10](Screenshot%202026-11-08%20154310.png) | Firmware UART setup, board revision and image loading |
| [15:43:52](Screenshot%202026-11-08%20154352.png) | OurOS banner, EL2 entry, EL1 execution and monitor startup |
| [15:44:07](Screenshot%202026-11-08%20154407.png) | Commands, advancing uptime, echo, CPU ID and expected RAM-discovery limitation |
| [17:41:24](Screenshot%202026-11-08%20174124.png) | Physical RAM discovery, page patterns, allocations and double-free rejection |
| [18:14:54](Screenshot%202026-11-08%20181454.png) | MB/MiB summaries and real-board RAMFS write/list/read/navigation |

The timestamp labels come from the filenames; they are not independently
verified measurement timestamps. The GIF in `docs/assets/` is a separately
labelled illustration. See [the verification record](../docs/pi5-hardware-verification.md)
for the tested setup and remaining limits.
