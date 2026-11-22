# Phase 4 process and shell workflows

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
