# Real Raspberry Pi 5 proof

Phase 58 verified cancellation and Ethernet resource cleanup. The router
trial recorded one completed transmission before shutdown and left the
OFFER and ACK unverified. [Phase 58 Pi capture](pi5-wired-dhcp-probe-2026-11-27.txt)

Phase 57 matched all three UDP probes on their first attempts without
repeats or queue drops, then passed the 16 KiB TCP half-close. GEM and
DMA frame counts matched and the Ethernet resource claims were released.
[Phase 57 Pi and Windows capture](pi5-gem-tx-ring-2026-11-27.txt)

Phase 56 matched all three UDP probes on their first attempts. Windows
received three distinct requests and logged twelve echoes in total.
The Pi counted two mismatched replies and five full-queue drops.
[Phase 56 Pi and Windows capture](pi5-udp-probes-2026-11-27.txt)

Phase 55 recorded one carrier loss and one recovery, followed by a full
16 KiB TCP half-close. Both transports advanced to generation 2. The
UDP echo in the same capture timed out.
[Phase 55 recovery and TCP capture](pi5-ethernet-recovery-2026-11-27.txt)

The first Phase 55 run passed the wired UDP echo and resource cleanup.
The TCP child reached its process deadline and the capture shows one
transport generation. [Initial Phase 55 Pi and Windows capture](pi5-ethernet-recovery-initial-2026-11-26.txt)

Phase 54 passed its idle session, checked-output and network permission
tests on the Pi. The wired traffic counters have a separate acceptance run.
[Phase 54 Pi netstat capture](pi5-netstat-idle-2026-11-26.txt)

Phase 53 passed the wired TCP half-close test. Windows received the
client FIN and sent a final reply. The Pi read the reply before EOF,
and both sides reported the same SHA-256 for 16,384 bytes.
[Phase 53 wired TCP half-close capture](pi5-tcp-half-close-2026-11-26.txt)

Phase 52 passed the direct Ethernet TCP echo. The Pi and Windows
reported the same SHA-256 for 16,384 bytes, and the Pi app exited cleanly.
[Phase 52 wired TCP capture](pi5-tcp-deadlines-2026-11-26.txt)

Phase 51 passed the direct Ethernet UDP echo twice on the Pi.
Windows sent a nine-byte reply and OurOS matched the datagram.
[Phase 51 wired UDP capture](pi5-socket-readiness-udp-2026-11-26.txt)

Phase 50 passed timer ownership, exec cleanup, a 75 ms one-shot, fault
and timeout cleanup, and service restart. The timer count returned to
zero after shutdown. USB and both v5 data banks passed.
[Phase 50 capture](pi5-user-timers-2026-11-26.txt)

Phase 49 passed the process-history ABI check and kept the PID across
an image replacement. The capture shows exit, fault, cancellation,
timeout, and successful reaping. The monitor kept 32 of 34 records.
The Pi returned to two allocated pages, eight resource claims, and zero
open files. USB storage and both data banks passed.
[Phase 49 capture](pi5-process-events-2026-11-26.txt)

Phase 48 passed the system statistics ABI check and three piped timer
samples on the Pi. Each sample delivered 26 timer interrupts. The
worker report showed two ready and one parked, then zero after shutdown.
The Pi returned to two allocated pages, eight resource claims, and
zero open files. USB and both v5 banks passed.
[Phase 48 capture](pi5-system-stats-2026-11-26.txt)

Phase 47 allocated a five-page run below 4 GiB, trimmed it to two
pages at the same address, and freed the rest. The physical memory
layout returned to its starting state. All three SMP workers released
their leases and shut down. The Pi returned to two allocated pages and
eight resource claims. USB storage and both v5 banks passed.
[Phase 47 capture](pi5-page-placement-2026-11-26.txt)

Phase 46 passed four rounds with three workers and two rounds with one
worker parked. Workers released all 64 grants. The 32-job queue, timer,
USB read test and both v5 banks passed. A UART receive error interrupted
the shutdown sequence before the final CPU and allocator readings.
[Phase 46 capture](pi5-smp-page-leases-2026-11-26.txt)

