# Shell expansion and persistent session profiles

The [new Pi excerpt](../proofs/pi5-session-profile-2026-11-15.txt) now verifies
expansion, profile save and automatic loading in a new shell session. It does
not show a power cycle. Development continues with
[private dynamic application pages](user-memory.md). The delivery figures below
describe the archived profile-stage image.

The [new Pi capture](../proofs/pi5-environment-exec-2026-11-15.txt) verifies
setenv/getenv, a value retained across exec, exit code 0, execs=1 and resource
restoration. The two rejected commands were missing their closing quotes.
The earlier completed hardware tests are accepted. This phase adds expansion
and saved settings; its new physical acceptance is pending.

## Expand variables in commands

At the `user:/$` shell prompt:

```text
setenv MESSAGE "hello from OurOS"
echo "$MESSAGE"
write /tmp/note.txt "${MESSAGE}"
cat /tmp/note.txt
echo '$MESSAGE'
```

The first echo and cat print `hello from OurOS`. The last echo prints literal
`$MESSAGE`. `$NAME` and `${NAME}` expand outside single quotes. A backslash
escapes the next input character, so `echo \$MESSAGE` is literal too. An unset
variable expands to an empty value. Expanded spaces stay within the same
argument, even outside quotes; no field splitting, globbing or recursive
expansion is implemented. Values containing `$NAME`, quotes or semicolons stay
literal data inside that argument. Quotes must still be closed in the input.

`$?` (also `${?}`) reads the preceding shell status:

```text
no-such-program
echo "result=$?"
```

This prints `result=127`. Ordinary operation failures set 1; malformed quoting,
expansion or pipeline syntax sets 2. Successful commands set 0. Child and
pipeline commands keep their actual bounded exit status; `status` reads it
without changing it. Expansion occurs before resetting the status for the next
command. Missing closing braces, invalid variable identifiers, an overlong
name or too much expanded data refuses the entire command before execution.

The input line still holds at most 127 bytes, argv at most 8 words and 512 total
bytes including terminators. Each expanded value uses the existing 127-byte
environment limit. The monitor's `appexec` parser keeps its original literal
behavior; expansion is performed by the EL0 shell. This is an original bounded
shell syntax, without command substitution, arithmetic, pipelines using `|`,
general shell scripts, tilde expansion or POSIX compatibility.

## Save and load settings

At the shell, after `/data` is mounted:

```text
setenv MESSAGE "hello after reboot"
setenv HOME /etc
profile save
exit
```

The default file is `/data/session.env`. Starting `appshell` again automatically
loads it if present. Then:

```text
echo "$MESSAGE"
cd
pwd
```

Expect `hello after reboot` and `/etc`. The saved file also survives a normal
reboot when the data device honors its existing flush/write-through contract.
After a reboot, run the established storage/MMU/timer setup before starting
the shell. A full-image flash replaces `/data`; a profile does not preserve
itself across reflashing. Use the [export/restore workflow](data-backup-restore.md)
when retaining files across a full-image update.

`profile save [path]` snapshots all current variables, including PATH/HOME/TERM
if still defined, and deliberately replaces an existing closed regular file.
Saving is restricted to `/data`; its parent must exist. The entire file uses
one existing atomic snapshot commit, followed by synchronization and complete
readback comparison. The disk format and file limits are unchanged; flat
profile files work on v1/v2 as well as v3.

`profile load [path]` accepts a regular file from any readable mounted
filesystem. It validates the whole document before replacing the environment.
It replaces the complete set, rather than merging variables; entries absent
from the file are removed. Loading performs no file writes. Open-file lease,
read-only, path, size and handle rules continue to apply. A refused load leaves
the environment untouched, including when earlier lines were valid.

Profiles are data-only settings: no command in a profile is executed and no
value is expanded during save/load. A saved value `$MESSAGE` stays that literal
value. Settings remain per-process: loading in a child does not change its
parent. Each shell normally loads the default profile at startup, which can
override inherited variables. Applications other than the shell do not load
profiles automatically. Cwd is not stored; HOME only controls a later bare cd.

Missing/unmounted default profiles are skipped. An existing corrupt profile is
reported and the inherited/default environment is retained. Skip automatic
loading from the monitor using:

