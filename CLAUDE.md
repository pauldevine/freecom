# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FreeCOM is the FreeDOS command-line interpreter - a COMMAND.COM replacement that provides the DOS shell environment. It implements the command processor for the FreeDOS kernel, handling interactive command execution, batch file processing, and environment management.

This fork is being ported to the **Victor 9000/Sirius 1** vintage computer as part of the FreeDOS Victor 9000 project. For detailed Victor 9000 hardware documentation and debugging, use the `victor9000-engineer` agent.

## Build Commands

**IMPORTANT**: Use macOS `container` command, NOT Docker. The `ia16-ubuntu-2` container has both the ia16-elf-gcc cross-compiler and Open Watcom for 8086 targets. The container has the local macos `~/projects` directory mounted at `/mnt/projects`.


### Quick Build (via container)

```bash
# Build with GCC (default)
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./build.sh gcc"

# Build with Open Watcom
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./build.sh wc"

# Rebuild (clean first)
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./build.sh -r gcc"
```

### Build Options
```bash
./build.sh [-r] [clean] [no-xms-swap] [debug] [compiler] [language]
```
- `-r` - Rebuild (clean before proceeding)
- `clean` - Remove built files and exit
- `no-xms-swap` - Build without XMS-Only Swap support
- `debug` - Enable debug code (uses medium memory model)
- Compiler: `wc`/`watcom` or `gcc`
- Language: `english`, `german`, `spanish`, etc. (see `strings/*.lng`)

### Clean
```bash
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./clean.sh"
```

### CI Build (builds all languages with both compilers)
```bash
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./ci_build.sh"
```

## Architecture

### Directory Structure
- **shell/** - Core command interpreter (`command.c`, `init.c`, `batch.c`, swapping code)
- **cmd/** - Built-in command implementations (copy.c, dir.c, etc.) → compiled to `cmds.lib`
- **lib/** - Shared utility functions → compiled to `freecom.lib`
- **strings/** - Localized strings (.lng files) → compiled to `strings.dat`
- **criter/** - Critical error handler (assembly) → generates `criter`, `criter1` resources
- **suppl/** - SUPPL support library (integrated dependency)
- **include/** - Header files for the project
- **mkfiles/** - Compiler-specific makefile fragments (gcc.mak, watcom.mak, tc2.mak, etc.)
- **utils/** - Build utilities (mkinfres, ptchsize, mktools)
- **tools/** - Supplemental DOS tools built alongside FreeCOM

### Build Output
The final `command.com` is assembled from multiple components:
```
shell/command.exe + infores + criter/criter1 + criter/criter + strings/strings.dat → command.com
```

### Configuration Files
- **config.h** - Feature flags (`INCLUDE_CMD_*`, `FEATURE_*`) to enable/disable commands and features
- **config.mak** - Compiler paths, memory model settings (copied from `config.std`)
- **gnuconf.mak** - Auto-generated GNU Make compatible version of config.mak

### Memory Models
- Small model (`s`) - Default for release builds
- Medium model (`m`) - Used for debug builds
- Configured via `SHELL_MMODEL` in config.mak

### Key Abstractions
- **Context** - Shell state management defined in `criter/context.def`, generates both C (`context.h_c`) and ASM (`context.inc`) headers
- **Strings** - All user-facing messages are externalized to .lng files, processed by `strings/fixstrs` into `strings.dat`
- **SUPPL library** - Portable DOS support library providing environment handling, file functions, memory management

## Code Conventions

- 16-bit real mode DOS code targeting 8086+
- Uses NASM for assembly files (.asm)
- Far pointers used for cross-segment access (`far`, `_fmemcpy`, etc.)
- Batch file variables and environment accessed through SUPPL's environ.h API

## Victor 9000 Testing

The built `command.com` is used by the FreeDOS kernel project for Victor 9000 boot testing.

### Integration with myfreedos

```bash
# Build FreeCOM
container exec -i ia16-ubuntu-2 bash -c "cd /mnt/projects/freecom && ./build.sh gcc"

# Build Victor 9000 boot disk (copies command.com automatically)
cd ~/projects/myfreedos/boot/victor
./build_stage1_disk.sh

# Test in MAME (use mame-victor-test-2 skill from myfreedos)
cd ~/projects/myfreedos
.claude/skills/mame-victor-test-2/scripts/run_mame_test.sh --timeout 70
```

The `build_stage1_disk.sh` script automatically copies `~/projects/freecom/command.com` into the boot image.

## Victor 9000 Quick Reference

> **For complete hardware documentation**, invoke the `victor9000-engineer` agent.

### Key Differences from IBM PC

- **Memory-mapped I/O** (not port-mapped) - use `mov es:[addr]`, not `in/out`
- **No BIOS after boot** - FreeDOS must provide all INT 10h/13h/14h/16h/17h services
- **Custom CRT controller** - HD46505 at 0xE800, RAM-based fonts at 0x0C00-0x2BFF
- **896KB max RAM** - exceeds IBM's 640KB limit

### Memory Map Essentials

```text
0x00000-0x003FF   Interrupt vectors (OS-managed)
0x00C00-0x02BFF   Font RAM (DOS convention) - DO NOT overwrite!
0x03000+          Safe for kernel/shell buffers
0xF0000-0xF0FFF   Screen RAM (2K words, 80x25)
0xF8000-0xFFFFF   ROM
```

### Critical 8088 Programming Rules

1. **ES corruption**: DOS INT 21h corrupts ES - save to memory before hardware access
2. **8088 limits**: No immediate shifts >1 (use `shr al,1` 4x or `mov cl,4; shr al,cl`)
3. **Stack fragility**: Avoid push/pop in interrupt handlers, use memory variables instead

### Current Status

The kernel boots and executes COMMAND.COM. Keyboard input is not yet functional (INT 16h polling not implemented), so the shell exits immediately after loading.