Phase 45 passed the lock timeout check, shared queues, cancellation
and queue reuse, and remapping with parked and fault-parked workers.
The 256-job queue was split 86/85/85 across the three workers.
The timer, USB reads, and both v5 banks passed. The final shutdown
and allocation snapshots are included in the Phase 46 procedure.
[Phase 45 capture](pi5-smp-work-queue-2026-11-26.txt)

Phase 44 passed all four TLB acknowledgements and all five remap
stages on the Pi. The three workers were online. The park command
with an uppercase `0X` prefix was rejected, so Phase 45 includes the
parked and fault-parked cases and the final cleanup checks.
[Phase 44 capture](pi5-smp-page-remap-2026-11-26.txt)

Phase 43 verified the targeted and broadcast IPIs, timer delivery
with zero unexpected IRQs, SGI claim release, and secondary shutdown.
The Pi returned to two allocated pages and eight resource claims.
USB reads passed and v5 generations 41 and 40 were valid. Physical
TLB verification is included in the Phase 44 procedure.
[Phase 43 capture](pi5-interprocessor-irqs-2026-11-26.txt)

Phase 42 passed same-boot resume after a test exception and an explicit
park. All six checksum batches matched. Cancellation succeeded and
all three secondary cores powered down. Allocations returned to two
pages, USB reads passed, and v5 generations 41 and 40 were valid.
[Phase 42 capture](pi5-worker-recovery-2026-11-26.txt)

Phase 41 brought up all three secondaries, ran checksum jobs, and isolated
the breakpoint test. A subsequent firmware restart stayed pending at
affinity `0x100`, so the kernel retained one stack page. USB reads and both
v5 banks passed. [Phase 41 capture](pi5-secondary-cores-2026-11-26.txt)

Phase 40 passed irreversible process restrictions, inheritance through
spawn and exec, and restricted pipeline output. The service rejected
descriptor transfers and answered plain IPC before and after restart.
Final checks showed no listeners or open files and valid v5 generations
41 and 40. [Phase 40 capture](pi5-process-permissions-2026-11-26.txt)

Phase 39 passed all eleven process rows, three service echoes, dependent
shutdown, and an independent service restart with a new PID. Final checks
showed zero listeners and open files, and verified v5 generation 41.
[Phase 39 capture](pi5-services-2026-11-26.txt)

Phase 38 passed the event-wait and concurrent echo checks. Each shell
collected every child. Shutdown released the listener and all handles,
and the Pi confirmed v5 generation 41. [Phase 38 capture](pi5-event-wait-2026-11-26.txt)

Phase 37 passed the named IPC tests with all seven children reaped.
The supervised echo service answered from the shell and again from the
monitor. Shutdown left zero listeners and file handles, and the Pi
verified v5 generation 41. [Phase 37 capture](pi5-named-ipc-2026-11-26.txt)

Phase 36 passed both VM checks and sent 16,001 mapped bytes through a pipe
with the expected SHA-256. The Pi reaped all 10 children, left zero open
files, and verified v5 generation 41. [Phase 36 capture](pi5-file-mappings-2026-11-26.txt)

Phase 35 verified the seven-stage binary transfer, eight process rows, and
four-job cancellation and collection. The Pi reaped all 23 children, left
zero open files, and confirmed the mounted v5 generation 41.
[Phase 35 capture](pi5-process-capacity-2026-11-25.txt)

Phase 34 verified a 30-to-120 tick split before and after swapping the two
jobs' priorities. The manual nested terminal test accepted `terminal-ok`.
Both applications exited with code zero. [Phase 34 capture](pi5-scheduling-2026-11-25.txt)

Phase 33 verified nested process ownership, collection, slot reuse, and
redirected input on the SanDisk at v5 generation 41. The interactive reader
printed its prompt and returned exit 2. Phase 34 verified the input retest.
[Phase 33 capture](pi5-process-trees-2026-11-25.txt)