```text
appterm 120 /boot/SHELL.ELF --noprofile
```

This also permits recovery if a valid profile deliberately leaves PATH empty.
The profile commands are shell builtins, so you can restore settings there
without external lookup:

```text
setenv PATH /data/bin:/boot
profile save
```

Or load an explicit known-good backup: `profile load /data/other.env`. Profile
save has no permanent version history: later data commits may reuse an older
bank. If an I/O error occurs after a write, the file may already be committed;
inspect storage after reboot rather than assuming an error rolled the file
back. No new format/resize/raw-write operation was added.

## Format, ABI and validation

The canonical text format is an `OUROSENV1` header followed by `NAME=value`
lines, each ending in LF. Decode also accepts CRLF. Values can contain spaces,
equals signs, quotes and dollar signs. Empty values are allowed. Duplicate or
invalid names, control bytes, embedded NUL, unsupported headers, incomplete
lines, blank body lines and more than eight entries are refused. A header-only
document represents an empty environment. Eight maximum-size variables encode
to 1290 bytes; input is bounded to 1536 bytes. Comments and executable commands
are not part of this format.

`ENV_PROFILE=36` appends to the original syscall ABI. Arguments are action
(`SAVE=1`, `LOAD=2`), path pointer and path length. Runtime wrapper:
`uos_profile(action,path)`. Results are 0 on success, EFAULT for bad ranges,
ENOENT/EBUSY/EROFS/EXDEV/EISDIR for those path/lease cases and ERROR for invalid
format, arguments or other storage failures. All bytes and paths are copied
and checked before mutation. Existing calls keep their numbers and behavior.

The shell remains within the 8192-byte stored-executable bound. Its build now
optimizes the shell and parser together, keeps a disassembly, then removes
unused ELF section-table metadata. The actual ELF/program headers, LOAD
segment, permission checks and size limit are retained. No dynamic runtime was
added. The kernel's literal argument parser remains available unchanged.

Local checks are `test-app-args.ps1`, `test-app-profile.ps1` and
`test-shell-qemu.ps1 -Profiles`. Native tests cover both raw and expanded parser
paths under 20000 mutations, exact expanded-buffer bounds, quote/escape/status
semantics and literal inserted values. Profile codec tests cover LF/CRLF,
all capacity boundaries, whole-output rollback and 15000 mutated/truncated
documents. `PROFCHK.ELF` exercises the actual profile syscalls, storage,
leases, malformed documents, CRLF, whole-environment replacement and cleanup.
It needs two free `/data` slots and refuses preexisting `.profile-check` or
`.profile-bad` scratch files. Successful completion removes its test files.
After ordinary setup, its new hardware self-test is:

```text
appterm 120 /boot/PROFCHK.ELF
```

Emulator acceptance uses regular scratch disks on A53/EL1 and A76/EL2, with
automatic/explicit/recovery loads, expansion/status errors, an installed shell
and profile readback after a fresh emulator boot. Legacy v1/v2 profiles are
also exercised without conversion. Source image and boot/unused sectors stay
untouched. Logs are `build/phase-profile-*.log`. New physical acceptance remains
pending. Full images and verified full-image ZIPs stay flat in `image_files`,
with previous versions preserved.

All native and emulator checks above passed for this delivery. The prior exec
acceptance also passed on both profiles, including PID/file-cursor continuity,
three occupied slots, original deadlines, fault containment, pipelines and
redirected binary streams. All 45 delivered ELFs, 25000 ELF parser mutations
and eight image corruption/profile checks pass. The compact shell contains no
SIMD/FP register instructions in its saved disassembly.

The full image is 134217728 bytes and its verified full-image ZIP is 877291
bytes. Image SHA-256:
`54968f0cd435452331635859622b8259a7283a8a69c0e464ce68a366a72ee5cc`.
Kernel image: 442100 bytes; reserved end `0x1cb000` below firmware DTB
`0x200000`. SHELL.ELF: 8094 bytes; PROFCHK.ELF: 3328. This delivery starts with
an empty v3 data partition and public DEMO.BAK; no user profile is preinstalled.
