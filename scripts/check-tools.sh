#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
missing=0
source "$(dirname -- "${BASH_SOURCE[0]}")/arm-toolchain.sh"
for tool in make git python3 "${arm_prefix}"{gcc,as,ld,objcopy,objdump,readelf,nm} qemu-system-aarch64 \
    gdb-multiarch dtc mkfs.fat mcopy parted file; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf 'FOUND %-28s %s\n' "$tool" "$(command -v "$tool")"
    else
        printf 'MISSING %s\n' "$tool" >&2
        missing=1
    fi
done
if (( missing )); then
    printf '%s\n' 'Run: bash scripts/install-deps-ubuntu.sh (inside Ubuntu WSL)' >&2
    exit 1
fi
"${arm_prefix}gcc" --version
"${arm_prefix}ld" --version
python3 --version
qemu-system-aarch64 --version
gdb-multiarch --version
dtc --version
printf '\nAvailable QEMU machines (not a claim of Pi 5 support):\n'
qemu-system-aarch64 -machine help
printf '\nAvailable CPU models:\n'
qemu-system-aarch64 -cpu help
printf '\nPASS: required development tools found. Run make check-toolchain next.\n'
