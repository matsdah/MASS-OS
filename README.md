# MASS OS — TP2 Sistemas Operativos (ITBA)

Kernel bare-metal x86-64 que corre directamente sobre QEMU o hardware real, sin sistema operativo subyacente. Implementa memoria, procesos, scheduling y syscalls desde cero.

## Requisitos

- Docker (único requisito en el host)
- QEMU (`qemu-system-x86_64`) para ejecutar la imagen
- Docker Desktop o el daemon Docker en ejecución antes de correr `./create.sh`

---

## Primer uso: crear el contenedor

```bash
./create.sh
```

Descarga la imagen `agodio/itba-so-multiarch:3.1` y crea el contenedor `TP_SO_2` con el directorio actual montado en `/root`. Solo es necesario hacerlo una vez.

---

## Compilar

```bash
./compile.sh           # compila con First-Fit (default)
./compile.sh vbox      # ídem, genera imagen VirtualBox (.vmdk)
```

### Selección del memory manager

El kernel soporta dos implementaciones de memoria, seleccionables en tiempo de compilación mediante la variable de entorno `MM`:

| Comando | Memory Manager |
|---------|---------------|
| `./compile.sh` | First-Fit (default) |
| `MM=FF ./compile.sh` | First-Fit (explícito) |
| `MM=BUDDY ./compile.sh` | Buddy System |

```bash
# Buddy System + imagen QEMU
MM=BUDDY ./compile.sh

# Buddy System + imagen VirtualBox
MM=BUDDY ./compile.sh vbox
```

> **First-Fit**: lista enlazada de bloques libres; busca el primer bloque que ajuste.
> **Buddy System**: bloques de potencia de 2; divide y fusiona en pares (`buddies`).

La selección es **exclusiva en compilación**: una imagen usa un único MM. Para comparar ambos hay que compilar y correr por separado.

---

## Ejecutar

```bash
./run.sh           # QEMU (busca .qcow2, fallback a .img)
./run.sh vbox      # muestra pasos para VirtualBox
./run.sh usb       # muestra comando dd para grabar en USB
```

**Permiso denegado en la imagen**: el contenedor corre como root, por lo que el `.qcow2` queda con permisos de root. Si `./run.sh` falla:

```bash
sudo chown $USER Image/x64BareBonesImage.qcow2
```

---

## Compilar y correr en un paso

```bash
./compile.sh && ./run.sh              # First-Fit
MM=BUDDY ./compile.sh && ./run.sh     # Buddy System
```

---

## Shell de usuario

Al bootear se inicia una shell interactiva. Comandos disponibles:

| Comando | Descripción |
|---------|-------------|
| `help` | Lista de comandos |
| `clear` | Limpia la pantalla |
| `printTime` / `printDate` | Hora y fecha del sistema |
| `registers` | Dump de registros de la última excepción |
| `testDiv0` / `invOp` | Disparan excepción #DE / #UD |
| `playBeep` | Toca una melodía por el parlante |
| `bmFPS` / `bmCPU` / `bmMEM` / `bmKEY` | Benchmarks |
| `testMM` | Test suite del memory manager (ver abajo) |
| `ps` | Lista procesos activos |
| `+` / `-` | Aumenta / disminuye tamaño de fuente |

---

## Test del Memory Manager

Dentro de la shell, ejecutar:

```
testMM
```

Corre una suite de 5 tests (alloc/free básico, múltiples allocaciones, coalescencia, edge cases, stress). El resultado esperado es `20 OK / 0 FAIL`.

Para testear **ambas implementaciones** compilar y ejecutar por separado:

```bash
# Testear First-Fit
./compile.sh && ./run.sh
# → dentro del OS: testMM

# Testear Buddy System
MM=BUDDY ./compile.sh && ./run.sh
# → dentro del OS: testMM
```

---

## Limpiar artefactos

```bash
make clean
```
