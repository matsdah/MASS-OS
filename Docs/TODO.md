# Plan de implementacion - TP2 Sistemas Operativos

## Estado actual (actualizado)

Ya se implementaron: kernel 64-bit, IDT/IRQs, driver de video (framebuffer VBE), driver de teclado, driver de sonido (PIT), timer, syscalls via `int 0x80`, **memory manager (FF y Buddy)**, **procesos (PCBs, tabla, stacks, prioridades, estados)**, **context switch en ASM**, **scheduler Round-Robin con prioridades**, **semaforos nombrados**, **pipes (anonimos y con nombre) + abstraccion de FDs**, y una shell basica. El sistema ya es **multi-tasking preemptivo**. Lo que resta es la **shell con `&`/`|`/Ctrl+C/Ctrl+D**, los **comandos de userland (cat/wc/filter/mvar)** y la **verificacion final + README**.

---

## Paso 0 - Preparacion del entorno

- [x] Limpiar binarios y `.o` del repositorio.
- [x] Configurar el entorno de compilacion con la imagen Docker de la catedra: `agodio/itba-so-multiarch:3.1`.
- [x] Ajustar los Makefiles para que `make` / `make all` solo compilen. Separar reglas de ejecucion.
- [x] Agregar soporte para compilacion condicional del memory manager: `MM=FF` vs `MM=BUDDY`.
- [x] Verificar que todo compile con `-Wall` sin warnings.
- [ ] (Opcional) Configurar GDB para debugging del kernel:
  - Agregar segundo linkeo en `Kernel/Makefile` con `--oformat=elf64-x86-64` para generar `kernel.elf`.
  - Agregar segundo linkeo en `Userland/Makefile` para generar `sampleCodeModule.elf`.
  - Modificar `run.sh` para soportar `./run.sh gdb` (agrega `-s -S` a QEMU).
  - Crear `~/.gdbinit` en el container con `target remote host.docker.internal:1234` + `add-symbol-file`.
  - Ver guia completa en `CLAUDE.md` seccion "Debugging with GDB + QEMU".

---

## Paso 1 - Memory Management

**Estado: COMPLETADO**

### 1.1 Interfaz comun del memory manager
- [x] Header `memoryManager.h` con `mm_init / mm_malloc / mm_malloc_kernel / mm_free / mm_status`.

### 1.2 Memory manager elegido por el grupo (First-Fit)
- [x] Implementado en `Kernel/c/memoryManager/memoryManagerFF.c`.
- [x] Soporta `malloc` y `free`.

### 1.3 Buddy System
- [x] Implementado en `Kernel/c/memoryManager/memoryManagerBuddy.c`.
- [x] Misma interfaz que FF.

### 1.4 Compilacion condicional
- [x] `#ifdef MM_BUDDY` / `#else` para elegir entre implementaciones.
- [x] `./compile.sh` compila FF; `MM=BUDDY ./compile.sh` compila Buddy.

### 1.5 Syscalls de memoria
- [x] `sys_malloc`, `sys_free`, `sys_mem_status` en la tabla del kernel.
- [x] Wrappers en `Userland/asm/userlib.asm`.
- [x] La variante `mm_malloc_kernel` excluye el alloc de `alloc_count` (no visible en `sys_mem_status`).

### 1.6 Test
- [x] `test_mm` integrado en el sistema. Pasa 20 OK / 0 FAIL con First-Fit.
- [x] `test_mm` se ejecuta como **proceso separado** (spawn desde la shell, foreground/background). Ver Paso 5.4.

---

## Paso 2 - Procesos, Context Switching y Scheduler

**Estado: COMPLETADO**

### 2.1 PCB (Process Control Block)
- [x] Estructura con: PID, nombre, prioridad, estado (READY/RUNNING/BLOCKED/ZOMBIE).
- [x] RSP guardado, foreground/background flag.
- [x] File descriptors `fd[2]` (stdin=0, stdout=1).
- [x] `parent_pid`, soporte `waitpid`.

### 2.2 Tabla de procesos
- [x] Array de PCBs con limite `MAX_PROCESSES = 64`.
- [x] Crear proceso: asignar PID, reservar stack de 4KB con `mm_malloc`, preparar stack frame inicial.
- [x] Destruir/finalizar proceso: liberar recursos.

