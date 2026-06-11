# MASS OS — TP2 Sistemas Operativos (ITBA)

Kernel bare-metal x86-64 que corre directamente sobre QEMU o hardware real sin sistema operativo subyacente. Implementa memoria, procesos, scheduling Round-Robin con prioridades, semaforos nombrados, pipes (anonimos y nombrados) y syscalls desde cero.

## Requisitos

- QEMU para ejecutar la imagen (`qemu-system-x86_64`)
- Docker Desktop o el daemon Docker en ejecucion antes de correr `./create.sh`

---

## Primer uso: crear el contenedor

```bash
./create.sh
```

Descarga la imagen `agodio/itba-so-multiarch:3.1` y crea el contenedor `TP_SO_2` con el directorio actual montado en `/root`.

---

## Compilar

```bash
./compile.sh           # compila con First-Fit (default)
```

### Seleccion del memory manager

El kernel soporta dos implementaciones de memoria, seleccionables en tiempo de compilacion mediante la variable de entorno `MM`:

| Comando | Memory Manager |
|---------|---------------|
| `./compile.sh` | First-Fit (default) |
| `MM=FF ./compile.sh` | First-Fit (explicito) |
| `MM=BUDDY ./compile.sh` | Buddy System |

```bash
# Buddy System + imagen QEMU
MM=BUDDY ./compile.sh
```

> **First-Fit**: lista enlazada de bloques libres; busca el primer bloque que ajuste.
> **Buddy System**: bloques de potencia de 2; divide y fusiona en pares (`buddies`).

---

## Ejecutar

```bash
./run.sh           # QEMU
```

**Permiso denegado en la imagen**: el contenedor corre como root, por lo que el `.qcow2` queda con permisos de root. Si `./run.sh` falla:

```bash
sudo chown $USER Image/x64BareBonesImage.qcow2
```

---

## Shell de usuario

Al bootear se inicia una shell interactiva.

### Comandos del sistema

| Comando | Parametros | Descripcion |
|---------|-----------|-------------|
| `help` | — | Lista de comandos disponibles. |
| `clear` | — | Limpia la pantalla. |
| `printTime` | — | Imprime hora actual (UTC-3). |
| `printDate` | — | Imprime fecha actual. |
| `registers` | — | Dump de registros de la ultima excepcion (requiere `F1` para capturar). |
| `testDiv0` | — | Dispara excepcion #DE (division por cero). |
| `invOp` | — | Dispara excepcion #UD (instruccion invalida). |
| `playBeep` | — | Reproduce una melodia por el PC speaker. |
| `mem` | — | Muestra estado de la memoria (total, libre, usada, usada por kernel, cantidad de allocaciones). |
| `kill <pid>` | `pid`: ID del proceso | Termina el proceso indicado. |
| `nice <pid> <prio>` | `pid`: ID, `prio`: 1–5 | Cambia la prioridad del proceso. |
| `block <pid>` | `pid`: ID del proceso | Alterna estado entre BLOCKED y READY. |
| `loop` | — | Imprime su PID periodicamente (busy-wait permitido). |
| `sh` | — | Abre una nueva shell interactiva. |
| `cat` | — | Lee stdin y escribe stdout hasta EOF. |
| `wc` | — | Cuenta lineas, palabras y bytes de stdin. |
| `filter` | — | Filtra vocales (mayusculas y minusculas) de stdin. |
| `mvar <esc> <lec>` | `esc`: escritores, `lec`: lectores | Problema lectores/escritores sobre MVar nativa del kernel. |
| `ps` | — | Lista procesos activos con PID, prioridad, estado y foreground. |

### Tests provistos

| Test | Parametros | Descripcion |
|------|-----------|-------------|
| `test_mm <max>` | `max`: bytes maximos | Ciclo infinito de alloc/free; imprime solo si hay error. Debe funcionar con al menos un MM. |
| `test_processes <max>` | `max`: cantidad de procesos | Crea, bloquea, desbloquea y mata procesos dummy ciclicamente. Nota: el enunciado lo llama `test_proc`. |
| `test_prio <target>` | `target`: valor a alcanzar | Crea 3 procesos con distintas prioridades; visualiza diferencias de ejecucion. |
| `test_sync <n> <sem>` | `n`: iteraciones, `sem`: 0/1 | Test de condiciones de carrera. Resultado esperado `0` si `sem=1`. Nota: el enunciado especifica 3 params pero la cantidad de pares esta hardcodeada en 2. |
| `test_named_pipe` | — | Test de pipes con nombre (IPC entre escritor y lector). |

---

## Caracteres especiales de la shell

- `&` al final de un comando: ejecuta en **background**. El prompt vuelve inmediatamente.  
  Ejemplo: `loop &`
- `|` entre dos comandos: conecta stdout del primero con stdin del segundo mediante un pipe unidireccional.  
  Ejemplo: `cat | wc`, `filter | cat`

