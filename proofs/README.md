# Real Raspberry Pi 5 proof

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