### 2.3 Context Switch
- [x] Rutina en ASM (`interrupts.asm`): guarda/restaura todos los registros en el stack del proceso.
- [x] Invocado desde handler del timer (IRQ0) para multitasking preemptivo.
- [x] Yield cooperativo via `int 0x80` con `force_switch`.

### 2.4 Scheduler (Round Robin con prioridades)
- [x] Prioridades 1-5; mayor prioridad = mas quanta consecutivos.
- [x] Ignora procesos en estado BLOCKED o ZOMBIE.

### 2.5 Proceso idle
- [x] PID 0, ejecuta `hlt` en loop. Se ejecuta cuando no hay ningun READY.

### 2.6 Adaptacion al modelo multiproceso
- [x] Kernel crea proceso `idle` (PID 0) y proceso `shell` (foreground, entry `0x400000`).
- [x] `scheduler_start_asm` es el unico camino de kernel a userland.

### 2.7 Syscalls de procesos
- [x] `sys_create_process(name, function, argc, argv, fg)` -> PID.
- [x] `sys_exit(retval)`.
- [x] `sys_getpid()`.
- [x] `sys_ps(info_buffer)`.
- [x] `sys_kill(pid)`.
- [x] `sys_nice(pid, new_priority)`.
- [x] `sys_block(pid)`.
- [x] `sys_unblock(pid)`.
- [x] `sys_yield()`.
- [x] `sys_waitpid(pid)`.
- [x] Wrappers `my_*` en `Userland/c/include/syscall.h` (API que usan los tests de la catedra).

### 2.8 Tests
- [x] `test_processes` como proceso separado (foreground y background). Ver Paso 5.4.
- [x] `test_prio` como proceso separado. Ver Paso 5.4.

---

## Paso 3 - Sincronizacion (Semaforos)

**Estado: COMPLETADO (verificacion pendiente)**

### 3.1 Semaforos con nombre
- [x] Estructura: valor, cola circular de PIDs bloqueados, nombre.
- [x] Tabla global de semaforos (max `MAX_SEMS = 32`).
- [x] Instruccion atomica (`xchg` o `lock cmpxchg`) para proteger acceso al valor.
- [x] `sem_wait`: si valor == 0 bloquea el proceso (sin busy waiting); si > 0 decrementa.
- [x] `sem_post`: incrementa valor; si hay cola, desbloquea uno.
- [x] Apertura por nombre + reference counting (`sem_open` es idempotente).

### 3.2 Syscalls de semaforos
- [x] `sys_sem_open(name, initial_value)`.
- [x] `sys_sem_close(sem_id)`.
- [x] `sys_sem_wait(sem_id)`.
- [x] `sys_sem_post(sem_id)`.
- [x] Wrappers `my_sem_*` en `userlib`.

### 3.3 Test
- [x] `test_sync` como proceso separado. Ver Paso 5.4.
- [ ] Verificar con semaforos: resultado final siempre 0.
- [ ] Verificar sin semaforos: resultado varia entre ejecuciones (comportamiento esperado).

---

## Paso 4 - Inter Process Communication (Pipes)

**Estado: COMPLETADO**

**Dependencias:** Paso 2 y Paso 3 completados (ya lo estan).
**Referencia:** Ver ejemplo en `https://github.com/alejoaquili/ITBA-72.11-SO/tree/main/examples/producer-consumer/` para patron de sincronizacion bloqueante con semaforos.

### 4.1 Implementar pipes unidireccionales
- [x] Estructura del pipe:
  - Buffer circular en memoria del kernel (ej. 4KB).
  - Punteros de lectura/escritura.
  - Colas de espera de lectores/escritores (bloqueo sin busy waiting).
  - Contador de extremos abiertos (para detectar EOF).
- [x] Lectura bloqueante: si el pipe esta vacio, el lector se bloquea (sin busy waiting).
- [x] Escritura bloqueante: si el pipe esta lleno, el escritor se bloquea.
- [x] **EOF**: cuando se cierra el extremo de escritura (contador de escritores llega a 0), `read` retorna 0 en vez de bloquearse.
- [x] Tabla global de pipes (anonimos).

### 4.2 Abstraccion de file descriptors (FD)

**Este es el punto mas critico del TP.** Hace que `sys_read`/`sys_write` sean transparentes.

- [x] Definir una union/struct de FD que pueda apuntar a:
  - Terminal (teclado/pantalla — comportamiento actual).
  - Extremo de lectura de un pipe.
  - Extremo de escritura de un pipe.
