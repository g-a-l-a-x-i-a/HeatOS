# HeatOS

REMEMBER HEATOS IS FULLY MADE BY ARTIFICIAL INTELLIGENCE

HeatOS is an educational 32-bit x86 operating system focused on low-level systems programming with a modern C-first kernel codebase.

## Overview

- Boots from a floppy image in QEMU.
- Uses a protected-mode kernel with a text console shell.
- Implements real packet networking (not mocked output).
- Prioritizes clear architecture and incremental feature growth.

## Architecture

Boot flow:
1. [src/boot/boot.asm](src/boot/boot.asm): BIOS boot sector, disk load, A20, GDT, protected-mode jump.
2. [src/kernel/entry.asm](src/kernel/entry.asm): 32-bit entry shim and IDT setup.
3. [src/kernel/main.c](src/kernel/main.c): kernel init and terminal handoff.

Runtime is mostly C:
- [src/terminal/terminal.c](src/terminal/terminal.c)
- [src/drivers/network.c](src/drivers/network.c)
- [src/drivers/keyboard.c](src/drivers/keyboard.c)
- [src/drivers/vga.c](src/drivers/vga.c)
- [src/lib/ramdisk.c](src/lib/ramdisk.c)
- [src/lib/string.c](src/lib/string.c)

Only required bootstrap assembly remains in active use.

## Networking

HeatOS networking currently includes:
- PCI probing and init for QEMU `ne2k_pci`
- Ethernet RX/TX path
- ARP request/reply handling
- IPv4 parsing
- ICMP echo request/reply (`ping`)
- UDP transport support for DNS queries
- DNS A-record lookup for domain ping

This means `ping` supports both:
- IPv4 targets (for example `1.1.1.1`)
- Domain targets (for example `cloudflare.com`)

## Terminal

The built-in shell includes commands for:
- System inspection (`status`, `mem`, `uptime`, `arch`)
- Networking (`net`, `ping <ipv4|domain>`)
- Filesystem operations (`ls`, `cd`, `pwd`, `mkdir`)
- Session utilities (`help`, `clear`, `history`, `repeat`, `reboot`, `halt`)

`ping` output uses a custom HeatOS net-probe style with:
- Target and resolved address
- Per-attempt pass/fail lines
- Packet/loss summary
- Through status (yes/no)
- Final pass/partial/fail result

## Requirements (Windows)

1. NASM
2. LLVM/Clang toolchain (`clang`, `ld.lld`, `llvm-objcopy`)
3. QEMU

Install with winget:

```powershell
winget install --id NASM.NASM -e --accept-package-agreements --accept-source-agreements
winget install --id LLVM.LLVM -e --accept-package-agreements --accept-source-agreements
winget install --id SoftwareFreedomConservancy.QEMU -e --accept-package-agreements --accept-source-agreements
```

## Build

```bat
build.cmd
```

or

```powershell
.\scripts\build.ps1
```

Build artifacts:
- `build\boot.bin`
- `build\kernel.bin`
- `build\Heatos.img`

## Run

```bat
run.cmd
```

or

```powershell
.\scripts\run.ps1
```

Default run path enables networking with:
- `-nic user,model=ne2k_pci`

## Quick Network Checks

In the HeatOS terminal:
1. `net`
2. `ping 10.0.2.2`
3. `ping 1.1.1.1`
4. `ping cloudflare.com`

## Project Direction

Current direction is practical kernel growth in C while keeping assembly minimal and limited to unavoidable bootstrapping responsibilities.
