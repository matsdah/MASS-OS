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
- **Other targets:** `./compile.sh vbox` / `./run.sh vbox` (build/attach a `.vmdk` for VirtualBox); `./compile.sh usb` / `./run.sh usb` (raw `.img` to `dd` onto a pendrive). The `TARGET` arg is positional; combine with `MM` as `MM=BUDDY ./compile.sh vbox`.
- **Permission fix:** `compile.sh` already runs `chown -R` on `/root` after building, so host ownership is normally restored automatically. The manual fallback `sudo chown $USER Image/x64BareBonesImage.qcow2` is only needed if you bypassed `compile.sh`.

## Architecture

Four top-level components, each with its own Makefile, assembled into one bootable disk image by `Image/Makefile`:

- `Bootloader/` — Pure64 + BMFS (vendored). Loads packed kernel from disk.
- `Toolchain/ModulePacker` — Host-side tool that concatenates `kernel.bin` + userland modules into `packedKernel.bin`.
- `Kernel/` — Kernel proper. Linked at `0x100000` via `kernel.ld`, output as raw binary (not ELF). Entry: `loader.asm` -> `kernelMain` -> `initializeKernelBinary` -> `main` -> `scheduler_start` (never returns).
- `Userland/` — Two flat binary modules loaded by the kernel at fixed addresses:
  - `0000-sampleCodeModule.bin` -> `0x400000` (the shell + all user code)
  - `0001-sampleDataModule.bin` -> `0x500000`

Kernel heap: `0x600000`, 8 MB (`HEAP_START` / `HEAP_SIZE` in `Kernel/c/kernel/kernel.c`).

**Kernel-internal allocations** (process stacks, argv copies) must use `mm_malloc_kernel()`, which is tracked separately from user `mm_malloc()` in `MemStatus.used_kernel`.

**MVar** (multiple-reader/writer synchronization): writers block in `wq` when FULL, readers block in `rq` when EMPTY. Unblocking is **priority-based** (`wq_pop_highest` / `rq_pop_highest`) with an anti-starvation cooldown — do not revert to FIFO `wq_pop` / `rq_pop`.

## Adding a user-space command

All user code lives in the **single** `0000-sampleCodeModule.bin`. To add a new standalone command (e.g. `wc`, `filter`):

1. Implement the function with signature `int64_t cmd_name(int argc, char *argv[])` in a `Userland/c/commands/*.c` file.
2. Register it in `Userland/c/shell/syscall.c` -> `registry[]` (`name` -> `fn`).
3. Register the **name** in `Userland/c/shell/userlib.c` -> `is_child_command()` so the shell spawns it as a new process instead of running it as a built-in.
4. Update `help` text in `Userland/c/shell/help.c` if desired.

## Syscalls

The only kernel<->userland channel is `int 0x80`.  
To add a syscall:

1. Bump `CANT_SYS` in `Kernel/include/defs.h`.
2. Implement `sys_*` in the kernel.
3. Add it to the `syscalls[]` table in `Kernel/c/syscall/syscallDispatcher.c`.
4. Expose a wrapper in `Userland/asm/userlib.asm` and `Userland/c/include/syscall.h`.
5. **Critical:** Update the hardcoded `cmp rax, 44` in `Kernel/asm/interrupts.asm` (`_irq128Handler`) to match the new `CANT_SYS`. If this assembly check is out of sync, the new syscall will return `-1` (invalid) even if the table is correct.

## Testing

Run these **inside the running OS shell**. They must work as both foreground and background (`&`):

- `test_mm <max_bytes>` — infinite alloc/free loop; prints only on error. Must pass with at least one MM.
- `test_processes <max_procs>` — process creation/kill loop.
- `test_prio <target>` — priority scheduler test.
- `test_sync <n> <use_sem>` — race-condition test; result must be `0` when `use_sem=1`. (`<pairs>` and `<increments>` are hardcoded in the test.)
- `test_named_pipe` — named pipe IPC test (runs as a child process).
- `ps` — list processes.

## Critical constraints