- [x] Cada PCB tiene `fd[2]`: `fd[0]` = stdin, `fd[1]` = stdout.
- [x] Modificar `sys_read` para consultar `current_process->fd[0]` y despachar al terminal o al pipe.
- [x] Modificar `sys_write` para consultar `current_process->fd[1]` y despachar al terminal o al pipe.
- [x] **Ningun proceso necesita saber si lee/escribe de un pipe o de la terminal** — la transparencia es un requisito del enunciado.

### 4.3 Pipes con nombre
- [x] Permitir que procesos no relacionados compartan un pipe acordando un string identificador.
- [x] `sys_pipe_open(name)` crea si no existe, abre si ya existe.

### 4.4 Syscalls de pipes
- [x] `sys_pipe(fd_array[2])` — crea pipe anonimo, devuelve fd[0]=lector, fd[1]=escritor en el proceso que llama.
- [x] `sys_pipe_open(name)` — crea o abre pipe nombrado, devuelve fd de lectura y escritura.
- [x] `sys_dup2(old_fd, new_fd)` — redirige un FD (usado por la shell para conectar pipes: `dup2(pipe_write, 1)` en el hijo-escritor, `dup2(pipe_read, 0)` en el hijo-lector).
- [x] `sys_close(fd)` — cierra un file descriptor (decrementar contador de extremos del pipe).
- [x] Wrappers en `Userland/asm/userlib.asm` y `Userland/c/userlib.c`.
- [x] Actualizar `CANT_SYS` en `defs.h` y la tabla `syscalls[]` en `syscallDispatcher.c`.

---

## Paso 5 - Aplicaciones de User Space

**Estado: PENDIENTE**

**Dependencias:** Pasos 1-4 completados.
**Referencia shell:** `https://github.com/alejoaquili/ITBA-72.11-SO/tree/main/examples/simple-shell/` — patron de fork/exec/wait adaptado a nuestras syscalls.

### 5.1 Reescribir la shell (`sh`)

La shell actual es single-tasking. Reescribirla para que:

- [ ] Parsee el comando y use `sys_create_process` para ejecutarlo como hijo.
- [ ] Soporte `&` al final para ejecutar en **background** (shell no cede foreground, no llama `waitpid`).
- [ ] Soporte `|` para conectar 2 procesos via pipe:
  1. Crear un pipe con `sys_pipe`.
  2. Crear proceso-escritor con `fd[1]` = extremo escritura del pipe.
  3. Crear proceso-lector con `fd[0]` = extremo lectura del pipe.
  4. La shell cierra sus propias copias de los extremos del pipe.
  5. Solo soporta 2 etapas (`cmd1 | cmd2`); no es necesario `p1 | p2 | p3`.
- [ ] Soporte `Ctrl+C`: enviar `sys_kill` al proceso en foreground.
- [ ] Soporte `Ctrl+D`: cerrar el stdin del proceso en foreground (escribir EOF en su pipe de stdin, o senhal equivalente).
- [ ] En foreground: llamar `sys_waitpid` antes de mostrar el proximo prompt.

### 5.2 Comandos basicos (cada uno como proceso separado)
- [ ] `help` — lista todos los comandos; apartado especial para tests de la catedra.
- [ ] `mem` — imprime estado de la memoria (total, usada, libre) via `sys_mem_status`.
- [ ] `ps` — lista procesos: nombre, PID, prioridad, RSP, RBP, foreground flag.
- [ ] `loop` — imprime su PID con saludo cada cierto tiempo. **Espera activa** (no bloquearse — es uno de los pocos casos permitidos de busy waiting).
- [ ] `kill <pid>` — mata proceso por PID.
- [ ] `nice <pid> <prioridad>` — cambia prioridad.
- [ ] `block <pid>` — alterna estado BLOCKED/READY.

### 5.3 Comandos de IPC (cada uno como proceso separado)
- [ ] `cat` — lee de `fd[0]` (stdin) y escribe en `fd[1]` (stdout). Funciona solo y via pipe.
- [ ] `wc` — cuenta lineas del stdin. Termina al recibir EOF.
- [ ] `filter` — lee stdin, filtra vocales (mayus y minus), escribe en stdout.
- [ ] `mvar <escritores> <lectores>` — problema de lectores/escritores sobre una MVar (variable compartida con semaforos). El proceso principal termina **inmediatamente** despues de crear hijos.
  - Escritores: espera activa aleatoria → espera que la variable este vacia → escribe valor unico ('A', 'B', 'C'...).
  - Lectores: espera activa aleatoria → espera que haya valor → consume e imprime con color unico.
  - Ver tabla de comportamiento esperado en `CLAUDE.md` seccion "mvar expected behavior".