Phase 32 verified named-app install, upgrade, rollback, and uninstall,
plus signed repository installation and dependency upgrade on the current
SanDisk. The Pi remounted generation 26 after the power cycle and ran
both installed apps. Cleanup finished with zero open files at generation 41.
[Phase 32 capture](pi5-storage-apps-2026-11-24.txt)

Phase 31 verified path history and recovery from generation 5.
The Pi selected generation 7 after the watchdog reboot.
[v5 file recovery capture](pi5-v5-file-recovery-2026-11-24.txt)

Phase 30 passed the Core OS check with all seven children reaped.
The supervised service restarted once, retained PID 12 across shell
sessions and advanced its heartbeat from 22 to 63 during the
four-stage pipeline check. Shutdown returned to zero open handles,
and the USB and mounted-data checks passed. [Core OS capture](pi5-core-os-2026-11-24.txt)

Phase 29 streamed 16 KiB through three pipe rings on the Pi. All four
children were reaped, both reader and writer waits occurred, and
file, UART, and storage checks passed. [Pipeline capture](pi5-pipe-chain-2026-11-24.txt)

Phase 28 passed handle duplication, access reduction, append control,
close-on-exec and inventory. The test also passed its intentional failed
`exec` case, retained redirected stdout through the successful `exec`,
and returned to zero open handles. [Descriptor capture](pi5-descriptors-pass-2026-11-24.txt)

Phase 27 passed the six file-handle checks and the shell append test.
The append hash matched, all five children were reaped, and the
monitor reported zero open handles. [File-handle capture](pi5-file-handles-pass-2026-11-24.txt)

Phase 26 passed archive creation, member listing, validation, binary
extraction, and tree comparison. All seven children were collected,
and serial, USB, and mounted-data checks passed.
[Archive tools capture](pi5-archives-pass-2026-11-24.txt)

Phase 25 matched the four text output hashes and the expected fruit
counts. Both pipeline stages exited with code 0, and UART, resource
and storage checks passed. [Text pipeline capture](pi5-text-pipelines-pass-2026-11-24.txt)

Phase 24 verified all five WAV edits by hash. The UART reported zero
framing, parity, break, and overrun errors during the run, and the
resource and storage checks passed.
[Batch verification capture](pi5-serial-batch-pass-2026-11-24.txt)

Phase 23 verified stereo tone generation, gain, and downmix. The
transcript shows altered trim and join input paths and a fade command
with missing output redirection. USB and mounted data checks passed.
[WAV and serial capture](pi5-wav-serial-retest-2026-11-24.txt)

Phase 22 passed three ordered-load and ordered-store tests, displayed the
media device inventory, read the RTC alarm state, and generated and analyzed
a PCM WAV file. USB storage and the mounted data bank passed afterward.
[Media and CPU capture](pi5-media-pass-2026-11-24.txt)

Phase 21 listed four CPU affinities, read all PSCI states and reported
L1 through L3 cache geometry. The exclusive-load test halted in the
uncached profile. [CPU diagnostics and abort capture](pi5-cpu-atomic-abort-2026-11-24.txt)

Phase 20 verified the RTC write and UTC sync, read the throttle flags,
and remounted the same data generation after the halt request. Both
alarm queries returned an invalid firmware response.
[RTC and power capture](pi5-rtc-power-2026-11-24.txt)

The PCIe commands reported the expected external and RP1 device-tree
windows. USB readback and the mounted data check both passed.
[PCIe inventory capture](pi5-pcie-inventory-pass-2026-11-24.txt)

Windows reached the configured wired address `192.0.2.6/30`, received
six 512-byte pings with zero loss, and learned the Pi's MAC address.
The hardware resource and storage checks passed afterward.
[Wired IPv4 configuration capture](pi5-ethernet-address-pass-2026-11-24.txt)

`TCPCHK.ELF` and the Windows server reported the same SHA-256 for a
16,384-byte wired TCP echo on port 4041.
[Wired TCP capture](pi5-ethernet-tcp-pass-2026-11-24.txt)

