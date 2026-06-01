# AGENTS.md — MASS OS

Bare-metal x86-64 kernel (ITBA SO TP2). Builds inside a Docker container; runs on QEMU/VirtualBox/real hardware via Pure64+BMFS.

## Build & run (Docker required)

- **One-time setup:** `./create.sh`  
  Creates container `TP_SO_2` from `agodio/itba-so-multiarch:3.1`.
- **Compile:** `./compile.sh`  
  Runs `make clean all` **inside** the container. Do **not** run `make` directly on the host.
- **Memory manager switch:** `MM=BUDDY ./compile.sh` (default is First-Fit).  
  Compile-time switch (`-DMM_FF` / `-DMM_BUDDY`); a single image embeds exactly one allocator. To compare both, rebuild and rerun.
- **Alias:** `make buddy` in the container is equivalent to `make MM=BUDDY all`.
- **Run:** `./run.sh`  
  Boots QEMU with `Image/x64BareBonesImage.qcow2` (fallback `.img`), 512 MB RAM.
- **Permission fix:** If `./run.sh` fails with "permission denied", the container wrote the image as root:  
  `sudo chown $USER Image/x64BareBonesImage.qcow2`

## Architecture

Four top-level components, each with its own Makefile, assembled into one bootable disk image by `Image/Makefile`:

- `Bootloader/` — Pure64 + BMFS (vendored). Loads packed kernel from disk.
- `Toolchain/ModulePacker` — Host-side tool that concatenates `kernel.bin` + userland modules into `packedKernel.bin`.
- `Kernel/` — Kernel proper. Linked at `0x100000` via `kernel.ld`, output as raw binary (not ELF). Entry: `loader.asm` -> `kernelMain` -> `initializeKernelBinary` -> `main` -> `scheduler_start` (never returns).
- `Userland/` — Two flat binary modules loaded by the kernel at fixed addresses:
  - `0000-sampleCodeModule.bin` -> `0x400000` (the shell + all user code)
  - `0001-sampleDataModule.bin` -> `0x500000`

Kernel heap: `0x600000`, 8 MB (`HEAP_START` / `HEAP_SIZE` in `Kernel/c/kernel.c`).

## Adding a user-space command

All user code lives in the **single** `0000-sampleCodeModule.bin`. To add a new standalone command (e.g. `wc`, `filter`):

1. Implement the function with signature `int64_t cmd_name(int argc, char *argv[])` in a `Userland/c/*.c` file.
2. Register it in `Userland/c/syscall.c` -> `registry[]` (`name` -> `fn`).
3. Register the **name** in `Userland/c/userlib.c` -> `is_child_command()` so the shell spawns it as a new process instead of running it as a built-in.
4. Update `help` text in `Userland/c/userlib.c` if desired.

## Syscalls

The only kernel<->userland channel is `int 0x80`.  
To add a syscall:

1. Bump `CANT_SYS` in `Kernel/include/defs.h`.
2. Implement `sys_*` in the kernel.
3. Add it to the `syscalls[]` table in `Kernel/c/syscallDispatcher.c`.
4. Expose a wrapper in `Userland/asm/userlib.asm` and `Userland/c/userlib.c` / `Userland/c/include/syscall.h`.

## Testing

Run these **inside the running OS shell**. They must work as both foreground and background (`&`):

- `test_mm <max_bytes>` — infinite alloc/free loop; prints only on error. Must pass with at least one MM.
- `test_processes <max_procs>` — process creation/kill loop.
- `test_prio <target>` — priority scheduler test.
- `test_sync <n> <use_sem>` — race-condition test; result must be `0` when `use_sem=1`. (`<pairs>` and `<increments>` are hardcoded in the test.)
- `test_named_pipe` — named pipe IPC test (currently runs in-process, not as a child).
- `ps` — list processes.

## Critical constraints

- **Zero `-Wall` warnings.** Kernel and userland compile with `-Wall -Wextra -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wformat -Wstrict-prototypes -Wno-unused-parameter -ffreestanding -nostdlib -mno-red-zone -fno-common -fno-pie -fno-exceptions -fno-asynchronous-unwind-tables -mno-mmx -mno-sse -mno-sse2 -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -std=c99`.
- **No busy waiting** except where explicitly allowed (`loop` command, `test_sync` without semaphores).
- **No deadlocks, no race conditions.** Semaphore value updates must use atomic instructions (`xchg` / `lock cmpxchg`).
- **No binaries in repo** — `.o`, `.bin`, `.img`, `.qcow2`, `.vmdk` are already in `.gitignore`.
- **Spanish** for code, comments, and commit messages.
- `make`, `make all`, and `make <memory_manager>` are reserved **exclusively** for compilation inside the Docker image. Other tasks (run QEMU, pull image, etc.) must use the provided scripts.

## Known deviations

- The enunciado requires **all** commands to be real processes. Some are still in-process built-ins:
  - `clear`, `ps`, `printTime`, `printDate`, `registers`, `testDiv0`, `invOp`, `playBeep`
- Shell background (`&`) and two-stage pipes (`cmd1 | cmd2`) **are** already implemented in `Userland/c/userlib.c`.
- Keyboard shortcuts `Ctrl+C` (kill foreground) and `Ctrl+D` (EOF), and `+`/`-` (font size) **are** implemented.
  - `Ctrl+C` kills the **newest** foreground process (highest PID), avoiding killing the shell when it is waiting for a child.
  - `Ctrl+D` sends `0x04` (EOT) to the keyboard buffer; `fd_read` interprets it as EOF.

## Technical notes: Ctrl+C fix and ZOMBIE reaping

This section documents critical kernel fixes applied to make `Ctrl+C` safe and prevent system freezes.

### Problem
Killing the foreground process from the keyboard ISR (`Ctrl+C`) caused the screen to freeze. The shell prompt never returned.

### Root causes and fixes

1. **`process_kill_foreground` killed the shell instead of the child.**
   - *Fix:* It now searches for the foreground process with the **highest PID** (the most recent child) and only kills if `fg_count > 1`. File: `Kernel/c/process.c`.

2. **`process_kill` freed the current process's stack from interrupt context.**
   - *Fix:* When `p == current_process`, it only marks the process as `ZOMBIE`, releases FDs, and sets `force_switch = 1`. It **does not** call `mm_free(stack_base)` or `mm_free(argv)`. File: `Kernel/c/process.c`.

3. **ZOMBIE reaping was missing in the scheduler.**
   - *Fix:* Both `scheduler_tick` and `scheduler_yield_impl` now check `cur->state == PROCESS_ZOMBIE` **after** finding a valid `next` process. If true, they call `scheduler_remove(cur)`, free `stack_base` and `argv`, and mark the slot as `FREE`. File: `Kernel/c/scheduler.c`.

4. **`process_waitpid` leaked memory when reclaiming a ZOMBIE child.**
   - *Fix:* It now frees `stack_base` and `argv` before setting `child->state = PROCESS_FREE`. File: `Kernel/c/process.c`.

5. **`process_exit` left the process in the scheduler queue.**
   - *Fix:* Added `scheduler_remove(current_process)` in all exit paths. File: `Kernel/c/process.c`.

6. **The keyboard ISR (`_irq01Handler`) ignored `force_switch`.**
   - *Fix:* Before `popState`, it now checks `force_switch`. If set, it calls `scheduler_yield_impl` and swaps `rsp`, exactly like the syscall handler (`_irq128Handler`). File: `Kernel/asm/interrupts.asm`.

## Repo-specific gotchas

- `compile.sh` validates that the container mounts the current directory at `/root`; if you moved the repo, recreate the container.
