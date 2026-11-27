#!/usr/bin/env bash
# Run inside Ubuntu WSL. Installs host tools; does not access removable storage.
set -euo pipefail
if [[ ! -r /etc/os-release ]]; then
    printf '%s\n' 'ERROR: run this script inside Ubuntu WSL.' >&2
    exit 1
fi
source /etc/os-release
if [[ "$ID" != ubuntu ]]; then
    printf '%s\n' 'ERROR: this installer is intended for Ubuntu.' >&2
    exit 1
fi
sudo apt-get update
sudo apt-get install -y \
    build-essential git python3 ca-certificates curl xz-utils \
    qemu-system-arm gdb-multiarch \
    device-tree-compiler dosfstools mtools parted file

source "$(dirname -- "${BASH_SOURCE[0]}")/arm-toolchain.sh"
if [[ ! -x "$arm_dist/$arm_toolchain/bin/aarch64-none-elf-gcc" ]]; then
    mkdir -p -- "$arm_dist"
    stage=$(mktemp -d "$arm_dist/arm-gnu.XXXXXXXX")
    trap 'rm -rf -- "$stage"' EXIT
    archive="$arm_toolchain.tar.xz"
    url="https://developer.arm.com/-/media/Files/downloads/gnu/$arm_release/binrel/$archive"
    curl --fail --location --retry 2 --output "$stage/$archive" "$url"
    curl --fail --location --retry 2 --output "$stage/checksum" "$url.sha256asc"
    digest=$(awk 'length($1)==64 && $1 !~ /[^0-9a-fA-F]/ { print $1; exit }' "$stage/checksum")
    [[ -n "$digest" ]] || { printf 'Missing toolchain SHA-256\n' >&2; exit 1; }
    (cd "$stage" && printf '%s  %s\n' "$digest" "$archive" | sha256sum -c -)
    tar -xJf "$stage/$archive" -C "$stage"
    "$stage/$arm_toolchain/bin/aarch64-none-elf-gcc" --version
    mv -T -- "$stage/$arm_toolchain" "$arm_dist/$arm_toolchain"
fi
