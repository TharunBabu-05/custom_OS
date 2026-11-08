# Optional Ubuntu WSL2 environment

**The default workflow now runs directly in Windows PowerShell.** See
[Native Windows build, run and debug](windows.md). The AArch64 compiler and QEMU
have been installed locally in `.dist/`, both CPU compiler checks pass, and the
first kernel boots in QEMU without WSL.

For the current hardware-port work, use the [Pi 5 boot candidate guide](rpi5-boot.md).
It builds in native Windows; no WSL repair is required.

`bash scripts/install-deps-ubuntu.sh` is an Ubuntu-only command. Running `bash`
in Windows PowerShell can select the broken Windows WSL launcher; it does not
install Ubuntu or turn a Windows terminal into an Ubuntu environment. Use the
PowerShell scripts in the native guide instead.

The following WSL notes remain available if you choose to repair it later.

## Observed host state

On 2026-11-08, the project directory contained only `.dist/`. Git, VS Code,
MSYS2 Make/GCC/Bash and CMake were discoverable. MSYS2's host GCC is not an
AArch64 cross-compiler. No AArch64 GCC or QEMU was found on PATH.

`wsl --status` and `wsl --list --verbose` both returned "The system cannot find
the path specified", including a retry outside the sandbox. This does not prove
that no distribution exists; WSL itself must be repaired or installed before
we can inspect distributions. Do not unregister or delete an existing distribution.