- **Zero `-Wall` warnings.** Kernel and userland compile with `-Wall -Wextra -Werror -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wformat -Wstrict-prototypes -Wno-unused-parameter -ffreestanding -nostdlib -mno-red-zone -fno-common -fno-pie -fno-exceptions -fno-asynchronous-unwind-tables -mno-mmx -mno-sse -mno-sse2 -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -std=c99`.
- **No busy waiting** except where explicitly allowed (`loop` command, `test_sync` without semaphores).
- **No deadlocks, no race conditions.** Semaphore and MVar state/value updates are protected by a spinlock acquired via `atomic_xchg` (test-and-set) — see `Kernel/asm/libasm.asm`, `Kernel/c/semaphore/semaphore.c`, and `Kernel/c/syscall/mvar.c`.
- **No binaries in repo** — `.o`, `.bin`, `.img`, `.qcow2`, `.vmdk` are already in `.gitignore`.
- **Spanish** for code, comments, and commit messages.
- `make`, `make all`, and `make <memory_manager>` are reserved **exclusively** for compilation inside the Docker image. Other tasks (run QEMU, pull image, etc.) must use the provided scripts.

## Known deviations

- The enunciado requires **all** commands to be real processes. Some are still in-process built-ins:
  - `clear`, `ps`, `printTime`, `printDate`, `registers`, `testDiv0`, `invOp`, `playBeep`
- Shell background (`&`) and two-stage pipes (`cmd1 | cmd2`) **are** already implemented in `Userland/c/shell/userlib.c`.
- Keyboard shortcuts `Ctrl+C` (kill foreground) and `Ctrl+D` (EOF), and `+`/`-` (font size) **are** implemented.
  - `Ctrl+C` kills the **newest** foreground process (highest PID), avoiding killing the shell when it is waiting for a child.
  - `Ctrl+D` sends `0x04` (EOT) to the keyboard buffer; `fd_read` interprets it as EOF.

## Critical invariants (Ctrl+C / process kill / ZOMBIE reaping)

These bugs were fixed and their invariants **must not be regressed**:

- `process_kill` must **never** free `stack_base` or `argv` when killing the current process from interrupt context — only mark ZOMBIE + set `force_switch = 1`. (`Kernel/c/process/process.c`)
- `process_kill_foreground` must select the foreground process with the **highest PID** (newest child) and only kill if `fg_count > 1`, to avoid killing the shell itself. (`Kernel/c/process/process.c`)
- Both `scheduler_tick` and `scheduler_yield_impl` must reap ZOMBIEs **after** confirming a valid `next` — call `scheduler_remove(cur)`, free `stack_base` and `argv`, mark slot `FREE`. (`Kernel/c/scheduler/scheduler.c`)
- `process_waitpid` must free `stack_base` and `argv` before setting `child->state = PROCESS_FREE`. (`Kernel/c/process/process.c`)
- `process_exit` must call `scheduler_remove(current_process)` in all exit paths. (`Kernel/c/process/process.c`)
- `_irq01Handler` (keyboard ISR) must check `force_switch` before `popState` — same pattern as `_irq128Handler` (syscall ISR). (`Kernel/asm/interrupts.asm`)

## Critical invariants (MVar / scheduler priority)

- `mvar_put`/`mvar_take`/`mvar_destroy`/`mvar_cleanup_for_process` protect the section (state + value + queues) with `mvar_lock`/`mvar_unlock` (`atomic_xchg`). The unlock must happen **before** `process_unblock` or `cur->state = BLOCKED; force_switch = 1`: these only set state + force_switch; the context switch occurs at `iretq`. Unlocking after would leave the lock held by a descheduled process → deadlock. (`Kernel/c/syscall/mvar.c`)
- MVar uses **handoff** instead of a retry loop: when a `put` finds a reader waiting, it writes the value directly into `reader->rsp[14]` (the saved RAX slot) and unblocks the reader — the reader wakes with the value in RAX, no retry. When a `take` finds a writer waiting, it reads the current value and writes the writer's pending value (stored in `mvar_q_entry.value`) into `mv->value` — the writer wakes with 0 (saved by the ISR), no retry. This eliminates the old `-2` return code and the `sys_yield()` + retry loop in userland. (`Kernel/c/syscall/mvar.c`, `Userland/c/commands/mvar.c`)
- The pending value of a blocked writer lives in `mvar_q_entry.value` (the queue entry), **not** in the PCB. The queues `wq[]`/`rq[]` are arrays of `mvar_q_entry {pid, value}`. (`Kernel/c/syscall/mvar.c`)
- `mvar_destroy` wakes all blocked processes with `-1` written in their `rsp[14]` (MVar destroyed). (`Kernel/c/syscall/mvar.c`)
- `mvar_cleanup_for_process` must wake blocked processes after removing a killed PID — if MVar is EMPTY and `wq_count > 0`, pop a writer and do handoff (write its value, set FULL); if FULL and `rq_count > 0`, pop a reader and do handoff (write `mv->value` into `reader->rsp[14]`, set EMPTY). Without this, killing a blocked process deadlocks the MVar. (`Kernel/c/syscall/mvar.c`)
- `wq_pop_highest` / `rq_pop_highest` use a **cooldown** to prevent starvation: after `priority` consecutive serves of the highest-priority level, the lowest-priority entry gets one turn. Do not replace with pure FIFO or pure priority — both cause bugs (FIFO ignores `nice`; pure priority starves lower-priority writers). (`Kernel/c/syscall/mvar.c`)
- `scheduler_next_ready` gives foreground processes an **effective +1 priority boost** for selection. Without this, a foreground shell at priority 3 is starved when any background process has priority ≥ 3. (`Kernel/c/scheduler/scheduler.c`)

