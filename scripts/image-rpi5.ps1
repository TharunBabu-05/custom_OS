# Writes a regular project-local disk-image file; never opens a physical device.
[CmdletBinding()]
param([ValidateSet('debug','header')][string]$Console = 'debug')
. "$PSScriptRoot/windows-tools.ps1"
. "$PSScriptRoot/rpi5-profile.ps1"
$piProfile=Get-Rpi5Profile $Console
& "$PSScriptRoot/package-rpi5.ps1" -Console $Console
$out=Join-Path $ProjectRoot $piProfile.Output
$work=Join-Path $out ('disk-stage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$bundle=Join-Path $work 'bundle'
Expand-Archive -LiteralPath (Join-Path $out 'ouros-rpi5-boot-files.zip') -DestinationPath $bundle
& "$PSScriptRoot/verify-rpi5-package.ps1" -Directory $bundle
$files=Join-Path $work 'files'
New-Item -ItemType Directory -Path $files | Out-Null
# Explicit 8.3 names avoid dependence on a custom long-filename implementation.
$map=[ordered]@{'OUROS.IMG'='ouros-rpi5.img';'PI5.DTB'='bcm2712-rpi-5-b.dtb';
    'COPYING.TXT'='COPYING.linux';'LICENSE.TXT'='LICENCE.broadcom'}
foreach ($name in $map.Keys) { Copy-Item -LiteralPath (Join-Path $bundle $map[$name]) -Destination (Join-Path $files $name) }
. "$PSScriptRoot/wifi-assets.ps1"
foreach ($asset in $WifiAssets.files) { Copy-Item -LiteralPath (Join-Path $WifiCache $asset.name) -Destination (Join-Path $files $asset.name) }
$config=Get-Content -Raw -LiteralPath (Join-Path $bundle 'config.txt')
$config=$config.Replace('kernel=ouros-rpi5.img','kernel=OUROS.IMG').Replace('device_tree=bcm2712-rpi-5-b.dtb','device_tree=PI5.DTB')
[IO.File]::WriteAllText((Join-Path $files 'CONFIG.TXT'),$config,[Text.Encoding]::ASCII)
$readme=@'
OurOS experimental Raspberry Pi 5 UART boot image
HARDWARE BOOT UNTESTED. This is an early kernel, not a complete operating system.
No GUI/HDMI, USB keyboard, preemption or user-space applications.
Opt-in MMU identity protection and foreground Wi-Fi/DHCP/DNS/ping are implemented.
Boot firmware reads FAT; the kernel creates a
separate volatile RAMFS root. Run storageinit to attempt a read-only FAT32 mount.
Only the dedicated UART10 console is used.

Requires Pi 5, working EEPROM firmware and a 3.3 V serial adapter/cable at the
3-pin UART connector between the HDMI ports. Terminal: 115200 8N1, no flow control.
Expected message: OurOS: AArch64 kernel booted
Commands: help info cpuinfo meminfo memmap uptime echo clear halt fault
Memory commands: pagealloc, pagefree <slot>, pagetest.
Current phase: USB single-sector writes/flush and persistent /data files.
Boot FAT32 partition is read-only. A separate 1 MiB type-DA partition stores
two SHA256-protected snapshots, eight flat files up to 4096 bytes each.
At normal > run storageinit, datainit, datainfo, write /data/note.txt saved,
cat /data/note.txt, sync. Wait for each command. After power-cycle, run
storageinit, datainit, cat /data/note.txt: saved should remain. /tmp stays RAM.
Every data update flushes and verifies its inactive bank before committing;
no auto-format or FAT metadata writes. Reflashing a new image resets /data.
New Pi writes/durability still need their own hardware test. See docs/persistent-storage.md.
Latest Pi capture verifies cooperative task contexts/stacks/yield/sleep/exit,
MMU, exception return, timer IRQs and USB reads with tasks before/after storage.
Boot still masks IRQs. At normal > run mmuinit, irqinit, timerstart,
schedtest, taskinfo, timertest, heaptest, pagetest, storageinit,
cat /boot/CONFIG.TXT, storagetest, schedtest, taskinfo, timerinfo, timerstop,
uptime. Expected: three guarded tasks PASS, last_result=passed,
state=exited and sleeps=4 wakes=4 completed=8 for each of three workers;
private_stack_timer_irqs positive, timer fault=no and memory counts restored.
See docs/kernel-tasks.md. Cooperative task behavior now has Pi evidence.
GICv2/PPI30 uses validated DT profiles. Firmware must expose the NS timer PPI.
Handlers rearm/count/EOI only; no UART/heap/storage/network I/O from the ISR.
Task switching is cooperative at explicit yield/sleep/exit calls, not in ISR.
Four static slots, 16 KiB stack + unmapped 4 KiB guard per slot; three workers
exercise contexts, stack/heap data, timed wake, implicit/explicit exit and IRQ
return on a private stack. schedtest is bounded and returns to the monitor.
No preemption, background commands, EL0, nested IRQs or peripheral handlers.
At the normal > prompt run: mmuinfo, mmuinit, mmuinfo, mmutest, heaptest,
pagetest, fsinfo, meminfo, storageinit, cat /boot/CONFIG.TXT, storagetest,
pagetest, mmutest, uptime. Wait for each prompt. See docs/mmu.md.
mmuinit is opt-in; boot starts with MMU off. Caches stay off, RAM is Normal-NC,
device windows are Device-nGnRnE. Code is RO executable, constants/DTB/tables
RO XN, data RW XN; address zero, boot/task stack guards are unmapped.
mmutest intentionally catches five faults, then returns to the monitor.
Unexpected faults still halt. Repeat mmuinit never rewrites active tables.
USB accepts only MMU-off/verified identity profile with caches off and writable
Normal-NC DMA buffers. MMU-on USB reads/tasks have Pi evidence; the new data
write phase needs its own hardware test. MMU-on Wi-Fi is not newly established.
Full feature/driver checklist: docs/os-status.md. Preemption, EL0,
general VFS/writable FAT metadata and long-lived networking remain future stages.
Earlier DTB memory discovery and 4 KiB pages have Pi hardware evidence.
MB/MiB summaries and RAMFS commands now have physical Pi evidence.
SDIO and chip 0x4345 revision 6 have physical header-console evidence.
USB block/FAT reads and ring cycling now have physical header-console Pi evidence.
Dynamic reserved-memory alloc-ranges windows are conservatively excluded in full.
File commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.
RAM root files disappear after reboot; /data files persist after a successful
commit/sync. See docs/ramfs.md. Seed files share two heap pages.
Storage: storageinfo, storageinit, ls /boot, cat /boot/CONFIG.TXT.
Pi driver inherits an active SDHC/SDXC card from firmware; no full init yet.
Firmware USB boot works on the user's header build with a 32 GB pendrive.
The retained -sd.img filename does not limit the disk image to SD media.
Header build: storageinit uses USB2 BOT if the SD host is absent/unclocked.
On a fresh boot use storageinit BEFORE usbtest/usbenum; it includes enumeration.
Then ls /boot, cat /boot/CONFIG.TXT, storagetest, storageinfo, cat again.
usbinfo: DT USB hosts. usbprobe: read-only xHCI controller/port observation.
USB MMIO probe requires the header UART's known firmware RP1 window.
usbenum reads direct USB2 device/configuration descriptors and BOT endpoints.
This descriptor path now passes on the physical header-console Pi.
It runs No-ops, resets a port, assigns an address, then reads descriptors.
storageinit selects configuration/bulk endpoints for read-only SCSI LUN 0.
USB capacity, sectors, FAT reads and bulk/event ring cycling pass QEMU and Pi.
Only direct USB2 BOT disks with 512-byte sectors; no hubs/USB3/UAS/hotplug.
USB discovery has physical evidence. usbtest selects a connected controller,
tests four No-op command/event DMA completions, then halts it. Reboot to repeat.
It leaves PCIe/UART setup intact and issues no USB transfers or disk commands.
/boot is read-only; no card write commands exist in this revision.
Wi-Fi: wifiinfo, wifiprobe (diagnostics only; no scan/connect/IP).
wifi is an alias of wifiinfo. wifiinit selects/identifies an SDIO device and
reads its CIS with CMD52 after guarded C0/C1/D0 WLAN cold preparation.
It now enables function 1, requests ALP and reads the ChipCommon ID via CMD53.
The function-0 CMD53 CIS probe rejected on the Pi has been removed.
Cold preparation powers WLAN and configures the SDIO pins and 400 kHz clock.
WLAN cold preparation, chip 0x4345 revision 6 and USB BOT descriptors have Pi evidence.
wificores (after wifiinit): AI core table and CR4 RAM-bank sizing.
Five cores, 800 KiB TCM and selector restoration now have physical Pi evidence.
wififiles verifies SHA256 pins for FW43455.BIN, WIFINV.TXT and WIFICLM.BIN.
wifiload (after storageinit, wifiinit, wificores) uploads and reads back code
and packed board settings. It requires the observed chip/core/RAM profile,
keeps the radio CPU halted and holds D11 in reset. Full upload/readback now
passes on the physical Pi (609309 code bytes and packed NVRAM).
F1 block size=64, host block counting and <=64-byte RAM requests are verified.
wifistart is separate: install vector, release CR4 and check HT/shared memory.
Startup now passes on the physical header-console Pi: shared memory ready.
wificontrol (after wifistart) enables F2, checks protocol/credits and queries
the ioctl ABI and actual firmware version. Physical control now passes:
ioctl ABI 2, firmware 7.45.265. wificaps reads features/MAC/PMF/supplicant.
wifiprepare IN verifies/loads the pinned CLM, checks status/version and sets
and reads country IN. Physical preparation now passes for IN revision 0:
sae_ext, PMF value 1, CLM status 0; sup_wpa reports unsupported.
wifiscan enables result events/interface, requests passive discovery, displays
SSID/BSSID/channel/RSSI/security, then DOWN/restores the mask on success.
Physical scanning now passes including Jack's / SAE / required PMF.
cryptotest runs HMAC/HKDF published known-answer tests.
p256test checks P-256 scalar/point validation, group operations and RFC5903
public/shared-point known answers. It uses public fixtures only, not credentials.
Host known-answer and independent OpenSSL comparisons cover this new math path.
Physical P-256 now passes, including repeat execution; storage/pages still pass.
h2etest adds group-19 H2E SSWU mapping, password-token derivation and MAC-bound
password elements using synthetic public fixtures only. This self-test accepts
no credential. Physical H2E now passes, with storage/pages intact.
saetest adds an in-memory group-19 SAE exchange with public synthetic peers.
It checks commits, confirms, key derivation, bad-confirm rejection and key export
gating. No real credentials, radio authentication, association or IP requests.
Native exchange vectors and rejection tests pass; physical saetest now passes.
Math tests are optional for the auth-policy check below. Their first execution
can take minutes with caches off; results are cached until reboot. Append
' force' to p256test, h2etest or saetest for another full run; elapsed seconds
are reported. No arithmetic validation or entropy checks have been weakened.
wifiauthprep <SSID> temporarily sets/readbacks SAE auth, AES, required PMF and
the extended event mask (187..190), then restores the entire saved configuration.
Run it after wifiscan/wifitarget; it never raises the interface or joins a network.
If restoration is unconfirmed, control access is blocked until reboot.
The latest capture verifies the temporary event mask and full restoration.
auth reads 2 immediately after SET auth=3; ordering did not resolve it.
wsec=0x604 is AES plus PMF flags, now accepted with separate required mfp=2.
Unknown/extra cipher bits and optional PMF still fail. The strict wifiauthprep
diagnostic can still report the auth=2 mismatch; it is optional below.
wifijoinprobe <SSID> is a new one-shot external-SAE request observation. It
first initializes station mode, snapshots/applies security/events, raises the interface,
rechecks live security/events, then issues one fixed BSSID/channel/SSID join.
The earlier 2026-11-10 capture stopped at INFRA=0 before UP/join. The driver requires
AP=0 and UP=0, accepts only INFRA 0/1, selects INFRA=1 if needed, and verifies
GET_INFRA=1 before continuing. An already-STA interface skips the SET. The
role is checked again after UP. Station selection is retained this boot;
security/event snapshots are taken after this initialization and restored.
The latest 2026-11-10 capture passes station setup, matched request delivery,
DOWN and security/event restoration. No role-0 fallback is used.
auth=2 may proceed only to this bounded observation with SAE-only wpa_auth,
AES and required PMF. Only a matched firmware external-SAE request can pass.
After observation/timeout/error it sends DOWN before restoring policy/mask.
It sends no password, SAE response, auth success, keys or IP. It is not a
connection command. Pending join/SSID cache is not restored: reboot to repeat.
The request-only path is physically verified. Wait 15 seconds plus I/O.
Image ZIPs stay local; the user currently handles GitHub publication.
wifisaeprobe <SSID> adds a one-shot fresh group-19 SAE commit and peer-reply
observation. On a fresh boot skip wifijoinprobe; run wifisaeprobe after target.
It checks RNG200, accepts a hidden local password, derives the token/PWE and
fresh commit BEFORE UP, then erases the password and all private SAE state.
Preparation can take minutes with caches off. Wait for progress and the prompt.
Use PuTTY local echo and local line editing Force off. At the hidden password
prompt type it without quotes, then Enter. Printable ASCII 1..128 bytes only;
Backspace/Delete edit, Ctrl+U clears, empty Enter cancels. Rejected lines are
drained and wiped. UART is not encrypted; no credential is saved or packaged.
The probe sends one commit after the matched request, waits for the correct
packet-ID TX ACK and a peer commit, then DOWN/restores. Peer curve/reflection
validation happens after DOWN. Public transcript buffers are then erased.
An earlier Pi capture passes commit SET/TX ACK but rejects RX metadata v1 in
the old parser. This revision accepts the documented 16-byte v1 header, keeps
legacy v0 support, and rejects v2 rather than interpreting different offsets.
Fixed group-19 bodies are accepted, without optional IEs.
Anti-clogging/unknown metadata/status/lengths stop with diagnostics, not success.
The probe cannot prove password correctness. No confirm, auth-success, keys,
association, EAPOL/PMF data path or IP stack is implemented in this commit-only path.
Reboot after any started preparation/probe. This commit-only probe does not retry.
wifisaeauth <SSID> is the new bounded live commit/confirm test. On a fresh
boot skip BOTH wifijoinprobe and wifisaeprobe; run wifisaeauth after target.
It uses the same hidden local input and fresh RNG, but retains a private SAE
session only for this synchronous test. Password/token temporaries are erased
before UP. It validates the peer commit, derives keys, sends one confirm with
a distinct packet ID, requires SET/TX ACKs and verifies the peer confirm HMAC.
Key math is outside callbacks; key_math_ms is reported, and >=4000 ms stops
before sending a known-late confirm. It then DOWN/restores and destroys the
session/keys on all attempted exits. No PMK export, firmware auth-success,
association, EAPOL/PMF key installation or IP in this confirm-only command.
The latest Pi capture PASSES peer_confirm_verified=yes, both TX ACKs and
cleanup. Preparation takes 3 seconds; peer key calculation takes 2900 ms.
The first attempt succeeded. Up to THREE identical commit transmissions exist,
with new packet IDs, ONLY after SET success/TX ACK 0/no matched RX for a full
five seconds. It preserves the same private session/commit; no new RNG draws.
NACK, missing ACK, rejected/malformed RX, I/O or stalled clocks stop immediately.
The total missing-commit wait is about 15 seconds plus bounded control I/O.
No confirm retransmission, anti-clogging response or weaker-security fallback.
Capture commit_attempts, observed_auth_RX, early_RX, filtered_RX, malformed_RX
and the final 12 event diagnostics. Rows contain only envelope metadata; no
password, frame bodies or SAE keys. They distinguish early/filtered RX and
firmware events from an absent reply. They never count as authentication.
crypto_result=not-run marks a stop before peer-key processing; numeric zero
alone does not prove password agreement. Check peer_confirm_verified.
P-256 prime folding replaces slow 512-bit serial reduction; 13647 independent
BigInt products and existing OpenSSL/H2E/SAE vectors pass. Pi preparation went
from 95 seconds to 3 in the captures; live peer-key timing is 2900 ms.
Latest physical Pi capture PASSES WPA3, DHCP, ARP, DNS, three replies each
from www.google.com and 1.1.1.1, and explicit disconnect with clean teardown.
The previous kernel heap phase now passes Pi repeated self-tests/counts.
For a heap regression at normal > run:
meminfo, heapinfo, heaptest, heapalloc 17, heapalloc 33, heapinfo,
heapfree 0, heapfree 0 (expected rejection), heapfree 1, heaptest, heapinfo.
Expect live heap baseline 2 pages / 3 objects / 544 capacity bytes afterward.
Use fsinfo, write/cat/rm under /tmp, heapinfo, pagetest, storageinit,
cat /boot/CONFIG.TXT, storagetest and uptime. See docs/kernel-heap.md.
RAMFS files use 16..4096-byte heap buffers; small files share pages. No disk
writes or persistent RAM files. Heap alignment=16, page cap=128 (512 KiB).
New buffers are zeroed, freed buffers wiped; empty pages return to PMM.
Heap/MMU/IRQ/USB and earlier Wi-Fi baselines pass. New task contexts need a Pi test.
For a network regression, wificonnect <SSID> runs verified SAE/M4/keys/port/DHCP/address
checks/gateway pings, then keeps a foreground net> console OPEN.
Fresh boot: meminfo, memmap, pagetest, nettest, keytest, storageinit, wififiles,
wifiinit, wificores, wifiload, wifistart, wificontrol, wificaps, wifiprepare IN,
wifiscan, wifitarget Jack's, wificonnect Jack's.
Run separately. Skip wifiping/wifidhcp/wifiassoc and other join probes this boot.
Password is accepted only at the hidden prompt and is not saved or embedded.
At net>: netinfo, nslookup www.google.com, ping www.google.com, ping 1.1.1.1,
netinfo, disconnect. Wait for each DNS/Ping complete result before another query.
DNS uses the DHCP server, A/IN over UDP, fresh random ID/source port, validated
question/endpoints/checksums and bounded compressed CNAME answers. No cache.
A DNS error/timeout does not start ping and leaves Wi-Fi open. A ping timeout leaves Wi-Fi
open. Leave idle one minute, check the phone still lists the Pi, then ping again.
Ctrl+C also requests disconnect. Only network commands run inside net>.
After disconnect, at normal >: netinfo, wifiinfo, storagetest, pagetest, uptime.
Reboot before joining again. One-shot initialization/install journals remain.
Initial DHCP and address/gateway checks retain their 45 s/30 s plus I/O limits.
The open session ends on explicit disconnect, lease expiry, one-hour/360000-poll
limit, unsupported rekey, lost peer/authorization, conflict or I/O/RNG failure.
No renewal, automatic reconnect, DNSSEC, TCP, sockets or package installation yet.
No DNS TCP fallback, AAAA, EDNS or incomplete-CNAME follow-up. Other on-link
neighbors are not implemented. DNS/hostname ping pass in the prior Pi image.
Each user ping uses fresh random ID/token, exact endpoint/payload/checksum
matching and three requests. RX/ARP/M3 continue while input is polled. Peer
identity/security/SCB flags are inspected every 15 s and on netinfo.
Every exit removes IP, attempts DECLINE on conflict, deauthorizes, DOWN/deletes
keys/clears RSN/restores policy and erases private keys. No key reinstallation.
RX has bounded per-operation budgets in the console; setup retains its cap.
ARP replies: one queued, at most 64/minute. Existing M3/M4/credit limits remain.
The 1 MiB page bitmap now occupies an explicit reserved RAM arena instead of
kernel BSS. meminfo/memmap show it; managed RAM may be 1 MiB lower. Physical
address limit stays 32 GiB. Native tests and the Pi bitmap/page test pass.
Firmware entry, DTB placement and Image-size checks are unchanged.
PuTTY: 115200 8N1, no flow control; force Local echo/Local line editing off.
Network partial input is redrawn after asynchronous DNS/ping output; continue
the displayed command prefix instead of retyping it. This console fix is new.
See docs/kernel-heap.md and docs/wifi-dns.md. Long math self-tests are optional.
rngtest manually checks the DT-gated RNG200 source and erases its samples.
No random bytes are printed, and no timer/MAC fallback is allowed.
Native crypto/RNG tests, physical RNG checks, live SAE, DHCP/DNS and ping pass.
wifitarget <SSID> checks cached RSN group/pairwise/SAE/PMF/management suites,
and retains RSNXE/H2E advertisements. It sends no packets and accepts no password.
Run wifitarget Jack's after wifiscan; no surrounding quotes. Physical preflight
now passes with CCMP-128, SAE AKM 8, required PMF and H2E advertised.
The first failing SDIO command/R5/IRQ/FIFO snapshot is retained for diagnosis.
It can take a minute or more at 400 kHz SDIO; wait for progress and the prompt.
WIFILIC.TXT retains vendor asset notices; SOURCE.TXT records the pinned source.
CLM is verified/staged on disk for later FullMAC use, not loaded by wifiload.
WPA3-Personal is the tested target; wificonnect keeps its foreground session
open until disconnect/failure/lease expiry. Standalone DHCP probes end DOWN.
No background networking, lease renewal or long-lived key management yet.
Reboot before retrying a partial CLM/country operation.
Then storagetest, storageinfo, pagetest and uptime to check continued operation.
Root dirs: /boot /dev /etc /home /mnt /proc /sys /tmp /var/log.
See docs/storage.md, docs/usb.md and docs/wifi.md for limits and test instructions.

OUROS.IMG is the kernel. PI5.DTB is the official board device tree (renamed only).
CONFIG.TXT selects those filenames. COPYING.TXT and LICENSE.TXT retain boot-asset
notices. WIFILIC.TXT retains radio firmware notices. SOURCE.TXT records provenance.
No Linux kernel or EEPROM update is here.

See docs/rpi5-boot.md and docs/disk-image.md in the source repository.
Do not infer hardware success from filesystem validation. Record a UART boot log.
'@
if ($Console -eq 'header') {
    $readme=$readme.Replace('Only the dedicated UART10 console is used.', 'Only the RP1 UART0 header console is used.')
    $readme=$readme.Replace('3-pin UART connector between the HDMI ports.', '40-pin header: Pi pin 8 TX -> adapter RXD, pin 10 RX <- adapter TXD, pin 6 GND.')
    $readme+="`r`nLeave adapter power pins disconnected; use 3.3 V signals and Pi USB-C power.`r`nRequires firmware enable_rp1_uart=1, pciex4_reset=0; board=rpi5-header.`r`nSee docs/rpi5-header-uart.md. No PCIe reinitialization is implemented.`r`n"
    $readme+="UART base 0x1c00030000 matches captured RP1_UART on board revision d04171.`r`nOther firmware mappings require a matching build. Prior header boot/IRQ/tasks/USB reads have Pi evidence; data writes need their own physical run.`r`n"
}
[IO.File]::WriteAllText((Join-Path $files 'README.TXT'),$readme,[Text.Encoding]::ASCII)
$assets=Get-Content -Raw (Join-Path $ProjectRoot 'boot/rpi5/assets.json') | ConvertFrom-Json
$source="Official DTB and notices: $($assets.repository)/tree/$($assets.revision)/boot`r`nWi-Fi vendor assets: $($WifiAssets.repository)/tree/$($WifiAssets.revision)/debian/config/brcm80211`r`nRetain WIFILIC.TXT. FW43455.BIN is the standard Cypress radio firmware, not an OS kernel.`r`nKernel: original OurOS local source, no Linux kernel linked.`r`nDTB redistribution: retain GPL notices and provide corresponding source before public distribution.`r`nOriginal OurOS source license remains undecided. This is a local test artifact.`r`n"
[IO.File]::WriteAllText((Join-Path $files 'SOURCE.TXT'),$source,[Text.Encoding]::ASCII)
$entries=@(Get-ChildItem -LiteralPath $files -File | Sort-Object Name)
if ($entries.Count -gt 14) { throw 'Root directory capacity exceeded' }

# Fixed 128 MiB disk, 1 MiB partition alignment, FAT32 with 512-byte clusters.
$sector=512; $diskSectors=262144; $start=2048; $dataSectors=2048; $dataStart=$diskSectors-$dataSectors; $total=$dataStart-$start
$reserved=32; $fatSectors=2048; $clusters=$total-$reserved-2*$fatSectors
if ($clusters -lt 65525 -or ($clusters+2)*4 -gt $fatSectors*$sector) { throw 'Invalid FAT32 geometry' }
$dataLba=$start+$reserved+2*$fatSectors
$image=Join-Path $out $piProfile.Image
$temporary=Join-Path $work 'disk.img'
function Put16([byte[]]$Buffer,[int]$Offset,[uint16]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer,$Offset) }
function Put32([byte[]]$Buffer,[int]$Offset,[uint32]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer,$Offset) }
function PutText([byte[]]$Buffer,[int]$Offset,[string]$Value) { [Text.Encoding]::ASCII.GetBytes($Value).CopyTo($Buffer,$Offset) }
$stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $stream.SetLength([long]$diskSectors*$sector)
    function WriteAt([long]$Offset,[byte[]]$Bytes) {
        $stream.Position=$Offset
        $stream.Write($Bytes,0,$Bytes.Length)
    }
    $mbr=New-Object byte[] 512
    Put32 $mbr 440 0x4f55524f
    $mbr[446]=0x80; $mbr[447]=0x20; $mbr[448]=0x21; $mbr[449]=0
    $mbr[450]=0x0c; $mbr[451]=0xfe; $mbr[452]=0xff; $mbr[453]=0xff
    Put32 $mbr 454 $start; Put32 $mbr 458 $total
    $mbr[466]=0xda; $mbr[467]=0xfe; $mbr[468]=0xff; $mbr[469]=0xff
    Put32 $mbr 470 $dataStart; Put32 $mbr 474 $dataSectors
    $mbr[510]=0x55; $mbr[511]=0xaa
    WriteAt 0 $mbr
    $boot=New-Object byte[] 512
    $boot[0]=0xeb; $boot[1]=0x58; $boot[2]=0x90
    PutText $boot 3 'OUR0S   '
    Put16 $boot 11 512; $boot[13]=1; Put16 $boot 14 $reserved; $boot[16]=2
    $boot[21]=0xf8; Put16 $boot 24 63; Put16 $boot 26 255
    Put32 $boot 28 $start; Put32 $boot 32 $total; Put32 $boot 36 $fatSectors
    Put32 $boot 44 2; Put16 $boot 48 1; Put16 $boot 50 6
    $boot[64]=0x80; $boot[66]=0x29; Put32 $boot 67 0x4f555235
    PutText $boot 71 'OUROSBOOT  '; PutText $boot 82 'FAT32   '
    $boot[90]=0xeb; $boot[91]=0xfe # x86 loop, irrelevant to Pi firmware.
    $boot[510]=0x55; $boot[511]=0xaa
    WriteAt ($start*$sector) $boot
    WriteAt (($start+6)*$sector) $boot
    $fat=New-Object byte[] ($fatSectors*$sector)
    Put32 $fat 0 0x0ffffff8; Put32 $fat 4 0x0fffffff; Put32 $fat 8 0x0fffffff
    $directory=New-Object byte[] 512
    PutText $directory 0 'OUROSBOOT  '; $directory[11]=8
    $nextCluster=3; $slot=1
    $records=@()
    foreach ($entry in $entries) {
        if ($entry.Name -cnotmatch '^[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}$' -or $entry.Length -eq 0) { throw "Unsupported file: $($entry.Name)" }
        $contents=[IO.File]::ReadAllBytes($entry.FullName)
        $count=[int][Math]::Ceiling($contents.Length/512.0)
        if ($nextCluster+$count -gt $clusters+2) { throw 'Image full' }
        for ($i=0;$i -lt $count;$i++) {
            $value=if ($i -eq $count-1) { 0x0fffffff } else { $nextCluster+$i+1 }
            Put32 $fat (($nextCluster+$i)*4) $value
        }
        WriteAt (([long]$dataLba+$nextCluster-2)*$sector) $contents
        $offset=$slot*32; $parts=$entry.Name.Split('.')
        PutText $directory $offset ($parts[0].PadRight(8)+$parts[1].PadRight(3))
        $directory[$offset+11]=0x20
        # Deterministic FAT date: 2026-11-08, 00:00. No host-time dependency.
        Put16 $directory ($offset+16) 23912; Put16 $directory ($offset+18) 23912
        Put16 $directory ($offset+24) 23912
        Put16 $directory ($offset+20) ([uint16]($nextCluster -shr 16))
        Put16 $directory ($offset+26) ([uint16]($nextCluster -band 65535))
        Put32 $directory ($offset+28) $contents.Length
        $records+=[ordered]@{name=$entry.Name;bytes=$contents.Length;sha256=(Get-FileHash $entry.FullName -Algorithm SHA256).Hash}
        $nextCluster+=$count; $slot++
    }
    WriteAt ([long]$dataLba*$sector) $directory
    WriteAt (($start+$reserved)*$sector) $fat
    WriteAt (($start+$reserved+$fatSectors)*$sector) $fat
    $fsinfo=New-Object byte[] 512
    Put32 $fsinfo 0 0x41615252; Put32 $fsinfo 484 0x61417272
    Put32 $fsinfo 488 ($clusters-($nextCluster-2)); Put32 $fsinfo 492 $nextCluster
    $fsinfo[510]=0x55; $fsinfo[511]=0xaa
    WriteAt (($start+1)*$sector) $fsinfo
    WriteAt (($start+7)*$sector) $fsinfo
    $extra=New-Object byte[] 512; $extra[510]=0x55; $extra[511]=0xaa
    WriteAt (($start+2)*$sector) $extra; WriteAt (($start+8)*$sector) $extra
    . "$PSScriptRoot/data-format.ps1"
    $dataPartition=New-OurOSDataPartition
    WriteAt ([long]$dataStart*$sector) $dataPartition
    $dataHasher=[Security.Cryptography.SHA256]::Create()
    try { $dataDigest=([BitConverter]::ToString($dataHasher.ComputeHash($dataPartition))).Replace('-','').ToLowerInvariant() } finally { $dataHasher.Dispose() }
    $stream.Flush()
} finally { $stream.Dispose() }
$manifest=[ordered]@{board='Raspberry Pi 5 Model B';status='EXPERIMENTAL-HARDWARE-UNTESTED';
    bytes=134217728;partition_lba=$start;partition_sectors=$total;fat='FAT32';
    asset_revision=$assets.revision;wifi_revision=$WifiAssets.revision;console=$Console;files=$records;
    data_format=1;data_lba=$dataStart;data_sectors=$dataSectors;data_sha256=$dataDigest}
$manifestPath=Join-Path $work 'image-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding ASCII $manifestPath
& "$PSScriptRoot/verify-rpi5-image.ps1" -Image $temporary -Manifest $manifestPath
# Promote only a verified artifact. Output paths are fixed project-local files.
if (Test-Path -LiteralPath $image) { & "$PSScriptRoot/archive-images.ps1" -Image $image }
Copy-Item -LiteralPath $temporary -Destination $image -Force
Copy-Item -LiteralPath $manifestPath -Destination "$image.json" -Force
$digest=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $([IO.Path]::GetFileName($image))" | Set-Content -Encoding ASCII "$image.sha256"
& "$PSScriptRoot/archive-images.ps1" -Image $image
& "$PSScriptRoot/compress-image.ps1" -Image $image
& "$PSScriptRoot/clean-build.ps1" -IncludeTestImages
Write-Output "IMAGE: $image"
Write-Output "SHA256: $digest"
Write-Output 'Experimental Pi 5 image. Filesystem verified; hardware boot UNTESTED. No physical device written.'
