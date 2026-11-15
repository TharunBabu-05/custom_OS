# Private application environments and configurable program lookup

The Pi's [new exec/lookup capture](../proofs/pi5-exec-lookup-2026-11-15.txt)
verifies bare `hello`, the default path, and shell image replacement with ECHO,
code 0, `execs=1` and resource restoration. The earlier hardware checks are
accepted; they do not need to be repeated to develop this phase.

This phase adds a bounded environment to each EL0 process. A child receives a
private snapshot at spawn, both pipeline peers receive separate copies, and
successful `exec` preserves the calling process's environment. A failed exec
keeps it unchanged. Children can change their own variables without modifying
their parent or sibling. Process cleanup wipes the stored environment.

Fresh root processes, including every new `appshell` session, start with:

```text
PATH=/data/bin:/boot
HOME=/home/user
TERM=serial
```

These are runtime values in RAM. They are not saved to `/data`, loaded from
profile files or inherited from a previous monitor session. The mounted
filesystem and installed application files keep their existing persistence
behavior. `HOME` points into the volatile RAM root by default.

## Shell commands

After the established storage/MMU/timer setup, enter `appshell`. Then:

```text
env
setenv MESSAGE "hello from OurOS"
getenv MESSAGE
run env MESSAGE
exec env MESSAGE
```

`env` lists this shell's variables; `getenv NAME` prints one value. `setenv`
creates or changes a value, and `unsetenv NAME` removes it. Removing an absent
valid name succeeds. The external `/boot/ENV.ELF [name]` prints a child process's
environment or one value. `exec env MESSAGE` replaces the shell with that
application; its output proves that the value survived exec. Its exit returns
to the monitor. A later `appshell` starts with the defaults again.

`setenv NAME ""` creates an empty value; it is different from `unsetenv NAME`.
Quoted spaces remain part of the value. Shell `$NAME` substitution, `NAME=value`
prefix syntax, export flags and persistent profiles are not implemented; use
`getenv NAME` or an application calling `uos_getenv`.

Use these commands to inspect and change lookup:

```text
path
setenv PATH /boot
hello
setenv PATH /data/bin:/boot
```

`path` prints the current `PATH`. Bare names search its colon-separated
absolute directories from left to right; each directory tries the exact name,
then `.elf` when the name has no extension. Empty and relative components are
skipped, so neither `:` nor `.` implicitly searches the current directory.
Use `./name.elf` explicitly when desired. A selected regular file with invalid
ELF contents is refused, without falling back to a different candidate.
Builtins keep precedence, and names containing `/` bypass PATH.

An empty or unset PATH disables bare external-program lookup. Explicit paths
and builtins still work, including `setenv PATH /data/bin:/boot` to restore it.
The path affects bare commands, `run`, `runio`, `pipe`, `pipeio` and `exec`.
Changing the path does not create directories or install applications.

`cd` without a path now uses HOME:

```text
setenv HOME /etc
cd
pwd
```

Unset HOME or a nonexistent/non-directory value causes `cd` to fail without
changing cwd. Tilde expansion and automatic home-directory creation are not
implemented. `TERM=serial` describes the default console; changing it does not
activate display or input hardware drivers.

## Bounds and ABI

Each environment holds **8 variables**, including the defaults. Names are case
sensitive, at most 31 bytes, and match `[A-Za-z_][A-Za-z0-9_]*`. Values are at
most 127 bytes of printable ASCII, including spaces and empty strings. Control
characters, embedded NUL bytes and longer names/values are rejected. Changes
are validated before mutation, including when the environment is full.
Existing variables can still be replaced at capacity; removing a variable
frees a slot and wipes its old entry. No heap allocation or disk write occurs.

The original ABI keeps calls 0 through 31 unchanged and appends:

| Call | Arguments and result |
| --- | --- |
| `ENV_GET=32` | name pointer/length, output pointer/capacity (1..128). Returns value bytes excluding NUL; ENOENT if absent. Capacity must include the NUL. Errors leave the output unchanged. |
| `ENV_SET=33` | name pointer/length, value pointer/length. Returns 0, ENOSPC (-12) for a full table when adding, EFAULT for invalid ranges, or ERROR for invalid syntax/length. A zero-byte value needs no readable value pointer. |
| `ENV_UNSET=34` | name pointer/length. Returns 0, including for an absent valid name; invalid names/ranges fail. |
| `ENV_ENTRY=35` | index (0..7), writable 160-byte `app_env_entry`. Returns 1 for an entry, 0 at the current end without changing the output, or an error. |

An entry has `name[32]` and `value[128]`; unused bytes are zero. Results are
copies, never pointers into kernel memory. All user ranges are checked before
access or mutation. Enumeration indexes can change after a mutation; it is
not a stable open directory-style cursor. Runtime wrappers are `uos_getenv`,
`uos_setenv`, `uos_unsetenv` and `uos_env_entry`.

## Verification and delivery

`test-app-env.ps1` runs the real pure environment store against 20000
deterministic operations and an independent model, plus exact bounds,
full-table replacement/reuse, output preservation and private-copy tests.

`ENVCHK.ELF` exercises the real syscalls: invalid pointers/read-only buffers,
names/values/NULs, empty/max-length values, capacity recovery, private spawn
snapshots, parent changes after spawn, environment preservation during child
exec, failed-exec retention and separate pipeline environments after both
peers exec. It removes its own temporary variables before finishing. It is
intended to start at the monitor so it receives the known default environment:

```text
appterm 120 /boot/ENVCHK.ELF
```

`ENVRACE.ELF` runs in all three process slots to check that distinct values stay
private across yields and sleeps. `test-shell-qemu.ps1 -Environment` uses
regular scratch disks on A53/EL1 and A76/EL2 to exercise these applications,
shell commands, variable PATH/HOME, empty/unset paths, explicit paths, copied
binary streams, fresh-session reset and installed-program lookup after a
fresh emulator boot. The delivery image and boot/unused sectors stay unchanged.
Logs are `build/phase-env-*.log`. New physical environment acceptance is pending.

The environment acceptance above passed on both emulator profiles, including
fresh sessions and fresh emulator boots. All 44 delivered ELF files, 25000 ELF
parser mutations and eight image corruption/profile checks also pass. Native
environment tests pass all 20000 model operations and explicit boundary cases.
The prior exec acceptance also passes against this new kernel on both CPU
profiles, including three occupied slots, PID/handle cursors, original
deadlines, fault containment and redirected/pipeline streams.

This delivery is a 134217728-byte image with a verified 873769-byte full-image
ZIP. Image SHA-256:
`e369217400f981f109ab71b019d11ec4b00d59e89b8b5bd944dcbfb7153ab582`.
Kernel image: 438004 bytes, reserved end `0x1ca000`, below firmware DTB
`0x200000`. SHELL.ELF: 7296 bytes; ENV.ELF: 880; ENVCHK.ELF: 4168;
ENVRACE.ELF: 976. The image has an empty v3 `/data` volume and public DEMO.BAK;
Pi-created files require the established export/restore workflow before a full
image flash if they need to be retained.

Full images and verified full-image ZIPs remain flat in `image_files`, with
earlier versions preserved. Environment support does not expand the existing
three-process, foreground/direct-child or filesystem size limits. Process
trees, longer-lived jobs, dynamic user memory, packages and local hardware UI
remain later work.
