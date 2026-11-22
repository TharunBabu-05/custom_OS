# Phase 4 process and shell workflows

## Binary comparison and inspection

`cmp FIRST SECOND` compares the bytes in two files and reports the first
difference using a one-based byte number. Equal inputs return code 0,
a byte or length difference returns 1, and invalid arguments or I/O
errors return 2. Use `-` for one input to compare a file against a pipe.

`hexdump [-s offset] [-n length] [file|-]` prints zero-based offsets,
hexadecimal bytes, and printable ASCII in 16-byte rows. The offset
defaults to 0 and the output limit defaults to 256 bytes. Both options
accept decimal values from 0 to 65,536. Omit the file or use `-` to
read from standard input.

```text
write /tmp/bin-a alpha
write /tmp/bin-b alpha
cmp /tmp/bin-a /tmp/bin-b && echo binary-match
write /tmp/bin-b alpxa
cmp /tmp/bin-a /tmp/bin-b || echo mismatch=$?
hexdump -s 1 -n 3 /tmp/bin-a
rm /tmp/bin-a
rm /tmp/bin-b
```

Expect `binary-match`, a difference at byte 4 with `mismatch=1`,
and offset `00000001` with bytes `6c 70 68` and ASCII `lph`.

Use `build/rpi5-header/OurOS-Pi5-header-binary-tools-boot-update.zip`
with the [boot updater](boot-updates-tharun_verified.md). Against the
directory tools update, the preview should add `CMP.ELF` and `HEXDUMP.ELF`.
Both QEMU profiles passed binary comparisons and hex ranges across
read boundaries, pipe input, output redirection, and cleanup. The Pi
confirmed exit codes 0 and 1, the first mismatch at byte 4, offset
and ASCII output for the three-byte range, and cleanup.
[Pi capture](../proofs/pi5-binary-tools-2026-11-22.txt)

## Recursive search and directory usage

`find [path] [-name pattern]` lists a file or walks a directory,
including its starting path. The optional pattern matches the basename
and is case-sensitive. `*` matches any number of bytes and `?` matches
one byte. `du [path]` reports logical file bytes, file count, and directory
count for the same tree, including the starting directory. With no path,
both use the current directory.

```text
mkdir /tmp/du-demo
mkdir /tmp/du-demo/sub
write /tmp/du-demo/one.txt alpha
write /tmp/du-demo/sub/two.txt beta
find /tmp/du-demo -name '*.txt'
du /tmp/du-demo
rm /tmp/du-demo/sub/two.txt
rm /tmp/du-demo/one.txt
rmdir /tmp/du-demo/sub
rmdir /tmp/du-demo
```

Expect the two `.txt` paths and `bytes=9 files=2 directories=2`.
`du` totals file lengths. Use `df` for mount limits and capacity.
The walk shares the existing 255-byte path and 1,024-index directory
limits and caps a traversal at 4,096 entries. Run measurements while
the directory tree is stable, since enumeration uses the live namespace.

Use `build/rpi5-header/OurOS-Pi5-header-directory-tools-boot-update.zip`
with the [boot updater](boot-updates-tharun_verified.md). Against the
shell-history update, the preview should add `DU.ELF` and `FIND.ELF`.
Both QEMU profiles passed recursive searches, wildcard filters,
pipeline output, size reporting on the boot and data mounts, and
resource cleanup. The Pi found both text files and reported
`bytes=7 files=2 directories=2`, matching the five bytes of `alpha`
and two bytes of `ba` in its capture.
[Pi directory tools capture](../proofs/pi5-directory-tools-2026-11-22.txt)

## Session command history

`history` lists up to 16 previously entered command lines with
their numbers. Type `!!` to repeat the most recent entry or `!N`
to repeat the command on line number N. Enter either form on its own
line. The shell prints the recalled text, then runs it with the
current working directory and environment. Each replay gets a new
entry. Use `history -c` to clear the list.

History lives in the current shell's private memory and starts empty
in each new shell. The shell stores an entered `source` command as
one entry. Script lines, history-management commands, and comments
are excluded. Recalling a missing or expired entry returns status 1.

Try this in a fresh shell.

```text
echo first
echo second | cat
history
!!
!1
history -c
history
```

`!!` should replay the pipeline, and `!1` should print `first`.
The final command should print `History is empty.`

