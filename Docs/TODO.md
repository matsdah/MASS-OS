# Plan de implementacion — TP2 SO

## Estado actual

Sistema multitasking preemptivo funcional. Kernel 64-bit, IDT/IRQs, drivers (video, teclado, sonido, timer), syscalls `int 0x80`, ambos memory managers (FF / Buddy), scheduler Round-Robin con prioridades, semaforos nombrados, pipes anonimos/nombrados + FDs, shell con parseo de `&` y `|` operativos.

**Comandos core implementados como procesos reales:** `mem`, `kill`, `nice`, `block`, `loop`, `sh`, `cat`, `wc`, `filter`, `mvar`.
**Senales de teclado implementadas:** `Ctrl+C` (mata foreground), `Ctrl+D` (EOF a stdin), `F1` (snapshot de registros, reemplazo de `L_CONTROL`).
**Fixes criticos aplicados:** reap de ZOMBIEs en scheduler, `process_kill` seguro desde ISR, `process_kill_foreground` mata al hijo y no a la shell.

Lo que resta es la migracion total de built-ins (`help`, `clear`, `ps`, etc.) a procesos reales para cumplir el enunciado.

---

## Pasos naturales pendientes (orden recomendado)

### 1. Comandos core simples
Implementar los comandos del enunciado que ya tienen syscalls listas. Cada uno es un proceso separado (`int64_t cmd(int argc, char *argv[])`); registrar en `Userland/c/syscall.c` -> `registry[]`, en `Userland/c/userlib.c` -> `is_child_command()`, y actualizar `help`.

- [x] `mem` — muestra estado de la memoria (`sys_mem_status`).
  - *Nota tecnica:* Implementado en `Userland/c/mem.c`. Usa `sys_mem_status` para obtener `MemStatus` y formatea los campos `total`, `free`, `used`, `used_kernel`, `alloc_count` con `num_to_str`.
- [x] `kill <pid>` — envia `sys_kill` al PID.
  - *Nota tecnica:* Implementado en `Userland/c/kill.c`. Valida `argc == 1` y convierte el argumento con `satoi` antes de llamar `sys_kill`.
- [x] `nice <pid> <prio>` — ajusta prioridad (`sys_nice`).
  - *Nota tecnica:* Implementado en `Userland/c/nice.c`. Convierte ambos argumentos y llama `sys_nice`. El kernel ya acota a `[MIN_PRIORITY, MAX_PRIORITY]`.
- [x] `block <pid>` — alterna BLOCKED/READY (`sys_block`).
  - *Nota tecnica:* Implementado en `Userland/c/block.c`. El kernel (`process_block`) ahora rechaza desbloquear procesos bloqueados internamente por syscall (`waiting_for != 0`) para no romper la sincronizacion de `waitpid`.
- [x] `loop` — imprime PID cada X tiempo usando busy waiting (caso permitido).
  - *Nota tecnica:* Reusa `endless_loop_print` de `Userland/c/test_util.c`. Se corrigio el parametro `wait` por defecto de `0` a `1000000000` para evitar flooding de pantalla.
- [x] `sh` — nueva shell interactiva.
  - *Nota tecnica:* Implementado en `Userland/c/sh.c`. Extrae `shell_run()` de `shell.c` para permitir recursion. El proceso `sh` llama a `shell_run()` y nunca retorna.

> **Nota:** `help`, `ps`, `clear`, `printTime`, `printDate`, etc. aun son built-ins. Se migraran en el paso 5.

### 2. Ctrl+C y Ctrl+D en el keyboard driver
- [x] `Ctrl+C` (scan code + Ctrl flag): identificar el proceso en foreground (`fg=1`) y enviarle `sys_kill`.
  - *Nota tecnica:* El driver (`Kernel/c/drivers/keyboardDriver.c`) rastrea `ctrl` con flags de make/break. Al detectar `ctrl + 0x2E`, llama `process_kill_foreground()`.
  - *Fix critico:* `process_kill_foreground()` ahora busca el proceso foreground con **PID mas alto** (el hijo reciente) y solo mata si hay **mas de uno**. Antes iteraba desde el indice 0 y mataba a la shell (indice 1) en lugar del hijo.
