#!/bin/bash
set -euo pipefail

# Crea (si hace falta) y arranca un contenedor Docker de desarrollo.
# Monta el directorio actual en /root dentro del contenedor.

IMAGE="agodio/itba-so-multiarch:3.1"
NAME="TP_SO_2"

if ! command -v docker >/dev/null 2>&1; then
	echo "Docker no está instalado o no está en PATH. Instale Docker y vuelva a intentarlo."
	exit 1
fi

if ! docker info >/dev/null 2>&1; then
	echo "Docker está instalado, pero el daemon no responde."
	echo "Inicie Docker Desktop o el servicio Docker y vuelva a ejecutar ./create.sh."
	exit 1
fi

echo "Pull de la imagen ${IMAGE}..."
docker pull "${IMAGE}"

if docker ps -a --format '{{.Names}}' | grep -qx "${NAME}"; then
	echo "Contenedor '${NAME}' ya existe."
	if [ "$(docker inspect -f '{{.State.Running}}' ${NAME})" != "true" ]; then
		echo "Arrancando contenedor '${NAME}'..."
		docker start "${NAME}"
	else
		echo "Contenedor '${NAME}' ya está en ejecución."
	fi
else
	echo "Creando y arrancando contenedor '${NAME}'..."
	# Ejecutamos detached y dejamos un proceso dormido para mantenerlo vivo.
	docker run -d \
		--name "${NAME}" \
		--security-opt seccomp:unconfined \
		-v "${PWD}":/root \
		-w /root \
		"${IMAGE}" tail -f /dev/null
	echo "Contenedor creado. Use 'docker exec -it ${NAME} bash' para entrar."
fi

echo "Listo. Puede entrar al contenedor con: docker exec -it ${NAME} bash"

