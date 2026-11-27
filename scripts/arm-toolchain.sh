#!/usr/bin/env bash
# Shared by the optional Ubuntu installer and checks.
arm_release=13.3.rel1
case "$(uname -m)" in
    x86_64|aarch64) arm_host=$(uname -m) ;;
    *) printf 'Unsupported Arm GNU toolchain host: %s\n' "$(uname -m)" >&2; return 1 ;;
esac
arm_toolchain="arm-gnu-toolchain-$arm_release-$arm_host-aarch64-none-elf"
arm_dist="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/.dist"
export PATH="$arm_dist/$arm_toolchain/bin:$PATH"
arm_prefix="${CROSS_COMPILE:-aarch64-none-elf-}"