- [x] `Ctrl+D` (scan code + Ctrl flag): enviar EOF al stdin del proceso foreground.
  - *Nota tecnica:* Inserta byte `0x04` (EOT) en el buffer circular de teclado. `fd_read` en `Kernel/c/fd.c` detecta `0x04` y retorna `0` (EOF) en lugar de copiar el byte.
- [x] Snapshot de registros movido a `F1` (`0x3B`).
  - *Nota tecnica:* Antes se usaba `L_CONTROL` (`0x1D`) como tecla de snapshot, lo que colisionaba con el tracking de `Ctrl`. Se movio a `F1` para liberar `Ctrl` como modificador estandar.

### 3. Comandos de IPC basicos
Estos leen de `fd[0]` y escriben en `fd[1]`; la shell ya maneja `|` y `sys_pipe_setup`.

- [x] `cat` — lee stdin, escribe stdout (hasta EOF).
  - *Nota tecnica:* Implementado en `Userland/c/cat.c`. Loop de `sys_read(STDIN, buf, 128)`; si retorna `0` (EOF) termina, sino `sys_write(STDOUT, buf, n)`. Funciona tanto en foreground (teclado + Ctrl+D) como en pipes (`cmd1 | cat`).
- [x] `wc` — cuenta lineas del stdin; termina en EOF.
- [x] `filter` — lee stdin, filtra vocales (mayusculas y minusculas), escribe stdout.

### 4. Comando `mvar`
- [x] `mvar <escritores> <lectores>` — problema lectores/escritores sobre variable compartida usando **MVars nativas del kernel**.
  - Escritores: espera activa aleatoria, `sys_mvar_put(name, letter)` (bloqueante si FULL).
  - Lectores: espera activa aleatoria, `c = sys_mvar_take(name)` (bloqueante si EMPTY), imprime con `sys_write_color`.
  - El proceso principal crea la MVar, spawnea hijos y termina inmediatamente.
  - Cleanup al matar: `mvar_cleanup_for_process` remueve el PID de las colas de espera sin alterar el estado EMPTY/FULL.

### 5. Migrar todos los built-ins a procesos reales
El enunciado requiere que **todos** los comandos sean procesos; actualmente los tests, `np_writer`/`np_reader`, y los comandos core (`mem`, `kill`, `nice`, `block`, `loop`, `sh`, `cat`) ya lo son. Refactorizar la shell (`Userland/c/userlib.c` -> `commands[]` y `processLine`) para que:

- [ ] Ningun comando se ejecute in-process.
- [ ] `help`, `clear`, `ps`, `printTime`, `printDate`, `registers`, `testDiv0`, `invOp`, `playBeep`, `bmFPS`, `bmCPU`, `bmMEM`, `bmKEY` (+ y -) se conviertan en entry points de procesos registrados.
- [ ] La shell quede reducida a: leer linea, parsear `&` / `|`, spawnear proceso(s), `waitpid` si es foreground.
- [ ] Actualizar `help` para reflejar que comandos existen y sus parametros.

### 6. Verificacion final
- [x] Compilar con `-Wall` sin warnings.
- [ ] `test_mm`, `test_processes`, `test_sync`, `test_prio` en foreground y background -> sin errores.
- [ ] `test_sync` con semaforos debe dar resultado `0`.
- [ ] Probar pipes: `cat | wc`, `filter | cat`, etc.
- [ ] Probar `Ctrl+C` y `Ctrl+D` sobre procesos foreground.
- [x] Confirmar que no hay busy waiting fuera de `loop` y `test_sync` sin semaforos.
- [x] Eliminar binarios del repo; verificar `.gitignore`.

### 7. Documentacion
- [ ] Actualizar `README.md` con: compilacion/ejecucion, lista de comandos con parametros, uso de `&` y `|`, atajos de teclado, ejemplos y limitaciones.

---

## Changelog reciente