Use `build/rpi5-header/OurOS-Pi5-header-shell-history-boot-update.zip`
with the [boot updater](boot-updates-tharun_verified.md). Against the verified
shell-streams package, the preview should show `Replace SHELL.ELF`.
Both QEMU profiles passed replay, eviction, clearing, and a fresh
session. Each shell released the private history page on exit.
The Pi listed both entries, replayed the latest pipeline and entry 1,
and reported an empty history after clearing.
[Pi shell history capture](../proofs/pi5-shell-history-2026-11-22.txt)

## Search, count, and preview files

The shell can find these programs through its default `PATH`. Each
accepts a file path or reads from a pipeline or `runio` when the file
argument is omitted or is `-`.

| Command | Output |
| --- | --- |
| `grep LITERAL [file]` | Lines containing the case-sensitive literal. Status 0 means a match, 1 means no match, and 2 means an input or usage error. The pattern is 1–127 bytes and each line can contain up to 4,096 bytes before LF. |
| `wc [file]` | Newlines, words separated by ASCII whitespace, and bytes, in that order. An unterminated final line counts toward words and bytes. |
| `head [-n N] [file]` | The first N lines, including any unterminated last line within that limit. The default is 10. In a pipeline it drains the remaining input so the producer can finish. |

Try these from the `user:/$` prompt.

```text
runio - /tmp/text-demo ECHO "hello from OurOS"
grep OurOS /tmp/text-demo
wc /tmp/text-demo
head -n 1 /tmp/text-demo
pipe CAT /tmp/text-demo -- wc
rm /tmp/text-demo
```

The two counts should both print `1 3 17`. A pipeline can also
write its output to a new file through `pipeio`.

The text-tools update is based on the Pi-verified Phase 10 kernel.
Use the current `OUROSBOOT` drive letter in Windows PowerShell.

```powershell
$boot = 'D:\'
$zip = 'build/rpi5-header/OurOS-Pi5-header-text-tools-boot-update.zip'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/update-rpi5-boot.ps1 -BootDirectory $boot -Package $zip -Apply
```

The preview against Phase 10 adds `GREP.ELF`, `HEAD.ELF`, and `WC.ELF`.
If the preview lists a kernel replacement, pause before applying
and confirm which kernel the pendrive is using.

The Pi passed the search, preview, direct count, and piped count with
the expected output and zero exit statuses. [Pi text utility capture](../proofs/pi5-text-tools-2026-11-22.txt)

## Pipe and redirection operators

The shell supports a two-stage `LEFT | RIGHT` pipeline and file
redirection with `< INPUT` and `> NEWFILE`. A pipeline can read a
file into the first program and write the second program's output
to a new file. The shell never overwrites an existing output file.
Programs are resolved through `PATH`, including the executable
versions of `echo`, `cat`, `ls`, and `pwd`.

```text
echo "hello from OurOS" > /tmp/streams-in
cat < /tmp/streams-in | grep OurOS > /tmp/streams-out
wc < /tmp/streams-out | head -n 1 && echo streams-ok
echo "a | b < c > d"
rm /tmp/streams-in
rm /tmp/streams-out
```

The count should print `1 3 17`. Operators can touch adjacent words.
Quote or escape them to pass literal text. Variable values cannot
introduce operators. A command can have one input and one output
redirection, and each pipeline stage can have up to eight words
including the program. All commands on one entered line share the
127-byte limit. The same syntax works inside `source` scripts and
with `&&` and `||`. The existing `pipe`, `pipeio`, and `runio`
commands also work.

Use `build/rpi5-header/OurOS-Pi5-header-shell-streams-boot-update.zip`
with the updater commands above. Against the text-tools package,
the preview shows `Replace SHELL.ELF`. Both QEMU profiles passed the
operator and command-list tests, including literal operators, expanded
paths, no-overwrite behavior, and resource cleanup. The Pi passed
input and output redirection, two-stage pipelines and `&&` after
correcting the input filename and search text. It printed `1 3 17`
and `streams-ok`, then removed both temporary files.
[Pi shell streams capture](../proofs/pi5-shell-streams-2026-11-22.txt)

## Two jobs

`bg SECONDS PROGRAM` and `bgio SECONDS IN OUT PROGRAM` can start two
background jobs. `jobs` lists both PIDs and their states. `fg PID` gives
one child the terminal and waits for it. `stop PID` cancels and collects
one child. With one job, `fg` and `stop` still accept no arguments.