### 5.4 Tests como procesos de usuario (OBLIGATORIO para aprobar)

Todos deben poder correrse en **foreground y background**. Son los mismos archivos `.c` de la catedra, integrados como procesos reales, no built-ins.

- [x] Verificar que `Userland/c/include/syscall.h` expone la API `my_*` exactamente como la espera `https://github.com/alejoaquili/ITBA-72.11-SO/blob/main/kernel-development/tests/syscall.h`.
- [x] `test_mm <max_bytes>` — proceso separado, foreground y background.
- [x] `test_processes <max_procs>` — proceso separado, foreground y background.
- [x] `test_prio <target_value>` — proceso separado.
- [x] `test_sync <pairs> <increments> <use_sem>` — proceso separado, foreground y background.

---

## Paso 6 - Verificacion y limpieza final

### 6.1 Verificacion obligatoria (criterios de aprobacion del enunciado)
- [ ] Compilar con `-Wall` sin warnings.
- [ ] `test_mm` en foreground y background → sin errores (al menos con uno de los dos MM).
- [ ] `test_proc` en foreground y background → sin errores.
- [ ] `test_sync` en foreground y background → resultado 0 con semaforos.
- [ ] `test_prio` → se visualizan diferencias de tiempo segun prioridad.
- [ ] Sistema libre de deadlocks, race conditions y busy waiting (excepto: `loop`, `test_sync` sin semaforos).
- [ ] Todos los comandos del enunciado existen con el nombre exacto indicado.
- [ ] `|` y `&` funcionan correctamente.
- [ ] `Ctrl+C` y `Ctrl+D` funcionan correctamente.

### 6.2 Analisis estatico (recomendado pre-entrega)
- [ ] Ejecutar dentro del container: `cppcheck --enable=all --inconclusive -I Kernel/include Kernel/c/`
- [ ] Revisar y corregir cualquier warning relevante antes de entregar.

### 6.3 Limpieza del repositorio
- [ ] Eliminar todos los binarios del repo (`.o`, `.bin`, `.img`, `.qcow2`, `.vmdk`).
- [ ] Verificar que `.gitignore` cubra todos los artefactos de compilacion.

### 6.4 README.md
- [ ] Crear/actualizar `README.md` en la raiz con:
  - Instrucciones de compilacion y ejecucion.
  - Nombre y descripcion de cada comando y test, con sus parametros.
  - Caracteres especiales: `&` para background, `|` para pipes.
  - Atajos de teclado: `Ctrl+C` (matar foreground), `Ctrl+D` (EOF).
  - Ejemplos de uso para cada requerimiento.
  - Requerimientos faltantes o parcialmente implementados.
  - Limitaciones.
  - Citas de fragmentos de codigo / uso de IA.

---

## Orden de implementacion recomendado (actualizado)

```
[HECHO] Paso 0 (Preparacion)
[HECHO] Paso 1 (Memory Management)
[HECHO] Paso 2 (Procesos + Context Switch + Scheduler)
[HECHO] Paso 3 (Semaforos)
           |
    +-------+--------+
    |                |
[HECHO] Paso 4.1-4.2    Paso 5.2 (comandos simples: help, mem, ps, loop, kill, nice, block)
(Pipes + FDs)            |
    |                |
 [HECHO] Paso 4.3   Paso 5.4 (tests como procesos — no necesitan pipes)
 (Named pipes +       |
  sys_pipe_open)      |
    |                |
    +-------+--------+
           |
   Paso 5.1 (Shell con & | Ctrl+C Ctrl+D)
           |
   Paso 5.3 (cat, wc, filter, mvar — requieren pipes)
           |
   Paso 6 (Verificacion + README)
```

**Prioridad inmediata:**
1. **Paso 5.1 (shell con & | Ctrl+C | Ctrl+D)** — para demostrar foreground/background y manejo de señales.
2. **Paso 5.3 (cat, wc, filter, mvar)** — depende de pipes funcionando en userland.
3. **Paso 6 (verificacion + README)** — cierre de requisitos y entrega.