`UDPCHK.ELF` exchanged a nine-byte datagram with Windows over Ethernet
at 100 Mbps. The earlier command targeted port 4. The echo listener
was on port 4040. Ethernet cleanup returned to eight resource claims,
and the USB and data checks passed. [Wired UDP capture](pi5-ethernet-udp-pass-2026-11-24.txt)

The Pi passed two wired IPv4 echo exchanges at 100 Mbps. It returned
to eight resource claims and passed the USB and data checks.
[Wired IPv4 capture](pi5-ethernet-ipv4-pass-2026-11-24.txt)

The Pi exchanged ARP frames with Windows over the 100 Mbps cable and
reported the adapter's MAC `20-7B-D5-1A-C0-91`. Releasing Ethernet
returned to eight resource claims and the storage checks passed.
[Successful cable ARP capture](pi5-ethernet-arp-pass-2026-11-24.txt)

The PHY responded while held, but link negotiation remained pending.
`etharp` stopped before the DMA test. The Pi returned to eight claims
and passed USB and data checks. [Pending link capture](pi5-ethernet-link-pending-2026-11-24.txt)

`etharp` resolved a 100 Mbps link with Windows at the test address,
then stopped during MAC setup. PHY release returned the Pi to eight
claims, with USB and data checks passing. [ARP setup capture](pi5-ethernet-arp-setup-2026-11-24.txt)

`ethloop` passed three times. The Pi returned to eight resource claims,
reproduced the prior USB hash and mounted `/data` at generation 1.
[GEM loopback capture](pi5-ethernet-loopback-2026-11-24.txt)

`ethup` held the PHY out of reset until auto-negotiation completed.
Windows reported `Ethernet 3` up at 100 Mbps. After `ethdown`, the Pi
returned to eight claims, reproduced the USB hash, and reported valid
`/data` generation 1. [Ethernet link capture](pi5-ethernet-link-2026-11-24.txt)

PHY reset released the onboard Ethernet PHY long enough to read its ID
`0x600d84a2`. The next probe got no reply after the diagnostic restored
GPIO32. USB storage and `/data` passed. [Ethernet reset capture](pi5-ethernet-phy-reset-2026-11-24.txt)

The Pi read GEM ID `0x00070109` but received no PHY reply on three
attempts. The temporary GEM claim was released and USB and `/data`
checks passed. [Ethernet probe capture](pi5-ethernet-mdio-no-reply-2026-11-24.txt)

The Pi passed the microSD single read, eight-sector head scan, and two
tail scans at capacity minus eight sectors. Both tail hashes matched.
Host power, clock, and pin selection returned to baseline. The USB
read and `/data` generation 1 checks passed. [microSD tail capture](pi5-microsd-tail-2026-11-24.txt)

The Pi completed one microSD sector read. `restore_mask=0x02` isolated
the failure to the host-control and power register word. The subsequent
probe showed the host power and clock off. USB reads and `/data`
generation 1 passed. [Host-control capture](pi5-microsd-control0-restore-2026-11-24.txt)

The spare card returned CMD17 transfer completion. The diagnostic kept
the SD controller claimed because it could not verify the restore. The
USB storage read passed and `/data` stayed at generation 1.
[CMD17 and host restore capture](pi5-microsd-cmd17-restore-2026-11-24.txt)

Inserting a spare microSD changed the active-low card-detect GPIO from
1 to 0 while SDHCI's presence bit stayed set and its power and card clock
were off. USB storage still passed and `/data` stayed at generation 1.
[microSD card-detect capture](pi5-microsd-card-detect-2026-11-24.txt)

With a keyboard attached, the Pi mounted `/boot` from usb1 and `/data`
at generation 1. It identified the keyboard on usb0 port 2 and read HID
usages `0x04` and `0x0c` on successive tests. The storage read passed and
the SDHCI controller reported a stable inserted card with its power and
clock controls off. [Keyboard and SD host capture](pi5-usb-keyboard-live-sd-host-2026-11-24.txt)

Both RP1 USB controllers reported connected devices. Boot storage had
selected the accessory on usb0, left `/boot` unmounted and retained the
usb0 claim. [USB boot selection capture](pi5-usb-accessories-boot-selection-2026-11-24.txt)