---

## Atajos de teclado

| Atajo | Accion |
|-------|--------|
| `Ctrl + C` | Mata el proceso en **foreground** mas reciente (el hijo, no la shell). |
| `Ctrl + D` | Envia EOF (`0x04`) al stdin del proceso foreground. |
| `F1` | Captura snapshot de registros (para luego visualizar con `registers`). |
| `+` / `-` | Aumenta / disminuye tamano de fuente durante la edicion de linea. |

---

## Ejemplos por fuera de los tests

### Memory Manager
```bash
mem                    # ver estado actual
```

### Procesos y Scheduling
```bash
loop &                 # proceso en background
ps                     # ver procesos activos
kill <pid>             # matar el loop
nice <pid> 5           # subir prioridad
block <pid>            # bloquear/desbloquear
sh                     # shell anidada; exit o Ctrl+D vuelve
```

### Sincronizacion
```bash
test_sync 100 1        # con semaforos; resultado esperado: 0
test_sync 100 0        # sin semaforos; resultado varia (race condition)
```

### IPC (pipes)
```bash
cat | wc               # contar lineas de lo que se escribe por teclado
filter | cat           # filtrar vocales de stdin
```

---

## Estado de implementacion

- **Comandos como procesos reales**: `mem`, `kill`, `nice`, `block`, `loop`, `sh`, `cat`, `wc`, `filter`, `mvar`.
- **Tests como procesos reales**: `test_mm`, `test_processes`, `test_prio`, `test_sync`, `test_named_pipe`.
- **Built-ins heredados de Arquitectura de Computadoras**: `clear`, `ps`, `printTime`, `printDate`, `registers`, `testDiv0`, `invOp`, `playBeep`.

---

## Requerimientos faltantes o parcialmente implementados

- El scheduler usa Round-Robin con prioridades fijas (1–5). El MVar implementa anti-starvation via cooldown para evitar bloqueo de escritores de baja prioridad.
- `test_sync` acepta 2 parametros (`<n> <use_sem>`) en lugar de los 3 del enunciado (`<pairs> <increments> <use_sem>`); la cantidad de pares esta hardcodeada en 2.
- Los comandos `clear`, `ps`, `printTime`, `printDate`, `registers`, `testDiv0`, `invOp`, `playBeep` son procesos built-ins, no procesos de usuario. Estas funciones pertenecen al trabajo práctico de la materia Arquitecutra de Computadoras, y fue aprobado por el profesor en las 'Office Hours'.

---

## Uso de IA / Fragmentos de codigo asistidos

Algunas partes del codigo fueron desarrolladas o modificadas con asistencia de la IA generativa OpenCode y GitHub Copilot. Los cambios fueron revisados manualmente, probados en QEMU y adaptados al contexto del TP.

### Cambios asistidos principales (con commits)

**Correccion de bugs y alineacion con referencia de la catedra:**
- `Userland/c/tests/test_util.c`: distribucion uniforme real en `GetUniform()` (elimino modulo bias); fix de conteo de caracteres en `printf()`. *(commit `3d57ccb`)*
- `Userland/c/tests/test_sync.c`: firma de `my_process_inc` cambiada a `int64_t` con retorno de error.
- `Userland/c/shell/syscall.c` + `Userland/c/shell/userlib.c`: registro de `test_named_pipe` como proceso hijo.
- `Userland/c/shell/help.c`: corrigio texto de `+`/`-` de "built-in" a "atajo de teclado".

**Estandarizacion C (`()` -> `(void)`):**
- `Kernel/c/interrupts/idtLoader.c`, `Kernel/c/drivers/keyboardDriver.c`, `Kernel/c/drivers/videoDriver.c`, `Kernel/c/time.c`
- `Userland/c/shell/shell.c`, `Userland/c/shell/userlib.c`

**Modularizacion del Kernel:**
- `Kernel/Makefile`: reorganizacion en carpetas (`scheduler/`, `process/`, `semaphore/`, `pipe/`, `interrupts/`, `syscall/`, `lib/`, `sound/`, `kernel/`).

**Modularizacion de Userland:**
- `Userland/Makefile`: reorganizacion en carpetas (`tests/`, `shell/`, `commands/`).
- Movimiento de 18 archivos `.c` a sus carpetas correspondientes con actualizacion de includes.

**Documentacion:**
- `README.md`: correcciones (`testMM` -> `test_mm`, descripciones de tests y funciones).
- `AGENTS.md`: actualizaciones de rutas y convenciones.

### Verificacion

Todos los cambios asistidos compilaron con `-Wall -Wextra -Werror` sin warnings y fueron ejecutados en QEMU.

---

## Limpiar artefactos

```bash
make clean
```

> Nota: `make` debe ejecutarse **dentro** del contenedor Docker (`TP_SO_2`). En el host usar `./compile.sh`.
