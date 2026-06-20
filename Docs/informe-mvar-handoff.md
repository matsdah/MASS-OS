# Informe: MVar — Handoff sin Retry y Atomicidad con Spinlock

> Trabajo Práctico Nº 2 — Sistemas Operativos (ITBA)
> Sesión de corrección sobre `Kernel/c/syscall/mvar.c` y `Userland/c/commands/mvar.c`.

---

## Tabla de contenidos

1. [Conceptos clave](#1-conceptos-clave)
   - [1.1 El patrón retry con `-2`](#11-el-patrón-retry-con--2)
   - [1.2 El patrón handoff con `rsp[14]`](#12-el-patrón-handoff-con-rsp14)
   - [1.3 El struct `mvar_q_entry` vs. campo en el PCB](#13-el-struct-mvar_q_entry-vs-campo-en-el-pcb)
2. [Problemas detectados](#2-problemas-detectados)
3. [Resumen detallado de cambios](#3-resumen-detallado-de-cambios)
4. [Proceso de decisión y fundamentación](#4-proceso-de-decisión-y-fundamentación)
5. [Por qué este diseño soluciona los problemas](#5-por-qué-este-diseño-soluciona-los-problemas)
6. [Cumplimiento de la consigna de MVar](#6-cumplimiento-de-la-consigna-de-mvar)
7. [Verificación](#7-verificación)

---

## 1. Conceptos clave

### 1.1 El patrón retry con `-2`

El diseño anterior usaba un código de retorno especial `-2` para señalizar "el proceso fue bloqueado". El flujo era:

```
userland: sys_mvar_put(name, 'A')
  kernel: MVar FULL → encola PID, BLOCKED, force_switch=1, return -2
  ISR:    guarda -2 en rsp[14], yield (deschedulea)
  [proceso bloqueado...]
  [un take despierta al escritor via process_unblock]
  [proceso re-scheduleado, popState restaura RAX=-2]
userland: r == -2 → sys_yield() → reintenta sys_mvar_put(name, 'A')
  kernel: MVar ahora EMPTY → escribe 'A', return 0
userland: r == 0 → continúa
```

Este patrón **filtra un detalle de implementación del kernel** (el hecho de que el proceso fue bloqueado) al espacio de usuario. El contrato de `mvar_put` debería ser `0 = éxito, -1 = error`, no `0, -1, -2`. Además, el `sys_yield()` + retry es un **semi-busy-wait**: el proceso ya fue desbloqueado porque la condición cambió, pero vuelve a ceder CPU y reentrar al syscall para "descubrir" algo que el kernel ya sabe.

### 1.2 El patrón handoff con `rsp[14]`

El diseño nuevo usa **handoff** (entrega directa): la contraparte que desbloquea al proceso **escribe directamente** el valor de retorno en el slot RAX del stack guardado del proceso bloqueado. Cuando el proceso es re-scheduleado, `popState` restaura ese RAX y `iretq` lo devuelve a userland con el valor correcto — **sin retry, sin `-2`, sin reentrada**.

La clave de este mecanismo es cómo el ISR maneja el retorno de las syscalls (`Kernel/asm/interrupts.asm`):

```asm
call [syscalls + rax * 8]     ; ejecuta la syscall; retorno en rax
mov [rsp + 14*8], rax         ; guarda retorno en el slot RAX del frame
; si force_switch → scheduler_yield_impl (guarda cur->rsp)
```

El slot `[rsp + 14*8]` es el RAX dentro del frame pusheado por `pushState`. Cuando el proceso es descheduleado, su `rsp` (que apunta a ese frame) se guarda en `PCB.rsp`. Cualquier código del kernel que tenga acceso al PCB puede **escribir directamente** el valor de retorno en `pcb->rsp[14]`, y ese valor será restaurado por `popState` cuando el proceso vuelva a ejecutarse.

Este mecanismo ya se usaba en el kernel para `waitpid` (`process.c:319`):

```c
parent->rsp[14] = (uint64_t)(int64_t)retval;  /* padre recibe retval en RAX */
```

La innovación de esta corrección es extenderlo a MVar: cuando un `put` despierta a un lector, escribe el valor en `reader->rsp[14]`; cuando un `take` despierta a un escritor, el escritor ya tiene `0` en su `rsp[14]` (guardado por el ISR al bloquearlo) y además su valor ya fue escrito en la MVar por el `take`.

### 1.3 El struct `mvar_q_entry` vs. campo en el PCB

#### El problema del diseño ingenuo

Para implementar el handoff de un escritor bloqueado, el kernel necesita recordar **qué valor** quería escribir ese escritor. La opción más obvia es agregar un campo al PCB:

```c
typedef struct PCB {
    ...
    uint8_t mvar_pending_value;  /* valor a escribir cuando despierte */
} PCB;
```

Esto es **conceptualmente incorrecto**. El valor pendiente pertenece a la **operación de MVar**, no al proceso. Un proceso no "tiene" un valor pendiente de MVar como propiedad permanente; lo tiene **transitoriamente**, sólo mientras está bloqueado en la cola de un MVar. Para los otros 63 procesos del sistema (shell, lectores, tests, pipes, etc.) ese byte es basura inútil que contamina el PCB.

Es una violación del principio de **alta cohesión**: el PCB debería contener información del proceso, no información de los mecanismos de sincronización que el proceso usa accidentalmente. Si mañana se agrega otro mecanismo de sincronización con valores pendientes, ¿se le sumaría otro campo al PCB? El PCB crecería con campos específicos de cada primitiva.

#### La solución: `mvar_q_entry`

El valor pendiente vive en la **entrada de la cola del MVar**, atado al par {PID, valor} que representa "el proceso X quiere escribir el valor Y":

```c
typedef struct {
    uint64_t pid;
    char     value;   /* valor pendiente del escritor; 0 para lectores */
} mvar_q_entry;
```

Las colas del MVar cambian de `uint64_t wq[N]` (PIDs sueltos) a `mvar_q_entry wq[N]` (PID + valor empaquetados):

```c
typedef struct {
    ...
    mvar_q_entry wq[MVAR_Q_SIZE];   /* antes: uint64_t wq[MVAR_Q_SIZE] */
    mvar_q_entry rq[MVAR_Q_SIZE];   /* antes: uint64_t rq[MVAR_Q_SIZE] */
    ...
} MVar;
```

#### Valor agregado del struct

| Aspecto | Campo en PCB | `mvar_q_entry` en cola |
|---|---|---|
| **Propiedad** | El valor vive en el proceso, pero pertenece a la operación del MVar | El valor vive en el MVar, donde pertenece |
| **Cohesión** | Baja: el PCB se contamina con campos de primitivas específicas | Alta: cada MVar es autónomo y contiene toda su información |
| **Acoplamiento** | Alto: `mvar.c` debe leer/escribir un campo del PCB | Bajo: `mvar.c` sólo lee/escribe sus propias colas |
| **Escalabilidad** | Cada primitiva nueva sumaría campos al PCB | Cada primitiva nueva gestiona sus propias estructuras |
| **Memoria** | 1 byte × 64 procesos = 64 bytes siempre | 9 bytes × 64 entries × 2 colas × 16 MVars = 18 KB, pero sólo en MVars que existen |
| **Correctitud** | Si el proceso muere, el campo queda stale en el PCB | Si el proceso muere, `queue_remove_pid` elimina su entry completa |
| **Reutilización** | Imposible: el campo es específico de MVar | El patrón `q_entry {pid, data}` es reutilizable (pipes, semáforos, etc.) |

El valor agregado fundamental es **alta cohesión y bajo acoplamiento**: el MVar es dueño de toda la información de sus operaciones pendientes, y el PCB queda limpio de campos específicos de primitivas. Si un proceso está bloqueado en dos MVars distintos (caso teórico), cada MVar tiene su propia entry con su propio valor — no hay sharing de un campo del PCB.

#### Detalle de implementación

Para la cola de lectores (`rq`), el campo `value` de `mvar_q_entry` queda en 0 (los lectores no tienen un valor pendiente; ellos **reciben** el valor via `rsp[14]`). El struct es el mismo para ambas colas por simplicidad, aunque el campo `value` sólo sea meaningful para `wq`.

---

## 2. Problemas detectados

### Problema A: Acoplamiento kernel/userland con código `-2`

**Ubicación:** `Kernel/c/syscall/mvar.c:243-245` (`mvar_put`), `Kernel/c/syscall/mvar.c:275-277` (`mvar_take`), `Userland/c/commands/mvar.c:67-69` (`mvar_writer`), `Userland/c/commands/mvar.c:91-93` (`mvar_reader`).

**Síntoma:** `mvar_put`/`mvar_take` retornaban `-2` cuando el proceso era bloqueado. El wrapper de userland debía detectar `-2`, llamar `sys_yield()`, y reintentar el syscall desde el principio.

**Por qué es grave:**

- **Filtra detalles del kernel a userland.** El código `-2` expone que el proceso fue bloqueado, que es un detalle de implementación del kernel. El contrato de `mvar_put` debería ser `0 = éxito, -1 = error`, no incluir un tercer código para "fui bloqueado".
- **Semi-busy-wait.** El `sys_yield()` + retry es un loop innecesario: el proceso ya fue desbloqueado porque la condición cambió, pero vuelve a ceder CPU y reentrar al syscall.
- **Reentrada redundante.** El proceso re-ejecuta `mvar_put`/`mvar_take` completo: buscar MVar por nombre, validar, chequear estado, operar. Funciona porque el estado cambió, pero es trabajo redundante que consume CPU y entradas al syscall gate.
- **Fragilidad.** Si el estado de la MVar no hubiera cambiado al reintentar (race condition teórica), el proceso volvería a bloquearse con `-2`, creando un loop infinito de yield+retry. En la implementación actual funciona porque el `force_switch` garantiza que el proceso no se re-schedulea hasta que alguien lo desbloquee, pero es una propiedad implícita.

### Problema B: MVar sin atomicidad

**Ubicación:** `Kernel/c/syscall/mvar.c` (`mvar_put`, `mvar_take`, `mvar_destroy`, `mvar_cleanup_for_process`).

**Síntoma:** Las funciones modificaban `mv->state`, `mv->value` y las colas sin ninguna instrucción atómica ni spinlock. Es el mismo issue que tenían los semáforos antes de su corrección.

**Por qué es grave:** Por las mismas razones que en semáforos: no satisface el requisito de la cátedra de "utilizar alguna instrucción que garantice atomicidad", y es frágil frente a futuros cambios (preemción del kernel, SMP). El AGENTS.md lo documentaba explícitamente como pendiente.

---

## 3. Resumen detallado de cambios

### 3.1 Struct `mvar_q_entry`

**Archivo:** `Kernel/c/syscall/mvar.c`

```c
typedef struct {
    uint64_t pid;
    char     value;
} mvar_q_entry;
```

Las colas `wq[]` y `rq[]` del struct `MVar` cambian de `uint64_t` a `mvar_q_entry`. El valor pendiente del escritor viaja empaquetado con su PID en la entrada de la cola.

### 3.2 Spinlock en `MVar`

**Archivo:** `Kernel/c/syscall/mvar.c`

Se agrega `volatile uint64_t lock` al struct `MVar` y los helpers `mvar_lock`/`mvar_unlock` (idénticos a `sem_lock`/`sem_unlock`, reutilizando `atomic_xchg` de `libasm.asm`). El lock se inicializa en `UNLOCKED` en `mvar_init` y `mvar_create`.

### 3.3 Reescritura de helpers de cola

**Archivo:** `Kernel/c/syscall/mvar.c`

Las funciones `wq_push`, `wq_pop`, `rq_push`, `rq_pop`, `wq_pop_highest`, `rq_pop_highest`, `queue_remove_pid` se adaptan al nuevo tipo `mvar_q_entry`:

- `wq_push` ahora recibe el valor: `wq_push(mv, pid, value)`.
- `wq_pop`/`rq_pop` retornan `mvar_q_entry` en lugar de `uint64_t`.
- `wq_pop_highest`/`rq_pop_highest` retornan `mvar_q_entry` y reconstruyen la cola con `mvar_q_entry new_q[]`.
- `queue_remove_pid` compara `queue[idx].pid` en lugar de `queue[idx]`.

La lógica de índices (head, tail, count, modular) **no cambia** — es un cambio de tipo, no de algoritmo.

### 3.4 Reescritura de `mvar_put` con handoff + lock

**Archivo:** `Kernel/c/syscall/mvar.c`

Tres caminos bajo `mvar_lock`:

1. **EMPTY + lectores esperando (handoff):** extrae el lector de mayor prioridad, escribe el valor en `reader->rsp[14]`, desbloquea. La MVar queda EMPTY (el lector "tomó" el valor). Retorno 0.

2. **EMPTY sin lectores:** escribe el valor, pasa a FULL. Retorno 0.

3. **FULL (bloquea):** encola `{pid, value}` en `wq`, desbloquea el lock, setea `BLOCKED + force_switch`. Retorno 0 (el ISR lo guarda en `rsp[14]`; cuando un `take` despierte al escritor, su valor ya estará escrito en la MVar).

**Orden crítico:** el `mvar_unlock` va **siempre antes** de `process_unblock` o `cur->state = BLOCKED; force_switch = 1`. Si se liberara después, el proceso descheduleado nunca ejecutaría el unlock → deadlock.

### 3.5 Reescritura de `mvar_take` con handoff + lock

**Archivo:** `Kernel/c/syscall/mvar.c`

Tres caminos bajo `mvar_lock`:

1. **FULL + escritores esperando (handoff):** lee el valor actual, extrae el escritor de mayor prioridad, escribe el valor del escritor (`entry.value`) en `mv->value`, la MVar queda FULL. Desbloquea al escritor (que despierta con 0). Retorno del valor leído.

2. **FULL sin escritores:** lee el valor, pasa a EMPTY. Retorno del valor.

3. **EMPTY (bloquea):** encola `{pid, 0}` en `rq`, desbloquea el lock, setea `BLOCKED + force_switch`. Retorno 0 (placeholder que será sobrescrito por un `put` cuando despierte al lector via `rsp[14]`).

### 3.6 Reescritura de `mvar_destroy`

**Archivo:** `Kernel/c/syscall/mvar.c`

Bajo `mvar_lock`: recolecta todos los PIDs bloqueados, escribe `-1` en cada `rsp[14]` (MVar destruida), marca `in_use = 0`. Tras `mvar_unlock`, despierta a todos los procesos recolectados. El `-1` hace que los wrappers de userland detecten el error y salgan del loop.

### 3.7 Reescritura de `mvar_cleanup_for_process`

**Archivo:** `Kernel/c/syscall/mvar.c`

Itera todos los MVars abiertos. Para cada uno, bajo `mvar_lock`:
- Remueve el PID de ambas colas (`queue_remove_pid`).
- Si tras la limpieza hay un desbalance, despierta a un sucesor con handoff:
  - **EMPTY + escritor esperando:** extrae el escritor, escribe su `entry.value` en `mv->value`, pasa a FULL, despierta (el escritor despierta con 0).
  - **FULL + lector esperando:** extrae el lector, escribe `mv->value` en `reader->rsp[14]`, pasa a EMPTY, despierta (el lector despierta con el valor).
- Tras `mvar_unlock`, llama `process_unblock`.

### 3.8 Eliminación del loop retry en userland

**Archivo:** `Userland/c/commands/mvar.c`

```c
/* mvar_writer — ANTES */
while((r = sys_mvar_put(name, letter)) == -2){
    sys_yield();
}

/* mvar_writer — DESPUÉS */
int64_t r = sys_mvar_put(name, letter);
if(r == -1) break;   /* MVar destruida */
```

```c
/* mvar_reader — ANTES */
while((r = sys_mvar_take(name)) == -2){
    sys_yield();
}

/* mvar_reader — DESPUÉS */
int64_t r = sys_mvar_take(name);
if(r == -1) break;   /* MVar destruida */
char c = (char)(unsigned char)r;
```

El contrato desde userland queda limpio: `mvar_put` retorna `0` (éxito) o `-1` (MVar destruida); `mvar_take` retorna el valor (`0..255`) o `-1`. Sin `-2`, sin `sys_yield`, sin reentrada.

### 3.9 Actualización de `mvar.h` y `AGENTS.md`

**`Kernel/include/mvar.h`:** los comentarios de `mvar_put`/`mvar_take`/`mvar_destroy` se actualizaron para reflejar el handoff y eliminar la mención del `-2`.

**`AGENTS.md`:** la sección "Critical invariants (MVar / scheduler priority)" se actualizó con:
- El patrón handoff con `rsp[14]` (elimina `-2` y retry).
- El struct `mvar_q_entry` (valor en la cola, no en PCB).
- `mvar_destroy` escribe `-1` en `rsp[14]`.
- `mvar_cleanup_for_process` hace handoff al despertar sucesores.
- La constraint de atomicidad ahora incluye MVar (antes decía "pending").

---

## 4. Proceso de decisión y fundamentación

### 4.1 Handoff vs. alternativas para eliminar el `-2`

| Alternativa | Pros | Contras | Veredicto |
|---|---|---|---|
| **Handoff con `rsp[14]`** (elegida) | Elimina retry, elimina `-2`, reutiliza mecanismo existente (waitpid), bloqueo limpio, eficiente | Requiere acceso al `rsp` guardado del PCB desde `mvar.c` | **Mejor opción** |
| Mantener `-2` pero documentarlo | Cero código nuevo | No resuelve el issue; sigue filtraendo detalles del kernel | Rechazada |
| Busy-wait puro (sin bloquear) | Simple | Violación del requisito "no busy waiting"; ineficiente | Rechazada |
| Cola de eventos en userland | Desacopla kernel | Requiere infraestructura de IPC que no existe; complejo | Overkill |

El handoff es la opción que reutiliza un mecanismo **ya probado** en el kernel (`waitpid` usa `rsp[14]` desde antes de esta corrección) y produce el contrato más limpio desde userland.

### 4.2 `mvar_q_entry` vs. campo en el PCB

| Alternativa | Pros | Contras | Veredicto |
|---|---|---|---|
| **`mvar_q_entry` en cola** (elegida) | Alta cohesión (el valor vive donde pertenece), bajo acoplamiento (PCB limpio), reutilizable, correcto si el proceso muere | Cambio de tipo en todas las funciones de cola | **Mejor opción** |
| `mvar_pending_value` en PCB | Menos cambio de código | Contamina el PCB con campos de primitivas específicas; bajo acoplamiento; si el proceso muere el campo queda stale | Rechazada |
| Array paralelo `pending_values[MAX_PROCESSES]` en `mvar.c` | No toca el PCB | Indexado por PID global; frágil si hay PIDs reciclados; sharing entre MVars | Rechazada |
| Hash map PID→value en `mvar.c` | Flexible | Overkill; no hay malloc en el kernel para structs dinámicas; complejo | Rechazada |

La decisión se tomó por **alta cohesión**: el valor pendiente es información de la operación de MVar, no del proceso. Empaquetarlo con el PID en la entrada de la cola es la solución más cohesiva y la que menos acopla los módulos.

### 4.3 Spinlock con `atomic_xchg` para MVar

Se reutilizó el helper `atomic_xchg` agregado en la corrección de semáforos. El patrón es idéntico:

```c
static void mvar_lock(volatile uint64_t *l){
    while(atomic_xchg(l, LOCKED) == LOCKED){ /* spin */ }
}
static void mvar_unlock(volatile uint64_t *l){
    atomic_xchg(l, UNLOCKED);
}
```

**Regla crítica:** el unlock va **antes** de `process_unblock` o `cur->state = BLOCKED; force_switch = 1`. Estas funciones sólo setean estado + flag; el context switch ocurre en `iretq`. Si se liberara el lock después, el proceso descheduleado nunca ejecutaría el unlock → deadlock en single-core.

---

## 5. Por qué este diseño soluciona los problemas

### 5.1 Eliminación del `-2`

Los 6 casos posibles de `mvar_put`/`mvar_take` ahora se resuelven **sin retry**:

| Caso | Acción kernel | Retorno al proceso |
|---|---|---|
| put, EMPTY, sin readers | Escribe valor, FULL | put retorna 0 |
| put, EMPTY, con readers | Escribe valor en `reader->rsp[14]`, MVar queda EMPTY | put retorna 0; **reader despierta con valor en RAX** |
| put, FULL | Encola `{pid, value}`, bloquea | put retorna 0 (ISR); **despierta cuando take hace handoff** |
| take, FULL, sin writers | Lee valor, EMPTY | take retorna valor |
| take, FULL, con writers | Lee valor + escribe `entry.value`, FULL | take retorna valor; **writer despierta con 0** |
| take, EMPTY | Encola `{pid, 0}`, bloquea | take retorna 0 (placeholder); **despierta cuando put escribe valor en rsp[14]** |

En **ningún caso** el proceso debe reentrar al syscall. El handoff entrega el resultado directamente en el RAX guardado del proceso bloqueado.

### 5.2 Atomicidad garantizada

Toda la sección crítica (state + value + colas) está protegida por `mvar_lock`/`mvar_unlock` con `atomic_xchg`. El spinlock satisface el requisito de la cátedra de "utilizar alguna instrucción que garantice atomicidad" y es robusto frente a futuros cambios (SMP, preemción).

### 5.3 Limpieza del PCB

El PCB no se modificó. El valor pendiente del escritor vive en `mvar_q_entry.value`, dentro de la cola del MVar. El PCB queda limpio de campos específicos de primitivas de sincronización (aparte de `held_sems` para semáforos, que es un bitmap genérico de tenencia de recursos).

### 5.4 Despertar con error en `mvar_destroy`

Los procesos bloqueados en una MVar destruida despiertan con `-1` en su RAX (escrito por `mvar_destroy` en `rsp[14]`). Los wrappers de userland detectan `-1` y salen del loop limpiamente, sin colgarse esperando una MVar que ya no existe.

---

## 6. Cumplimiento de la consigna de MVar

La consigna dice:

> mvar: Implementa el problema de múltiples lectores y escritores sobre una variable global, similar a una MVar de Haskell. Toma como parámetros la cantidad de escritores y lectores. Cada escritor realiza una espera activa aleatoria, luego espera a que la variable esté vacía y finalmente escribe un valor único. Cada lector realiza una espera activa aleatoria, luego espera a que la variable tenga un valor para leer y finalmente lo consume e imprime junto con un identificador único. De esta forma, se simula el comportamiento de una MVar, garantizando que solo un proceso accede a la variable a la vez y que los accesos están correctamente sincronizados. El proceso principal debe terminar inmediatamente después de crear los lectores y escritores.

| Requisito | Cumple | Notas |
|---|---|---|
| "espera a que la variable esté vacía y escribe" | ✓ | Escritor bloquea si FULL; despierta cuando take hace handoff (su valor se escribe en la MVar) |
| "espera a que la variable tenga un valor y lo consume" | ✓ | Lector bloquea si EMPTY; despierta con el valor en RAX via `rsp[14]` |
| "solo un proceso accede a la variable a la vez" | ✓ | Spinlock con `xchg` serializa toda sección crítica |
| "acceses correctamente sincronizados" | ✓ | Spinlock + handoff atómico bajo el lock |
| "espera activa aleatoria" | ✓ | `random_busy_wait()` antes de put/take — sin cambios |
| "valor único ('A', 'B', 'C'...)" | ✓ | El valor viaja en `mvar_q_entry.value` desde el escritor |
| "consume e imprime con identificador (color)" | ✓ | Lector recibe valor en RAX, imprime con `sys_write_color` |
| "proceso principal termina inmediatamente" | ✓ | `mvar()` crea MVar + spawnea hijos + `return 0` |

El handoff es una **optimización de implementación** que no altera la semántica observable: el valor sigue transitando del escritor al lector con exclusión mutua garantizada. La diferencia es que cuando hay un lector esperando, el valor va directo a su RAX en lugar de pasar por `mv->value` y requerir un retry.

---

## 7. Verificación

### Compilación

Se compiló con `./compile.sh` (First-Fit) y `MM=BUDDY ./compile.sh` (Buddy System), ambos dentro del contenedor Docker `TP_SO_2`. En ambos casos el build pasó con **cero warnings** bajo `-Wall -Wextra -Werror` + banderas estrictas completas.

### Pruebas sugeridas en runtime (QEMU)

| Comando | Resultado esperado |
|---|---|
| `mvar 2 2` | Letras `A`, `B` se imprimen en colores rotativos; no se pierden valores; no se duplican |
| `mvar 5 3` | 5 escritores (A-E), 3 lectores; flujo continuo de letras en colores |
| `mvar 1 1` | Un escritor, un lector; alternancia estricta A A A... |
| `kill <pid-writer>` mientras corre `mvar` | El MVar no se deadlocks; otros escritores/lectores siguen |
| `test_sync 100 1` | `Valor final: 0` (el cambio de MVar no afecta semáforos) |
| `test_prio 1000000` | Los 3 procesos terminan (el cambio de MVar no afecta el scheduler) |

### Archivos modificados

| Archivo | Cambio |
|---|---|
| `Kernel/c/syscall/mvar.c` | struct `mvar_q_entry`, spinlock, handoff en put/take/destroy/cleanup, helpers de cola adaptados |
| `Kernel/include/mvar.h` | comentarios actualizados (handoff, sin -2) |
| `Userland/c/commands/mvar.c` | eliminado loop `-2`/`sys_yield` en `mvar_writer` y `mvar_reader` |
| `AGENTS.md` | invariants de MVar actualizados (handoff, spinlock, `mvar_q_entry`) |

### No se modificó

| Archivo | Razón |
|---|---|
| `Kernel/include/process.h` | **No** se agrega `mvar_pending_value` al PCB — el valor va en la cola |
| `Kernel/asm/interrupts.asm` | El ISR ya guarda retorno en `rsp[14]` antes del yield |
| `Kernel/asm/libasm.asm` | `atomic_xchg` ya existe (agregado en corrección de semáforos) |
| `Kernel/include/lib.h` | `atomic_xchg` ya está declarado |