## Critical invariants (scheduler aging)

- The scheduler uses **aging** to prevent starvation of low-priority processes. `PCB.wait_ticks` accumulates in `scheduler_tick` for every READY process except the one currently RUNNING. The effective priority for selection is `priority + (foreground ? 1 : 0) + aging_bonus`, where `aging_bonus = min(wait_ticks / AGING_INTERVAL, MAX_AGING_BONUS)` (`AGING_INTERVAL=50` ticks = 500 ms, `MAX_AGING_BONUS=4`). (`Kernel/c/scheduler/scheduler.c`)
- `wait_ticks` is reset to 0 when the process is selected to run (`scheduler_tick` and `scheduler_yield_impl`), when it is unblocked (`process_unblock` and the two `waitpid`-wake paths in `process_exit`/`process_kill`), when its priority changes (`process_nice`), and at creation (`process_create`). Time spent BLOCKED does **not** count as CPU starvation.
- Aging affects **selection** only; the quantum (timeslice duration) remains `priority` (base), not effective priority. This prevents a starved process from getting an oversized timeslice once it finally runs.
- `priority` (base) is what `nice` sets and `ps` displays; it does not fluctuate with aging. The foreground +1 boost is a tie-breaker **within** the same effective level; aging is the **inter-level** anti-starvation mechanism. Both coexist by summing into `eff`.

## Critical invariants (semaphores)

- `sem_wait`/`sem_post`/`sem_cleanup_for_process`/`sem_close` protect the section (value + wait queue) with `sem_lock`/`sem_unlock` (`atomic_xchg`). The unlock must happen **before** `process_block`/`process_unblock`: these only set `state` + `force_switch`; the context switch occurs at `iretq`. Unlocking after `process_block` would leave the lock held by a descheduled process → deadlock. (`Kernel/c/semaphore/semaphore.c`)
- Per-process semaphore tenancy is tracked via `PCB.held_sems` (a `uint32_t` bitmap: bit `i` = the process holds the resource of `sem_table[i]`). Replaces the old single `sem_name[32]` + `sem_blocked` fields, which only tracked one sem and caused spurious posts. (`Kernel/include/process.h`)
- `held_sems` is set in `sem_wait` (non-blocking path, resource acquired) and in `sem_post` (on the woken waiter — resource transfer); cleared in `sem_post` (the caller) and `sem_close`. Do not set it on the blocking path of `sem_wait`.
- `sem_cleanup_for_process` iterates **all** open semaphores (a process can hold several). For each sem: if the PID was in the wait queue → remove it and `value++` (compensates the `sem_wait` decrement); if the PID held the resource → `value++` and wake a waiter with resource transfer. If **neither** → do **not** touch `value` (this was the source of the spurious post). (`Kernel/c/semaphore/semaphore.c`)
- `sem_cleanup_for_process` is called from **both** `process_kill` and `process_exit`. The `process_exit` call was added so a process that exits without `sem_post`/`sem_close` does not leak the resource. (`Kernel/c/process/process.c`)
- `atomic_xchg` is defined in `Kernel/asm/libasm.asm` and declared in `Kernel/include/lib.h`. `xchg` with a memory operand has an implicit LOCK prefix on x86.

## Repo-specific gotchas

- `compile.sh` validates that the container mounts the current directory at `/root`; if you moved the repo, recreate the container.
- `Kernel/c/syscall/syscallDispatcher.c` uses `CANT_SYS` for the `syscalls[]` array size, but the bounds check in `Kernel/asm/interrupts.asm` is a hardcoded literal (`cmp rax, 44`). They must be kept in sync.
