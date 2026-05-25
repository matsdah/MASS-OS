# CLAUDE.md

This file provides guidance to AI assistants when working with code in this repository.

## Project

MASS OS — a bare-metal x86-64 kernel (TP2 Sistemas Operativos, ITBA) that boots on QEMU/VirtualBox/real hardware via Pure64 + BMFS. Implements memory management, processes, scheduling, semaphores, pipes (IPC), and syscalls from scratch.

## Build & run

All compilation happens **inside the Docker container** `TP_SO_2` (image `agodio/itba-so-multiarch:3.1`), which provides the `x86_64-linux-gnu-gcc/ld` cross-toolchain and `nasm`. Building on the host directly will fail.

| Command | Purpose |
|---------|---------|
| `./create.sh` | One-time: pull image and create `TP_SO_2` container with cwd mounted at `/root` |
| `./compile.sh` | Build with First-Fit memory manager (default), QEMU image |
| `MM=BUDDY ./compile.sh` | Build with Buddy System memory manager |
| `./compile.sh vbox` / `./compile.sh usb` | Build VMDK / raw IMG instead of qcow2 |
| `./run.sh` | Boot QEMU on `Image/x64BareBonesImage.qcow2` (fallback to `.img`), 512MB RAM, IDE disk |
| `make clean` | Clean all subprojects |

`compile.sh` does `make clean` + `make all` inside the container, propagating `TARGET` and `MM`. The MM choice is a **compile-time switch** (`-DMM_FF` or `-DMM_BUDDY`); a single image embeds exactly one allocator. To compare allocators, rebuild and rerun.

If `./run.sh` fails with "permission denied" on the image, the container wrote it as root: `sudo chown $USER Image/x64BareBonesImage.qcow2`.

**Important constraint (from the enunciado):** `make`, `make all`, and `make <memory_manager>` rules must be reserved exclusively for compilation tasks, run inside the Docker image. Any other tasks (pulling the image, starting the container, running QEMU, etc.) must use specific rules or scripts — never the standard make targets.

### Tests

All tests **must run as standalone user processes** (not built-ins inside the shell), in both foreground and background. The kernel ships a built-in test suite invoked from inside the booted shell:

```
test_mm <max_bytes>   # infinite loop: alloc/free random blocks, checks no overlaps; prints only on error
test_proc <max_procs> # infinite loop: creates, blocks, unblocks, kills dummy procs randomly
test_prio <target>    # 3 procs incrementing vars; first equal prio, then different prios
test_sync <pairs> <increments> <use_sem>  # readers/writers race; result must be 0 with sems
test_named_pipe       # named pipe IPC test (writer/reader)
ps                    # list processes
bmFPS / bmCPU / bmMEM / bmKEY  # benchmarks
```

To test both allocators, rebuild with each `MM` value and rerun `test_mm`.

**Mandatory pass criteria:**
- `test_mm` passes (20 OK / 0 FAIL) with at least one MM.
- `test_proc` and `test_sync` pass as foreground and background processes.
- `test_prio` shows visible execution-time differences between priority levels.

## Architecture

Four top-level components, each with its own Makefile, assembled into a single bootable disk image by `Image/Makefile`:

