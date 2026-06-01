#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
	cat <<'EOF'
Usage:
  gen-shared-memory.sh <service_name> <out_compose_yaml> <out_env>

Generates a docker-compose override that bind-mounts shared-memory / DMA related
paths that are commonly needed by NPUs:
  - /dev/dma_heap (directory)
  - /dev/udmabuf (device node, if present)
  - /dev/shm (POSIX shared memory filesystem)

The <out_env> argument is accepted for consistency with other generators but is
not currently modified.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
	usage
	exit 0
fi

SERVICE_NAME="${1:-}"
OUT_COMPOSE="${2:-}"
OUT_ENV="${3:-}"

if [[ -z "$SERVICE_NAME" || -z "$OUT_COMPOSE" || -z "$OUT_ENV" ]]; then
	usage >&2
	exit 2
fi

# Keep signature consistent with other generators.
touch "${OUT_ENV}"

DMA_HEAP_DIR="/dev/dma_heap"
UDMABUF_DEV="/dev/udmabuf"
SHM_DIR="/dev/shm"

VOLUMES=()

if [[ -d "${DMA_HEAP_DIR}" ]]; then
	VOLUMES+=("${DMA_HEAP_DIR}:${DMA_HEAP_DIR}")
fi

if [[ -e "${UDMABUF_DEV}" ]]; then
	VOLUMES+=("${UDMABUF_DEV}:${UDMABUF_DEV}")
fi

if [[ -d "${SHM_DIR}" ]]; then
	VOLUMES+=("${SHM_DIR}:${SHM_DIR}")
fi

{
	echo "services:"
	echo "  ${SERVICE_NAME}:"

	if [[ ${#VOLUMES[@]} -eq 0 ]]; then
		echo "    volumes: []"
		echo "No shared-memory related paths found. Generated empty shared_memory override." >&2
	else
		echo "    volumes:"
		for SPEC in "${VOLUMES[@]}"; do
			echo "      - ${SPEC}"
		done
		echo "Generated shared_memory mounts: ${VOLUMES[*]}" >&2
	fi
} >"${OUT_COMPOSE}"
