# User utilities, arguments and sleeping processes

The [2026-11-13 Pi capture](../proofs/pi5-el0-applications-2026-11-13.txt)
passes the previous ELF/EL0 stage: normal programs, invalid syscall buffers,
kernel/RX/NX protection faults, timer preemption, timeout containment, cleanup,
and installation/execution from `/data`. No second boot is present, so it does
not yet establish physical power-cycle persistence.

This stage adds useful **EL0** programs on that foundation. The existing `ls`,
`cat`, `pwd` monitor commands remain available. `appexec` runs the new executable
versions with checked system calls and private process memory.

## Try on the Pi

Use the new complete image from `image_files` and the usual header serial
console. Reflashing replaces `/data`; preserve any files you want before that.
Run one command per prompt:

```text
storageinit
datainit
mmuinit
irqinit
timerstart
apptest
tooltest
appexec /boot/ECHO.ELF "hello from user space"
appexec /boot/LS.ELF /boot
cd /etc
appexec /boot/PWD.ELF
appexec /boot/CAT.ELF hostname
appexec /boot/LS.ELF .
pwd
cd /
appexec /boot/SLEEP.ELF 100
appinfo
fileinfo
heaptest
pagetest
timerstop
```

Expected: both test commands end in PASS, `cleanup=restored`, zero remaining
handles, memory checks pass. PWD prints `/etc`; CAT prints `ouros`; LS lists
real VFS entries; SLEEP returns after at least 100 milliseconds. `tooltest`
includes a deliberately endless sleeper which ends with `state=timeout
code=124`. The previous `apptest` still includes deliberate protection faults.

`tooltest` checks quoted/empty arguments, bad-pointer and size refusals, private
cwd changes, relative opens, file/directory metadata, three sleeping processes,
mixed sleep/computation, all-asleep idle/resume, sleeping-process deadline and
resource restoration. Each sleeper retains stack values, identity and an open
file across three delays; exit automatically closes that file.

For the separate physical persistence check, install `HELLO.ELF` as described
in [user-applications.md](user-applications.md), then power-cycle **without
reflashing** and run the installed `/data/hello.elf` again.

## Launch contract

`appexec <ELF path> [arguments...]` launches one foreground process. There are
at most eight words including argv[0], at most 511 input bytes, and a 512-byte
packed argument buffer including NUL terminators. Single/double quotes preserve
spaces and empty arguments; backslash escapes the next character. Unmatched
quotes, trailing escapes, excess words/bytes and an empty executable are refused
before loading. There is no variable expansion, globbing, redirection or pipeline.
The current serial monitor has the tighter limit of **127 characters for the
whole command line**, including `appexec` and spaces; overlong monitor lines
are discarded. The 511-byte limit belongs to the underlying launch parser.

`apprun <ELF> [ELF2] [ELF3]` still runs a concurrent batch of up to three
programs. Each receives its executable path as its sole argument. Working
directories are copied from the monitor at launch, then private to each process;
CHDIR never changes the monitor's cwd. Relative OPEN/STAT/READDIR use that cwd.
argv[0] preserves the supplied executable spelling for `appexec`; the loaded
path shown by `appinfo` is canonical. Processes do not yet spawn or wait for
other processes, read terminal input, or run in the background.

The tiny runtime keeps `app_main(void)`. Programs retrieve arguments through
syscalls rather than a C `main(argc, argv)` entry stack. Existing ABI calls
0..8 and old executables are unchanged.

| Added call | Contract |
| --- | --- |
| ARGC (9) | Number of arguments including executable |
| ARG (10) | index, output pointer, capacity <=512; copy NUL-terminated argument, return length excluding NUL |
| GETCWD (11) | output pointer, capacity <=512; same string return convention |
| CHDIR (12) | path pointer, byte length excluding NUL; resolve existing directory, update only caller |
| STAT (13) | path pointer, length, output record pointer; return zero on success |
| READDIR (14) | path pointer, length, index 0..1023, output record pointer; return 1 entry, 0 end, negative error |
| SLEEP_MS (15) | delay 0..1000 ms; zero yields, positive delay blocks process; return zero after wake |
| GETPID (16) | Positive identity distinct across process slots and run epochs |

Metadata records are exactly 48 bytes: uint64 bytes, uint32 type (1=file,
2=directory), uint32 reserved=0, char name[32]. STAT leaves name empty;
READDIR fills a NUL-terminated name. All unused bytes are initialized. Each
READDIR call traverses the directory afresh; indices are not stable across
concurrent filesystem changes. End/error leaves output untouched. Output
pointers require writable user mappings; input paths reject embedded NUL.
String errors also leave the destination untouched. Existing generic -1 and
invalid-address -2 errors remain; this is not a POSIX errno ABI.

## Scheduling and boundaries

READY and SLEEPING are distinct states. Scheduling checks wake times against
the architectural counter; a sleeping process resumes no earlier than its
requested delay, subject to timer granularity and other work. When all live
processes sleep, the exception return restores the kernel root/stack and exits
the exception dispatcher. The foreground scheduler unmasks IRQs and waits with
WFI. It resumes a complete saved integer frame, including SP and NZCV, when a
process becomes runnable. Kernel drivers remain synchronous and non-preemptive.

The existing two-second **whole-batch** budget includes sleeping time. The
counter is checked during user timer scheduling and kernel idle; repeated
sleep cannot evade it. This is a bounded development runner, not a long-lived
application service. Synchronous driver calls can delay reaching a scheduling
point. `appinfo` reports idle waits plus each process's sleep and wake counts.

Eight new programs are bundled (19 total): ECHO, PWD, CAT, LS, SLEEP, SLEEPER,
DOZER and TOOLCHK. CAT uses the existing <=64 KiB read-file bound and prints
through the sanitized text console; it is not a binary export tool. All ELF
files remain <=4096 bytes and can use the existing bounded local installer.

Larger/nested persistent files, recovery/export, package management, an EL0
shell, terminal input, IPC and background jobs remain work. HDMI, keyboard,
mouse, GPU and audio are also pending; the current setup is serial only.
See [remaining-os-roadmap.md](remaining-os-roadmap.md).

## Verification and artifact

Native argument tests cover boundary lengths, quotes/escapes/empty words,
rejection clearing and 20,000 deterministic mutations. ELF validation covers
all 19 binaries; private mapping permission tests are retained. Emulator
acceptance is recorded in `build/phase-tools-qemu.log` by
`scripts/test-tools-qemu.ps1`: original application tests, new utilities and
sleep states, rejected launches, install refusal paths and fresh-boot execution
on A53/EL1 and A76/EL2. It writes only regular scratch disk files, independently
decodes snapshot hashes/ELF bytes, and checks unchanged boot sectors and source.
Storage failure injection and image corruption checks remain separate.

New utilities/sleep still require the Pi run above; the previous physical
capture cannot validate newly changed code.

Full image: `image_files/OurOS-Pi5-header-experimental-sd.img` (134217728 bytes).
ZIP: `image_files/OurOS-Pi5-header-experimental-sd.zip` (832611 bytes), verified
to contain exactly the complete image. SHA256:

```text
a6d05639b3e3a57b8cfe9e1ad88e0ffe76eb3d0fd15dba87776ac77dce70abb5
```

Previous full images/ZIPs are preserved flat in `image_files`. No physical
device is flashed and no Git commit or push is performed by this build.