- **Bootloader/** — Pure64 + BMFS (vendored). Loads the packed kernel from disk into memory and jumps to it. Rarely touched.
- **Toolchain/ModulePacker** — Host-side tool that concatenates `kernel.bin` + userland modules into `packedKernel.bin` for the bootloader.
- **Kernel/** — The kernel proper. Linked at `0x100000` via `kernel.ld`, output as raw `binary` (not ELF). Entry: `loader.asm` → `kernelMain` → `initializeKernelBinary` → `main` (calls `scheduler_start`, never returns).
- **Userland/** — Compiled into two modules loaded by the kernel at fixed addresses:
  - `0000-sampleCodeModule.bin` → loaded at `0x400000`, this is the **shell** (the first foreground process spawned)
  - `0001-sampleDataModule.bin` → loaded at `0x500000`

The kernel heap lives at `0x600000` and is 8 MB (see `HEAP_START`/`HEAP_SIZE` in `Kernel/c/kernel.c`).

### Boot flow

`loader.asm` → `kernelMain` (sets up GDT/segments) → `initializeKernelBinary` in `kernel.c`:
1. `loadModules` copies userland modules from end-of-kernel to their fixed load addresses.
2. `clearBSS`, `load_idt`.
3. `mm_init(HEAP_START, HEAP_SIZE)`.
4. `process_init`, `scheduler_init`, `sem_init`.
5. Creates `idle` process (PID 0, just `hlt`s) and `shell` process (foreground, entry = `0x400000`).
6. Returns; assembly then calls `main()` → `scheduler_start()` → `scheduler_start_asm` (loads first PCB's `rsp`, `popState`, `iretq` into userland).

### Memory manager

`Kernel/include/memoryManager.h` defines a stable interface; `Kernel/c/memoryManager/` has two implementations selected by the `MM` make variable:

- `memoryManagerFF.c` — First-Fit free list
- `memoryManagerBuddy.c` — Buddy system (power-of-two splits/coalesces)

Both expose `mm_init / mm_malloc / mm_malloc_kernel / mm_free / mm_status`. The `_kernel` variant excludes the allocation from `alloc_count` so `mm_status` reflects only userland-visible allocations made via `sys_malloc`.

### Processes & scheduler

- `process.c` — fixed-size PCB table (`MAX_PROCESSES = 64`), 4 KB stacks (`mm_malloc`'d), priorities 1–5, states `READY/RUNNING/BLOCKED/ZOMBIE`. PIDs, fd[2] (stdin/stdout), parent_pid, waitpid support.
- `scheduler.c` — round-robin with priority-derived quanta. `scheduler_tick` is called from the timer IRQ (irq00) in `interrupts.asm`; cooperative yield via `int 0x80` with `force_switch`. Both handlers in `interrupts.asm` save/restore full register state on the process's stack and pass `rsp` to/from C.
- The scheduler is the kernel's main loop — `scheduler_start_asm` is the only path that ever leaves the kernel into userland.

### Semaphores

`Kernel/c/semaphore.c` implements named semaphores (up to `MAX_SEMS = 32`) identified by string name. Each semaphore tracks a value and a circular wait queue of blocked PIDs. `sem_open` is reference-counted so multiple processes can share one by name. `sem_wait` calls `process_block` (which sets `force_switch`) so the current process blocks immediately; `sem_post` calls `process_unblock` to wake the next waiter.

**Atomicity requirement:** semaphore value updates must use an atomic instruction (`xchg` or `lock cmpxchg`) to avoid race conditions.

### Pipes (IPC) — IMPLEMENTED

Pipes are **unidirectional** and **blocking** (both reads and writes block when empty/full). The kernel supports:

- Anonymous pipes for related processes and **named pipes** so unrelated processes can share them by agreeing on an identifier a priori.
- A circular buffer in kernel memory with read/write ends and wait queues for readers/writers.
- **EOF semantics:** when the write end is closed, readers receive EOF.
- **Transparent I/O:** processes must not need code changes to read/write a pipe vs. the terminal. This is enforced by the file-descriptor abstraction:
  - Each PCB has `fd[2]` (stdin=0, stdout=1).
  - `sys_read` / `sys_write` must consult the current process's FD table and dispatch to terminal or pipe accordingly.

**Pipe syscalls:**
- `sys_pipe(fd_array)` — create anonymous pipe, return [read_fd, write_fd].
- `sys_pipe_open(name)` — open named pipe.
- `sys_dup2(old_fd, new_fd)` — redirect FDs (used by shell when connecting pipes).
- `sys_close(fd)` — close a file descriptor.

### Syscalls

`Kernel/include/syscallDispatcher.h` lists all syscalls (`CANT_SYS` in `defs.h`), grouped:
- 0–18: I/O, video, memory (`sys_write`, `sys_read`, `sys_malloc`, `sys_free`, `sys_mem_status`, …)
- 19–28: processes (`sys_create_process`, `sys_exit`, `sys_getpid`, `sys_ps`, `sys_kill`, `sys_nice`, `sys_block`, `sys_unblock`, `sys_yield`, `sys_waitpid`)
- 29–32: semaphores (`sys_sem_open`, `sys_sem_wait`, `sys_sem_post`, `sys_sem_close`)
- 33+: **pipes** (`sys_pipe`, `sys_pipe_open`, `sys_dup2`, `sys_close`)

Dispatched via `int 0x80` through `syscalls[]` table in `syscallDispatcher.c`. Userland calls them via `Userland/asm/userlib.asm` wrappers and `Userland/c/userlib.c` C wrappers. Adding a syscall requires: bumping `CANT_SYS`, writing the `sys_*` function, adding it to the dispatcher table, and exposing a wrapper in `userlib`.

**Important:** `sys_read` blocks when no data is available (terminal or pipe). The process is marked `BLOCKED` and a context switch is forced; it is unblocked when input arrives or the pipe state changes.

### Userland linkage

Userland code is freestanding and links against only `userlib` — no libc. Each user "program" is built as a flat binary loaded at a fixed address, with `_loader.c` calling `main`. The shell is the first real user binary; all test commands (`test_mm`, `test_proc`, `test_prio`, `test_sync`) must run as **separate processes**, not as built-ins inside the shell.

`Userland/c/include/syscall.h` exposes a `my_*` interface (`my_getpid`, `my_create_process`, `my_nice`, `my_kill`, `my_block`, `my_unblock`, `my_yield`, `my_wait`, `my_sem_*`) mirroring the cátedra's test-file API, plus `#define malloc/free` shims over `sys_malloc/sys_free`. Shell commands are registered in `userlib.c` in the `commands[]` array and dispatched by `processLine`.

### Drivers

`Kernel/c/drivers/` contains `keyboardDriver.c` (scancode→ASCII mapping, shift/caps, keyboard buffer) and `videoDriver.c` (framebuffer text rendering with font scaling). Both are compiled as part of the kernel but have no special linkage — they are just C files included by the wildcard `$(wildcard c/*.c c/drivers/*.c)` in `Kernel/Makefile`.

## Required user-space commands

All commands must be real user processes (not shell built-ins), with these exact names:

| Command | Description |
|---------|-------------|
| `sh` | Shell: supports `&` (background), `|` (pipe between 2 programs), `Ctrl+C` (kill foreground), `Ctrl+D` (send EOF) |
| `help` | Lists all commands; must include a section for cátedra-provided tests |
| `mem` | Prints memory status (total, used, free) |
| `ps` | Lists all processes: name, PID, priority, RSP, RBP, foreground flag |
| `loop` | Prints its PID with a greeting periodically; **active wait** (no blocking) |
| `kill <pid>` | Kills a process by PID |
| `nice <pid> <priority>` | Changes priority of a process |
| `block <pid>` | Toggles process between BLOCKED and READY |
| `cat` | Reads stdin and prints to stdout (works standalone or via pipe) |
| `wc` | Counts lines from stdin |
| `filter` | Reads stdin, prints filtering out vowels |
| `mvar <writers> <readers>` | Multiple readers/writers problem (MVar). Parent exits immediately after spawning children. Writers: active-wait → wait for empty var → write unique value. Readers: active-wait → wait for value → consume and print with unique color. |
| `test_mm <max_bytes>` | Memory manager test (as process) |
| `test_proc <max_procs>` | Process creation/kill test (as process) |
| `test_prio <target>` | Scheduler priority test (as process) |
| `test_sync <pairs> <increments> <use_sem>` | Semaphore/race-condition test (as process) |

### Shell (`sh`) requirements

- `cmd &` — run in background (shell does not wait, does not cede foreground).
- `cmd1 | cmd2` — connect stdout of cmd1 to stdin of cmd2 via a pipe. Only 2-stage pipes are required (no `p1 | p2 | p3`).
- `Ctrl+C` — send kill signal to the foreground process.
- `Ctrl+D` — send EOF to the foreground process.
- When running a foreground command, shell calls `waitpid` before showing the next prompt.

### `mvar` expected behavior

| Command | Action | Expected output |
|---------|--------|----------------|
| `mvar 2 2` | Let run | `ABABABABAB…` |
| `mvar 2 3` | Let run | `ABABABABAB…` |
| `mvar 3 2` | Let run | `ABCABCABC…` |
| `mvar 2 2` | Kill writer B | `ABABAAAAAA…` |
| `mvar 2 2` | Kill red reader | `ABABABABAB…` |
| `mvar 2 1` | Raise priority of writer B | `ABABABBBABBBABBB…` |

## Critical system-wide constraints

These come directly from the enunciado and failure to respect any is grounds for failing the assignment:

1. **No busy waiting** — except where explicitly required: `loop` command and `test_sync` without semaphores.
2. **No deadlocks.**
3. **No race conditions.**
4. **No binaries in the repository** — `.o`, `.bin`, `.img`, `.qcow2`, `.vmdk` must all be in `.gitignore`.
5. **Zero `-Wall` warnings** — the build must be clean.
6. **All tests run as user processes** — `test_mm`, `test_proc`, `test_sync` must run in foreground and background.
7. **The only kernel↔userland communication channel is syscalls** (`int 0x80`). No shared globals, no direct function calls across the boundary.
8. **Compile with `-Wall`**, no warnings allowed.

## README requirements (for final submission)

The `README.md` at the repo root must include:

- Compilation and execution instructions.
- Exact name and brief description of every command/test, with all parameters.
- Special characters: `&` for background, `|` for pipes.
- Keyboard shortcuts: `Ctrl+C` (kill foreground), `Ctrl+D` (EOF).
- Usage examples for each requirement.
- Missing or partially implemented requirements.
- Known limitations.
- Citations for any code fragments / AI assistance used.

## Conventions

- Code, comments, and commit messages are in **Spanish** (the repo is a course assignment at ITBA). Match this style when editing.
- Kernel C is compiled `-ffreestanding -nostdlib -mno-red-zone -fno-pie -std=c99` with strict warnings (`-Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes`). No SSE/MMX/floating-point. Don't pull in libc.
- Headers are always in `Kernel/include/` or `Userland/c/include/`; sources include them as `#include "foo.h"` (the Makefiles add `-I./include` / `-Ic/include`).
- `make clean` is invoked before every `compile.sh` run, so incremental builds across MM changes are safe.

## Memory layout (reference)

```
0x100000   kernel.bin (.text / .rodata / .data)
           <- endOfKernelBinary (linker symbol)
           # extra modules header + metadata  <- .bss overlaps here, clearBSS fixes it
0x400000   0000-sampleCodeModule.bin  (shell / userland entry)
0x500000   0001-sampleDataModule.bin
0x600000   Kernel heap (HEAP_START), 8 MB (HEAP_SIZE)
```

`loader.asm` is always the first object linked; `kernel.ld` sets `ENTRY(loader)` and `OUTPUT_FORMAT("binary")`. The `.bss` section is NOT stored in the binary — `clearBSS` zeroes it at runtime before the modules are relocated.

## Cátedra-provided test files

The official test source files live at:
```
https://github.com/alejoaquili/ITBA-72.11-SO/tree/main/kernel-development/tests/
  syscall.h        — syscall interface the tests expect (my_* wrappers)
  syscall.c        — ASM stubs for int 0x80
  test_util.h/c    — shared helpers (print, rand, etc.)
  test_mm.c        — memory manager test
  test_prio.c      — priority scheduler test
  test_processes.c — process lifecycle test
  test_sync.c      — semaphore/race condition test
```

These are the canonical versions. The copies in `Userland/c/` must stay compatible with the `my_*` API defined in `syscall.h`. Any syscall the tests call must exist in `Userland/c/include/syscall.h` with the same signature.

## Examples from the cátedra

The repo `https://github.com/alejoaquili/ITBA-72.11-SO/tree/main/examples/` contains reference implementations:

| Example | Relevance |
|---------|-----------|
| `simple-shell/` | Minimal Linux shell in C — useful reference for pipe/fork/exec/wait patterns to adapt |
| `producer-consumer/` | Blocking producer-consumer — reference for pipe + semaphore synchronization |
| `sync/` | Mutex/semaphore patterns |
| `orphan/` | Orphan process behavior |
| `zombie/` | Zombie process behavior |
| `make-variable/` | How to pass variables to sub-makes (relevant for `MM=BUDDY`) |

## Debugging with GDB + QEMU

The container image includes `gdb`. To debug the kernel:

### 1. Enable debug info at compile time

Link twice: once as `binary` (for the image) and once as `elf64-x86-64` (for GDB symbols):

```makefile
# In Kernel/Makefile — add a second link rule:
$(LD) $(LDFLAGS) -T kernel.ld --oformat=elf64-x86-64 -o kernel.elf $(OBJECTS)
# Same for Userland:
$(GCC) ... -T sampleCodeModule.ld -Wl,--oformat=elf64-x86-64 -o sampleCodeModule.elf
```

### 2. Run QEMU in debug mode

```bash
./run.sh gdb   # launches: qemu-system-x86_64 -s -S -hda Image/... -m 512
# -S freezes CPU at startup; -s opens gdbserver on TCP:1234
```

### 3. Connect GDB (from inside Docker)

```bash
gdb
(gdb) target remote host.docker.internal:1234
(gdb) add-symbol-file Kernel/kernel.elf 0x100000
(gdb) add-symbol-file Userland/sampleCodeModule.elf 0x400000
```

Put these 3 lines in `~/.gdbinit` inside the container so they run automatically.

### 4. Monitor interrupts

```bash
./run.sh gdb 2>&1 | grep "v="   # prints each interrupt vector + IP/SP
```

### 5. gdb-dashboard (optional, highly recommended)

```bash
# Inside the container:
wget -P ~ https://git.io/.gdbinit   # installs gdb-dashboard
```

Define profiles in `~/.gdbinit`:
```
define src-prof
    dashboard -layout source expressions stack variables
    dashboard source -style height 20
end
define asm-prof
    dashboard -layout registers assembly memory stack
end
```

### Known GDB issue: "packet too long"

```
(gdb) b main
(gdb) c
(gdb) disconnect
(gdb) set arch i386:x86-64
(gdb) target remote localhost:1234
```

## Static code analysis

The cátedra recommends running these tools inside the Docker container before submission:

```bash
# cppcheck (already in the container)
cppcheck --enable=all --inconclusive -I Kernel/include Kernel/c/

# PVS-Studio (requires license activation — see static-code-analysis/pvs-studio.md)
```

Key rule: **absence of warnings ≠ absence of bugs** (false negatives), and **warnings ≠ bugs** (false positives). Use as a supplement to manual review, not a replacement.

## Docker quick-reference

```bash
# Useful aliases to add to host shell profile:
alias dcrun="docker run -v ${PWD}:/root --privileged -ti --name TP_SO_2 agodio/itba-so-multiarch:3.1"
alias dcexec="docker exec -ti TP_SO_2 bash"
alias dcstart="docker start TP_SO_2"
alias dcstop="docker stop TP_SO_2"
alias dcrm="docker rm -f TP_SO_2"

# Open a second terminal in the running container:
docker ps                         # get CONTAINER_ID
docker exec -ti <CONTAINER_ID> bash

# Add GCC color output inside the container (~/.bashrc):
export GCC_COLORS='error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'
```

**ITBA lab PCs:** use `sudo /usr/bin/start_docker.sh && sudo /usr/bin/enter_docker.sh`, then `cd /shared` before running `docker run`.

## Modularization recommendations (from cátedra)

- Keep kernel components in separate folders (already done: `drivers/`, `memoryManager/`).
- Where possible, test data structures (queues, lists, trees) **outside the kernel** using Valgrind on the host — this catches memory leaks in the MM and generic collections before they become kernel bugs.
- Unit-test individual abstractions (TADs) independently. Reference: `https://github.com/alejoaquili/c-unit-testing-example`.

## Implementation status

| Component | Status |
|-----------|--------|
| Bootloader (Pure64 + BMFS) | ✅ Done |
| Memory Manager — First-Fit | ✅ Done |
| Memory Manager — Buddy System | ✅ Done |
| Processes & PCB table | ✅ Done |
| Context switching (ASM) | ✅ Done |
| Round-Robin scheduler with priorities | ✅ Done |
| Named semaphores | ✅ Done |
| Syscalls: memory, process, semaphore | ✅ Done |
| Keyboard & video drivers | ✅ Done |
| **Pipes (IPC)** | ✅ Done |
| **FD abstraction (transparent I/O)** | ✅ Done |
| **Shell: `&` background support** | ❌ Pending |
| **Shell: `\|` pipe support** | ❌ Pending |
| **Shell: Ctrl+C / Ctrl+D** | ❌ Pending |
| **Commands: `cat`, `wc`, `filter`, `mvar`** | ❌ Pending |
| **Tests as separate processes** | ✅ Done |
