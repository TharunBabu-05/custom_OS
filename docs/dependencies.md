# Dependency choices

No third-party library is vendored or linked into the probes. Start with our own
small C utilities. Add a dependency only when its subsystem exists and the port
has a clear benefit. Pin its version, retain notices, document local changes and
test it on the relevant target before claiming support.

| Project | Purpose / possible placement | Port needed | License and alternative |
| --- | --- | --- | --- |
| Newlib | C library for future user programs; not required for kernel startup | Our syscall stubs, allocator integration, startup and reentrancy policy | Multiple per-file licenses, largely permissive; inspect [COPYING.NEWLIB](https://sourceware.org/newlib/COPYING.NEWLIB). Initially write a small libc subset ourselves. |
| musl | Future user-space libc | Substantial adaptation: musl expects the Linux syscall API, which our OS will not initially implement | MIT with retained component notices; see [upstream description](https://musl.libc.org/about.html). Prefer a minimal native ABI/libc first. |
| FatFs | FAT filesystem in the kernel's storage/VFS layer | Block-device callbacks, time, configuration and locking | BSD-style, equivalent to one-clause BSD; [port/license notes](https://elm-chan.org/fsw/ff/doc/appnote.html). A small RAM filesystem first; writing FAT ourselves is possible but increases format/testing work. |
| lwIP | Later TCP/IP stack, initially a kernel subsystem | Network device, timers, buffers and OS synchronization hooks | BSD-3-Clause; [upstream license](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/COPYING). We can implement a small educational stack, but a robust stack is a large project. |
| TinyUSB | Later USB host/device stack if the controller is supported | Controller, DMA/cache, interrupt and platform integration; support must be checked separately for each board and host/device role | MIT core; [upstream license](https://raw.githubusercontent.com/hathach/tinyusb/master/LICENSE) and [porting documentation](https://docs.tinyusb.org/en/latest/). Our own controller/class drivers are an alternative. No Pi 5 compatibility is assumed. |
| zlib | Optional compression for images or user tools; omit until needed | Allocation and I/O integration depending on use | [zlib license](https://zlib.net/zlib_license.html). Use uncompressed images first; implementing DEFLATE is unnecessary for early boot. |
| Unity | Optional host-side tests for pure C allocators/parsers; later target tests | Test runner/output; target hardware code still needs emulation or boards | MIT; [upstream](https://github.com/ThrowTheSwitch/Unity). Simple C assertions suffice initially. |
| QEMU | Host emulator for the separate `qemu_virt` board | Our kernel needs a QEMU board port; QEMU is not linked into it | GPLv2 with component-specific licenses; [license](https://www.qemu.org/docs/master/about/license.html). Physical testing complements emulation; writing an emulator is outside scope. |

Host compilers/debuggers are development tools, not kernel runtime dependencies.
Raspberry Pi vendor firmware is a separate boot dependency with its own licensing;
verify the selected release's redistribution terms before packaging it. No firmware
blobs are included now. The original project code's distribution license is not
chosen in this scaffold; select it before public release.