### Fixes criticos de scheduler / Ctrl+C (congelamiento)
**Problema:** Al presionar `Ctrl+C` sobre un proceso foreground (`loop`), la pantalla se congelaba. La shell nunca recuperaba el prompt.

**Causas y soluciones:**

1. **`process_kill_foreground` mataba a la shell en lugar del hijo.**
   - *Causa:* Iteraba `process_table` desde el indice `0` (orden: idle, shell, hijo). La shell seguia marcada `foreground = 1`, asi que la encontraba primero.
   - *Fix:* Ahora busca el proceso foreground con **PID mas alto** y solo mata si `fg_count > 1`. Archivo: `Kernel/c/process.c`.

2. **`process_kill` liberaba el stack de `current_process` desde una ISR.**
   - *Causa:* Cuando `process_kill` mataba al proceso actual (llamado desde el keyboard ISR), hacia `mm_free(stack_base)` inmediatamente. El ISR retornaba via `iretq` al proceso "muerto", que seguia ejecutando con stack liberado. Cualquier timer tick o syscall posterior escribia en memoria liberada, corrompiendo el heap.
   - *Fix:* En el branch `p == current_process`, solo marca `ZOMBIE`, libera FDs, y pone `force_switch = 1`. **No libera stack ni argv.** Archivo: `Kernel/c/process.c`.

3. **Faltaba reaping de ZOMBIEs tras un context switch.**
   - *Causa:* Los procesos ZOMBIE quedaban en la cola de listos (`scheduler_add`/`scheduler_remove`) con stack/argv sin liberar.
   - *Fix:* `scheduler_tick` y `scheduler_yield_impl` ahora, **despues** de confirmar que existe un proceso `next` al cual saltar, detectan si `cur->state == PROCESS_ZOMBIE`. Si es asi, llaman `scheduler_remove(cur)`, liberan `stack_base` y `argv`, y marcan el slot como `FREE` (`pid = 0`). Archivo: `Kernel/c/scheduler.c`.

4. **`process_waitpid` no liberaba recursos del hijo ZOMBIE.**
   - *Causa:* Cuando un padre reclamaba un hijo ZOMBIE, ponia `state = FREE` pero nunca liberaba `stack_base` ni `argv`.
   - *Fix:* Ahora libera `stack_base` y `argv` antes de poner `state = FREE`. Archivo: `Kernel/c/process.c`.

5. **`process_exit` no removia el proceso de la cola del scheduler.**
   - *Causa:* Al morir, un proceso podia quedar referenciado en `run_queue[]` aunque su estado fuera FREE/ZOMBIE.
   - *Fix:* Se agrego `scheduler_remove(current_process)` en todos los caminos de `process_exit`. Archivo: `Kernel/c/process.c`.

6. **El keyboard ISR (`_irq01Handler`) no respetaba `force_switch`.**
   - *Causa:* Tras `process_kill_foreground()`, `force_switch = 1` quedaba seteado, pero el handler del teclado hacia `popState; iretq` sin consultar el flag. El proceso muerto seguia corriendo hasta el proximo timer tick.
   - *Fix:* `_irq01Handler` ahora revisa `force_switch` antes de `popState`. Si esta en 1, llama `scheduler_yield_impl` y cambia `rsp`, igual que `_irq128Handler`. Archivo: `Kernel/asm/interrupts.asm`.

### Nuevos comandos implementados

| Comando | Archivo | Commit |
|---------|---------|--------|
| `mem` | `Userland/c/mem.c` | `55a2c38` |
| `kill` | `Userland/c/kill.c` | `23a9e74` |
| `nice` | `Userland/c/nice.c` | `2d7dda0` |
| `block` | `Userland/c/block.c` | `0b5beb0` |
| `loop` | `Userland/c/test_util.c` | `a549fd9` |
| `sh` | `Userland/c/sh.c`, `shell.c` | `3c1792c` |
| `cat` | `Userland/c/cat.c` | nuevo |

---

## Leyenda

- `[ ]` Pendiente
- `[x]` Completado
- `[-]` No aplica / Postergado
