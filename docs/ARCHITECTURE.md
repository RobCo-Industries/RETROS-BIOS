# Architecture

## Files

| File | Pure | Role |
|---|---|---|
| `src/main.c` | No | The boot flow, steps 1 to 8, and the marker and screen output |
| `src/post_hw.c` | No | The self test probes |
| `src/maint.c` | No | The input backends, the maintenance port commands and the idle loop |
| `src/shell.c` | No | The emergency shell: line editor, pager and the commands |
| `src/diag.c` | No | The diagnostics of `DIAG` on the hardware |
| `src/bios.h` | No | State shared by the three files above |
| `src/bootlines.c` | Yes | Every line of screen text. Lore lines are marked. |
| `src/settings.c` | Yes | The `[bios]` section of `BOOT.CFG`, through RobCo-Core's INI parser |
| `src/post.c` | Yes | The memory test and the rules of the result table |
| `src/boot.c` | Yes | Error codes, error lines and the boot fields of the boot info |
| `src/shcmd.c` | Yes | The shell's command table, tokeniser, number parsing, argument checks, help text, MEM address rules, hex dump line and pager count |
| `src/diagcalc.c` | Yes | The default `DIAG MEM` window, the memory patterns and checks, the RNG statistics, the timer comparison and the SD speed |
| `src/robco_config.h` | | Stage name, image header, slot end, version |
| `boot/` | | `config.txt` and the default `BOOT.CFG` |
| `tests/stage2/` | No | A second stage for tests only |

Pure files touch no hardware. `PURE_SRCS` in the `Makefile` lists them, and the host tests link them with RobCo-Core's `src/lib/`. The other files run only on the target. Code that can be pure goes in a pure file with a host test.

The memory test and the `DIAG MEM` patterns read through a hook in host builds, so a host test can model a stuck bit or a shorted address line. The target build has no hook.

## Memory

From RobCo-Core's [MEMORY_MAP.md](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/MEMORY_MAP.md#terminal-chain):

| Range | Use by the BIOS |
|---|---|
| `0x00008000`-`0x000FFFFF` | The BIOS image, BSS and stacks. No heap. |
| `0x00100000` | The boot info, written once just before the jump |
| `0x00101000`-`0x0011FFFF` | The ATAGS or DTB copy |
| `0x00200000`-`0x003FFFFF` | The MFBootAgent slot. The loader writes it; nothing else touches it. |
| `0x00400000`-`0x0043FFFF` | The memory test window, at the start of the OS slot |
| `0x00400000`-end of ARM RAM | `DIAG MEM` writes here, in the shell only. `DIAG ALL` writes the first 16 MiB. |

The MMU and the caches stay off. The framebuffer is the GPU's, at the address the mailbox gives.