The recovery image booted and retained the USB drive and v5 generation 1.
The storage controller's spare USB2 port had a connection change but showed
no device when sampled. [USB port capture](pi5-usb-keyboard-port-2026-11-24.txt)

Three USB inventories matched across two 512-sector reads. The Pi
reported the SanDisk BOT endpoints and retained eight hardware claims
and `/data` generation 9. [USB inventory capture](pi5-usb-inventory-2026-11-22.txt)

GPIO 17 and 27 each passed the `010` input-pull test and restored their
pad and claim. USB storage, eight claims and generation 9 passed.
[GPIO pad capture](pi5-rp1-gpio-pads-2026-11-22.txt)

Both physical GPIO loopback attempts reported a signal mismatch and
restored both pins and claims. USB reads passed and data generation 9
was preserved. [GPIO loopback capture](pi5-rp1-gpio-loop-2026-11-22.txt)

GPIO 27 read back, GPIO 28 returned usage, and reserved GPIO 0 and 14
refused the test. GPIO 17 passed twice and returned to its original
configuration. Eight claims, USB reads and `/data` generation 9 passed.
[GPIO ownership capture](pi5-rp1-gpio-claims-2026-11-22.txt)

The Pi read GPIO 0, 14, 15 and 17. GPIO 27 was unexpectedly rejected.
The PCIe link was active, the root port reported bus numbers `0/0/0`,
and the doorbell reported unmapped. USB readback and generation 9 passed.
[GPIO readback capture](pi5-rp1-gpio-2026-11-22.txt)

Both endpoint MSI-X preflights reported an unmapped MIP doorbell and
stopped at bridge validation. The 512-sector USB read passed, both
resource reports showed six claims and `/data` stayed at generation 9.
[MSI-X preflight capture](pi5-rp1-msix-2026-11-22.txt)

The Pi passed two RP1 DMA copies with guard and cleanup checks, and
two synthetic GIC edges with one deferred callback each. USB storage,
six resource claims and data generation 9 were preserved.
[DMA and edge IRQ capture](pi5-rp1-dma-edge-2026-11-22.txt)

The Pi verified the MIP doorbell and 64-vector range. The DMA setup
failed on register `0x390`, which returned `0x07c08004` after
a zero write. IRQ, USB storage and data generation 9 were preserved.
[MIP and DMA diagnostic capture](pi5-rp1-mip-dma-signal-2026-11-22.txt)

The first RP1 DMA copy build reported a setup/readback error twice.
It released its claims on both runs. The following IRQ and USB storage
checks passed, and data generation stayed at 9.
[RP1 DMA setup capture](pi5-rp1-dma-setup-2026-11-22.txt)

The Pi reported the live RP1 IRQ settings and DMA window before and
after a passing 512-sector storage read. The mounted data generation
and six resource claims were preserved. [RP1 capture](pi5-rp1-observation-2026-11-22.txt)

The Pi handled three peripheral IRQ probes and returned the same six
hardware claims each time. Data generation 9 and the serial shell
remained available. [Resource ownership capture](pi5-resource-ownership-2026-11-22.txt)

The corrected RTC read advanced from 25 to 33 seconds. The Pi listed
46 SoC children, resolved USB and Ethernet device paths, and printed
the live header-console address. UTC and data generation 9 were unchanged.
[Device inventory capture](pi5-device-inventory-2026-11-22.txt)

The Pi returned 41.9 °C and zero firmware throttling flags. UTC and
the mounted data generation were unchanged. The original RTC read
failed validation. The corrected driver passed in the capture above.
[Platform health capture](pi5-platform-health-2026-11-22.txt)

The Pi verified `cmp` for matching files and a mismatch at byte 4, and
`hexdump` for a three-byte range starting at offset 1.
[Binary tools capture](pi5-binary-tools-2026-11-22.txt)

The Pi found both nested text files and counted the seven bytes written
to them. Both commands exited with code 0.
[Directory tools capture](pi5-directory-tools-2026-11-22.txt)

