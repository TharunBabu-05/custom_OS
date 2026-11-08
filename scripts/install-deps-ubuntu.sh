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
    build-essential git python3 ca-certificates \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    qemu-system-arm gdb-multiarch \
    device-tree-compiler dosfstools mtools parted file
