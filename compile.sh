#!/bin/bash
set -euo pipefail

# Uso: ./compile.sh [TARGET]
#   TARGET: qemu (default), vbox, usb
#
# Memory manager (variable de entorno MM):
#   MM=FF     -> First-Fit (default)
#   MM=BUDDY  -> Buddy System
#
# Ejemplos:
#   ./compile.sh                # First-Fit, imagen QEMU
#   MM=BUDDY ./compile.sh       # Buddy System, imagen QEMU
#   MM=BUDDY ./compile.sh vbox  # Buddy System, imagen VirtualBox

NAME="TP_SO_2"
TARGET_ARG="${1:-${TARGET_ARG:-qemu}}"
MM_ARG="${MM:-FF}"

# Validación de TARGET (evita propagar valores raros al Image/Makefile).
case "${TARGET_ARG}" in
	qemu|vbox|usb) ;;
	buddy|BUDDY|ff|FF)
		echo "Error: '${TARGET_ARG}' no es un TARGET, es un memory manager." >&2
		echo "Use: MM=${TARGET_ARG^^} ./compile.sh" >&2
		exit 1
		;;
	*)
		echo "Error: TARGET desconocido '${TARGET_ARG}'. Opciones: qemu, vbox, usb." >&2
		echo "Para cambiar el memory manager use: MM=BUDDY ./compile.sh" >&2
		exit 1
		;;
esac

# Validación de MM.
case "${MM_ARG}" in
	FF|BUDDY) ;;
	*)
		echo "Error: MM desconocido '${MM_ARG}'. Opciones: FF, BUDDY." >&2
		exit 1
		;;
esac

if ! command -v docker >/dev/null 2>&1; then
	echo "Docker no está disponible. Instale Docker y vuelva a intentarlo." >&2
	exit 1
fi

if ! docker ps -a --format '{{.Names}}' | grep -qx "${NAME}"; then
	echo "Contenedor ${NAME} no encontrado. Ejecute create.sh para crearlo." >&2
	exit 1
fi

# Verificar que el contenedor monte el directorio actual en /root. Si el
# contenedor se creó desde otro PWD, los cambios locales no son visibles
# adentro y el build usaría una versión obsoleta del código sin avisar.
CONTAINER_MOUNT="$(docker inspect "${NAME}" \
	--format '{{range .Mounts}}{{if eq .Destination "/root"}}{{.Source}}{{end}}{{end}}')"
# En Docker Desktop (Windows/WSL), PWD puede ser un path de bind-mount
# diferente al path Windows usado al crear el contenedor. Verificamos que
# ambos apunten al mismo directorio comparando el contenido de /root.
MOUNT_OK=false
if [ "${CONTAINER_MOUNT}" = "${PWD}" ]; then
	MOUNT_OK=true
else
	# Verificación alternativa: comparar un archivo marca dentro del contenedor.
	MARKER=".compile_mount_check_$$"
	touch "${MARKER}" 2>/dev/null || true
	if docker exec -u root "${NAME}" test -f "/root/${MARKER}" 2>/dev/null; then
		MOUNT_OK=true
		rm -f "${MARKER}"
	fi
	rm -f "${MARKER}"
fi
if [ "${MOUNT_OK}" != "true" ]; then
	echo "Error: el contenedor '${NAME}' monta '${CONTAINER_MOUNT}' en /root," >&2
	echo "pero el directorio actual es '${PWD}'." >&2
	echo "Los cambios locales no serán visibles para el contenedor." >&2
	echo "Recree el contenedor desde el directorio correcto:" >&2
	echo "  docker rm -f ${NAME} && ./create.sh" >&2
	exit 1
fi

echo "Arrancando (si hace falta) el contenedor ${NAME}..."
docker start "${NAME}" >/dev/null

echo "Compilando dentro del contenedor (${NAME}) con TARGET='${TARGET_ARG}' MM='${MM_ARG}'..."

# Pasamos TARGET y MM como variables de entorno; el Makefile raíz las exporta
# a todos los sub-makes (Kernel, Image, etc.). `make clean all` corre clean y
# luego all en una sola invocación, respetando el orden de dependencias.
docker exec -u root \
	-e TARGET="${TARGET_ARG}" \
	-e MM="${MM_ARG}" \
	"${NAME}" \
	make -C /root clean all

# El contenedor corre como root, así que los archivos generados (imagen,
# .o, .bin) quedan root:root. Sin esto, ./run.sh falla con "permission
# denied" al abrir la imagen.
echo "Restaurando ownership al usuario host..."
docker exec -u root "${NAME}" \
	chown -R "$(id -u):$(id -g)" /root

echo "Compilación finalizada. Memory manager: ${MM_ARG}."