The Pi listed numbered shell history, replayed a pipeline with `!!`
and an earlier command with `!1`, then cleared the history.
[Shell history capture](pi5-shell-history-2026-11-22.txt)

The Pi passed shell redirection and two-stage pipelines. Both pipelines
exited with code 0, and the shell printed `1 3 17` and `streams-ok`.
[Shell streams capture](pi5-shell-streams-2026-11-22.txt)

The Pi passed `grep`, `wc`, and `head` from the serial shell. Both
direct input and a pipeline returned `1 3 17` with exit code 0.
[Text tools capture](pi5-text-tools-2026-11-22.txt)

The Pi saved and installed a 512-byte ELF served by Windows over pinned
TLS. It refused the duplicate download and ran the managed app with
exit code 0. [Package download capture](pi5-package-download-2026-11-22.txt)

The Pi and Windows passed a live 9-byte TLS 1.3 encrypted echo. Windows
verified client Finished, and the Pi exited with code 0 and restored
app resources. [Live test result](../docs/tls-live.md#local-verification)

The Pi passed the enrolled OpenSSL TLS 1.3 transcript with code 0.
Unenrolled, wrong-name, changed ClientHello and changed Finished cases
returned code 1 with resources restored. [Pi TLS transcript capture](pi5-tls-transcript-2026-11-22.txt)

With UTC set, the Pi rejected the demo certificate before pin enrollment,
passed it with exit code 0 after enrollment, rejected the wrong DNS name,
and cleared its trust anchors. [Pi certificate capture](pi5-pinned-cert-2026-11-22.txt)

Operator-set UTC advanced on the Pi, the EL0 clock test exited with code
0, and trust preflight accepted the valid window and rejected the expired
one. [Pi UTC and trust capture](pi5-trust-time-2026-11-22.txt)

The Pi and a Windows PC echoed a 16 KiB file over TCP. Both reported
the same SHA-256 and `TCPCHK.ELF` exited with code 0.
[Pi and Windows capture](pi5-tcp-echo-2026-11-22.txt)

The Pi and a Windows PC exchanged a 10-byte UDP datagram over the local
Wi-Fi network. `UDPCHK.ELF` verified the echoed payload and exited with
code 0. [Pi and Windows capture](pi5-udp-echo-2026-11-22.txt)

The corrected Pi build passed shell help, two-job control, terminal handoff,
command lists and `source`. The watchdog reboot remounted USB storage, retained
generation 14 and the saved file's SHA-256, and passed the timer test.
[Passing capture](pi5-safe-reboot-2026-11-22.txt) · [First capture](pi5-shell-workflows-2026-11-22.txt)

The Pi delivered two BCM compare interrupts and ran two deferred callbacks.
The periodic timer, EL0 preemption, FAT reads and snapshot bank checks passed.
[Peripheral IRQ capture](pi5-peripheral-irqs-2026-11-22.txt)

The Pi distinguished a full private quota from fragmentation and recovered
all private pages. The physical-page, kernel heap and file-handle counts
matched before and after. [Memory pressure capture](pi5-memory-pressure-2026-11-21.txt)

The installed 10,664-byte ELF still ran after a power cycle on the SanDisk
drive. The 16 KiB file retained its SHA-256 and the mounted generation
remained 14. Windows verified the earlier generation 10 serial export.
[Pi persistence capture](pi5-v4-persistence-2026-11-21.txt)

The Pi passed `HEAPCHK.ELF resize` with child exit 0. The test checks
in-place growth, preserved contents, zeroed new bytes and tail-page
reclamation. [Pi capture](pi5-heap-resize-2026-11-21.txt)

The open-ended shell responded after idling, gave another prompt after each
Ctrl+C, and exited with code 0 and resource cleanup. [Pi capture](pi5-open-shell-2026-11-21.txt)

The SanDisk v4 volume passed `datacheck` on the Pi. Both generations were
valid and generation 11 matched the mounted filesystem. [Pi capture](pi5-datacheck-2026-11-21.txt)

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
