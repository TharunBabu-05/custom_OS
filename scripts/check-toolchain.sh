#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
prefix="${CROSS_COMPILE:-aarch64-linux-gnu-}"
for suffix in gcc ld readelf nm objdump objcopy; do
    if ! command -v "${prefix}${suffix}" >/dev/null 2>&1; then
        printf 'ERROR: missing %s%s. Install the AArch64 cross-toolchain in Ubuntu WSL.\n' "$prefix" "$suffix" >&2
        exit 1
    fi
done
common=(-std=c11 -O2 -g -Wall -Wextra -Werror -Wstrict-prototypes
        -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie
        -fno-unwind-tables -fno-asynchronous-unwind-tables
        -mgeneral-regs-only -mstrict-align -mno-outline-atomics
        -mabi=lp64 -mlittle-endian)
mkdir -p build/toolchain
"${prefix}gcc" --version > build/toolchain/compiler.txt
"${prefix}ld" --version > build/toolchain/linker.txt
for board in rpi_zero2w rpi5; do
    case "$board" in
        rpi_zero2w) cpu=cortex-a53 ;;
        rpi5) cpu=cortex-a76 ;;
    esac
    out="build/toolchain/$board"
    mkdir -p "$out"
    printf 'Checking %s (-mcpu=%s)\n' "$board" "$cpu"
    printf '%s\n' "${common[*]} -mcpu=$cpu" > "$out/flags.txt"
    "${prefix}gcc" "${common[@]}" "-mcpu=$cpu" -c tests/toolchain/probe.c -o "$out/probe.o"
    "${prefix}gcc" "-mcpu=$cpu" -g -c tests/toolchain/probe.S -o "$out/probe-asm.o"
    # Direct ld invocation: no CRT startup, libc, libgcc, or dynamic loader.
    "${prefix}ld" --build-id=none --no-undefined -T tests/toolchain/probe.ld \
        -Map="$out/probe.map" -o "$out/probe.elf" "$out/probe.o" "$out/probe-asm.o"
    "${prefix}readelf" -h -l -d "$out/probe.elf" > "$out/readelf.txt"
    "${prefix}nm" -u "$out/probe.elf" > "$out/undefined.txt"
    "${prefix}objdump" -d "$out/probe.elf" > "$out/disassembly.txt"
    "${prefix}objcopy" -O binary "$out/probe.elf" "$out/probe.bin"
    grep -Eq 'Class:[[:space:]]+ELF64' "$out/readelf.txt"
    grep -Eq 'Data:.*little endian' "$out/readelf.txt"
    grep -Eq 'Machine:[[:space:]]+AArch64' "$out/readelf.txt"
    grep -Eq 'Type:[[:space:]]+EXEC' "$out/readelf.txt"
    if [[ -s "$out/undefined.txt" ]] || grep -Eq 'INTERP|NEEDED|DYNAMIC' "$out/readelf.txt"; then
        printf 'FAIL: %s has unresolved symbols or dynamic runtime dependencies.\n' "$board" >&2
        exit 1
    fi
    [[ -s "$out/probe.bin" ]]
    printf 'PASS: %s: freestanding ELF64 AArch64; no unresolved symbols or dynamic runtime.\n' "$board"
done
printf '%s\n' 'Compiler checks only: these probe binaries are NOT bootable kernels.'