The shell and its children share the existing three process slots. One
background job leaves room for a foreground command. A two-stage pipeline
or `exec` requires collecting the background jobs first. Completed jobs
retain their result and slot until collected. Their original deadlines
keep running. Exiting the shell cancels and collects any remaining children,
then releases their private pages and file handles.

```text
bg 30 /boot/SPIN.ELF
bg 1 /boot/SPIN.ELF
jobs
```

Use `fg` with the second PID to collect its timeout, code 124. `jobs` can
still show the first PID as pending. Use `stop` with its PID to collect it.
Before the 30-second deadline, `stop` returns code 130.

## Command lists and scripts

| Operator | Runs the next command when |
| --- | --- |
| `;` | The previous command finishes |
| `&&` | The previous executed command succeeds |
| `\|\|` | The previous executed command fails |

The shell evaluates lists from left to right. A skipped command leaves `$?`
unchanged. Quote or escape delimiters to pass them as argument text.
The shell identifies operators before expanding variables. For example,
an expanded string that contains `&&` stays part of the argument.

```text
echo "one; two"; cat /tmp/missing || echo missing=$?
write /tmp/demo 'echo "script; text"; cat /tmp/missing || echo script-status=$?'
source /tmp/demo
```

`source PATH` reads an ASCII text file into the current shell, so `cd` and
environment updates persist. It accepts LF and CRLF, blank lines and lines
starting with `#` after optional whitespace. The limits are 4,096 bytes,
32 lines, 127 bytes per line and eight commands per line. Nested `source`
reports status 2. An invalid operator sequence or unmatched quote rejects
the entire affected line and stops the script. Executed commands on earlier
lines retain their effects. `source` returns the last executed command's
status. `exit` in a script exits the current shell.

## Build and verify

Both increments build on `7200377`. The two-job check passed separately
before the scripting increment. The combined build passed both checks on
Cortex-A53 and Cortex-A76, including terminal handoff, timeouts, parent
cleanup, quoting, status propagation and malformed script refusal. The
process-control regression and reboot regression passed on both profiles. Build
a verified v4 scratch image and set `$image` to the path printed after
`IMAGE:` to rerun the checks.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target handoff-test -KeepBuildFiles
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header -KeepBuildFiles -BuildOnly
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -Image $image -TwoJobs
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-shell-qemu.ps1 -Image $image -CommandLists
```

The Windows preview selects `OUROS.IMG`, `SHELL.ELF`, `PS.ELF` and
`SPAWNCHK.ELF` against the Phase 2 and Phase 3 boot files. It selects
`OUROS.IMG` and `SHELL.ELF` against the initial Phase 4 build. The updater
regression passed. The 9,774-byte shell runs from `/boot`. Installing a copy under
`/data` requires a v4 volume with a 16 KiB file limit. Earlier installed
shells remain usable. QEMU booted the current shell with v2 and v3 volumes,
refused an oversized install and ran a smaller app installed under `/data`.
[Windows boot update](boot-updates-tharun_verified.md)

Windows applied the initial Phase 4 build to the SanDisk on 2026-11-22. The updater
verified the four new files and kept a hash-checked rollback copy under
`backups/pi5-boot-20261122-093502-47c7cde5`.
The follow-up was also applied and verified on the same drive. Its rollback
copy is under `backups/pi5-boot-20261122-095119-f27c9dce`.

The Pi passed independent background jobs, PID-specific terminal handoff,
timeout and cancellation, command lists, `source`, and cleanup of six
children with zero open files. Hardware testing also found that `help`
exceeded the console's per-write limit. The follow-up splits that output
into two writes. Both QEMU profiles and the corrected Pi build printed the
complete help text. The Pi passed both workflows again before the successful
watchdog reboot.
[First Pi capture](../proofs/pi5-shell-workflows-2026-11-22.txt) · [Passing Pi capture](../proofs/pi5-safe-reboot-2026-11-22.txt)

| File | SHA-256 |
| --- | --- |
| `OurOS-Pi5-header-boot-update.zip` | `8ea842f9e3172521e3cf4d42dfeae9a1ec6d787ac08fc1a8d3436270a3277512` |
| `OUROS.IMG` | `d82cbda7805189b036c5c71950080dfa7c4c333e75ecd82abf346879bfb0591b` |
| `SHELL.ELF` | `327d64c3de64230c9a54e43cb8e79f237417249f290e8a87cf2d271a53500cc1` |