Further checks found a stopped `WslService` registered to
`C:\Program Files\WSL\wslservice.exe`, while `C:\Program Files\WSL` does not exist.
The normal `wsl --install -d Ubuntu-24.04 --no-launch` command also failed with
the same path error. Repair the missing WSL application first: Microsoft's
[offline installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install#offline-install)
link to the official [WSL MSI releases](https://github.com/microsoft/WSL/releases).
Install the MSI matching the laptop's architecture (x64 for an x64 Windows host)
as administrator, then repeat the commands below. Do not download an unofficial
WSL installer or remove existing distribution data.

This laptop reports `AMD64`. The current automation shell is not an elevated
administrator. After installing the official x64 WSL MSI, Microsoft's offline
procedure enables the required virtualization component in **Administrator
PowerShell**:

```powershell
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
```

Save your work and restart Windows when required, then install Ubuntu using the
commands below. WSL must launch normally only for this optional Ubuntu path.

## 1. Install or repair WSL

Run in **Windows PowerShell as Administrator**, not MSYS2 Bash:

```powershell
wsl --install -d Ubuntu-24.04 --no-launch
```

Save work and restart Windows if requested. Then run in **Windows PowerShell**:

```powershell
wsl --update
wsl --list --verbose
wsl -d Ubuntu-24.04
```

Complete Ubuntu's first-launch username and password prompts. If the list shows
version 1 for this distribution, run in **Windows PowerShell**:

```powershell
wsl --set-version Ubuntu-24.04 2
```

If installation stalls downloading, Microsoft's documented alternative is:

```powershell
wsl --install --web-download -d Ubuntu-24.04 --no-launch
```

If even `wsl --install` returns the same path error, use Microsoft's
[manual installation guide](https://learn.microsoft.com/en-us/windows/wsl/install-manual)
and [WSL troubleshooting](https://learn.microsoft.com/en-us/windows/wsl/troubleshooting).
Record the error before changing Windows features. BIOS virtualization may need
to be enabled. A reboot or interactive first-launch setup cannot be completed
by a compiler script. Do not reset/unregister Ubuntu as a repair shortcut.

The normal install procedure requires supported Windows 10/11 and administrative
access: [Microsoft installation instructions](https://learn.microsoft.com/en-us/windows/wsl/install).
Ubuntu 24.04 LTS is our reproducible starting distribution, not a claim that it is
the newest available Ubuntu release.

## 2. Install the toolchain

Run in **Ubuntu WSL**, after entering `wsl -d Ubuntu-24.04`:

```bash
cd /mnt/c/Users/tharu/operating_system
bash scripts/install-deps-ubuntu.sh
```

The script executes these exact package commands:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential git python3 ca-certificates \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  qemu-system-arm gdb-multiarch \
  device-tree-compiler dosfstools mtools parted file
```

One cross-compiler serves **both** confirmed targets. We use Ubuntu's packaged
`aarch64-linux-gnu-gcc` in freestanding mode and invoke the linker directly,
without Linux headers, CRT, libc, or a Linux kernel in the generated artifact.
The compiler's name does not require the resulting freestanding code to run Linux.
An `aarch64-none-elf-` GNU toolchain can later be used via `CROSS_COMPILE`.
Do not use the host's plain `gcc` for target code.

No ARM32 compiler is needed for the Zero 2 W. If an **original** Zero is added
later, its separate toolchain would be installed with `sudo apt-get install
gcc-arm-none-eabi binutils-arm-none-eabi`, using `-mcpu=arm1176jzf-s -marm
-mfloat-abi=soft` in a separate port. That port and its verification are not part
of this phase.

## 3. What each tool does

| Tool | Role and phase |
| --- | --- |
| WSL2 + Ubuntu | Linux host environment for cross-building on Windows; not the target OS |
| VS Code + WSL extension | Windows editor with compiler/debugger tasks inside Ubuntu |
| Git; optional GitHub hosting | Local history and later remote backups/review; no remote created yet |
| `build-essential` / GNU Make | Build orchestration and host C compiler for future host-side unit tests |
| `gcc-aarch64-linux-gnu` | Compile freestanding C and preprocess `.S` assembly for both CPUs |
| GNU `as`, `ld` | Assemble instructions and link sections using our linker scripts |
| `objcopy`, `objdump`, `readelf`, `nm` | Extract raw images, disassemble, inspect ELF structure and symbols |
| Python 3 | Future image packaging and verification scripts |
| `ca-certificates` | Verify HTTPS certificates for host downloads |
| `qemu-system-arm` package | Supplies `qemu-system-aarch64`; emulates ARM system hardware |
| GDB multiarch | Examine ELF symbols, registers and memory through a future QEMU debug stub |
| DTC | Compile/decompile device trees; inspect board resources |
| dosfstools | Create/check FAT filesystems in image files later |
| mtools | Copy files into FAT images without mounting them |
| parted | Inspect/construct partition tables later; never run against a device without confirmation |
| `file` | Identify generated ELF and image formats |
| PuTTY (Windows) / optional Minicom | Read the physical UART console |
| Optional CMake + Ninja | Alternative build orchestration if the project grows; Make is sufficient now |
| Optional Clang + LLD | Independent compiler/linker cross-check later |
| Optional OpenOCD | Hardware debug server only after probe, wiring, and board support are verified |
| Raspberry Pi Imager | Later explicit, user-approved SD flashing; not a kernel build tool |
| Raspberry Pi firmware / EEPROM | Vendor hardware initialization before our entry point |
| Optional U-Boot | Extra boot stage if needed; adds a board-specific port/configuration dependency |

Optional Ubuntu packages, when needed:

```bash
sudo apt-get install -y cmake ninja-build clang lld minicom openocd
```

Installing OpenOCD alone does not establish Pi 5 debug support. We will use
QEMU + GDB first, and UART for initial physical boot debugging.

## 4. Connect VS Code

VS Code is already present on this host. In **Windows PowerShell**:

```powershell
code --install-extension ms-vscode-remote.remote-wsl
```

Then in **Ubuntu WSL**:

```bash
cd /mnt/c/Users/tharu/operating_system
code .
```

Check that VS Code reports a WSL connection. This keeps the current shared
workspace intact. WSL's Linux filesystem is faster for large builds; a later move
to `~/src/operating_system` should be deliberate, with one authoritative checkout.
See [VS Code WSL documentation](https://code.visualstudio.com/docs/remote/wsl).

## 5. Verify tools and freestanding compilation

Run in **Ubuntu WSL**, at the repository root:

```bash
make check-tools
make check-toolchain
```

Useful independent inspection commands:

```bash
aarch64-linux-gnu-gcc -dumpmachine
qemu-system-aarch64 --version
qemu-system-aarch64 -machine help
qemu-system-aarch64 -cpu help
gdb-multiarch --version
aarch64-linux-gnu-readelf -h -l -d build/toolchain/rpi5/probe.elf
aarch64-linux-gnu-objdump -d build/toolchain/rpi_zero2w/probe.elf
aarch64-linux-gnu-nm -u build/toolchain/rpi5/probe.elf
```

Expected successful check output includes:

```text
PASS: rpi_zero2w: freestanding ELF64 AArch64; no unresolved symbols or dynamic runtime.
PASS: rpi5: freestanding ELF64 AArch64; no unresolved symbols or dynamic runtime.
Compiler checks only: these probe binaries are NOT bootable kernels.
```

The probes check C, AArch64 assembly, linkage, little-endian ELF64 machine type,
and absence of unresolved symbols or a dynamic interpreter. `nm -u` should print
nothing. The artifacts have no stack/boot initialization and must not be copied
to an SD card as kernels. Compilation is not execution or hardware validation.

`tests/toolchain/probe.c` checks freestanding mode and the LP64 data model, then
references an assembly function. `probe.S` assembles a system-register read.
`probe.ld` tests explicit section placement at an arbitrary inspection address.
`scripts/check-toolchain.sh` builds both CPU configurations and enforces checks.

We use `-mcpu=cortex-a53` and `-mcpu=cortex-a76` in separate outputs.
`-ffreestanding` changes C environment assumptions; `-mgeneral-regs-only` avoids
uninitialized FP/SIMD state, and `-mno-outline-atomics` avoids runtime atomic helper
dependencies. The flags do not supply startup code or a memory allocator.
See [GCC AArch64 options](https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html)
and [freestanding C options](https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html).

## Phase exit gate

The user's updated requirement permits a native Windows environment. Its
cross-compiler checks and emulator/debugger verification now satisfy the initial
development-tool gate; WSL repair is not a prerequisite. Ubuntu-specific tools
and SD packaging tools still need verification if that workflow is used later.
