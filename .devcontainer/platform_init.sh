#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

# This runs on the *host* (before the container is created).
# Generate docker-compose override(s) for camera/device passthrough.

TARGET_SERVICE_KIND="${1:-opk-dev}"
OPK_PICAMERA="${2:-disabled}"
OPK_WEBRTC_TURN="${3:-disabled}"
TARGET_CONTAINER_KIND="${4:-devcont}"
OPK_BUILD_BASE_IMAGE="${OPK_BUILD_BASE_IMAGE:-${OPK_DEV_CONTAINER_NAME:-open-perception-kit}-build-base}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT_DIR"

if [[ "${OPK_WEBRTC_TURN}" == auto ]]; then
    OPK_WEBRTC_TURN="$(bash scripts/private/select-webrtc-turn-mode.sh)"
fi

case "${OPK_PICAMERA}" in
    enabled | disabled) ;;
    *)
        echo "Error: OPK_PICAMERA must be 'enabled' or 'disabled'." >&2
        exit 2
        ;;
esac

case "${OPK_WEBRTC_TURN}" in
    enabled | disabled) ;;
    *)
        echo "Error: OPK_WEBRTC_TURN must be 'enabled' or 'disabled'." >&2
        exit 2
        ;;
esac

if [[ ! "${TARGET_CONTAINER_KIND}" =~ ^[a-zA-Z0-9_-]+$ ]]; then
    echo "Error: container kind contains unsupported characters." >&2
    exit 2
fi

upsert_env_value() {
    local file="$1"
    local key="$2"
    local value="$3"

    touch "$file"
    if grep -q "^${key}=" "$file"; then
        sed -i.bak "s|^${key}=.*|${key}=${value}|" "$file"
        rm -f "${file}.bak"
    else
        printf "%s=%s\n" "$key" "$value" >> "$file"
    fi
}

remove_env_value() {
    local file="$1"
    local key="$2"

    [[ -f "$file" ]] || return
    sed -i.bak "/^${key}=/d" "$file"
    rm -f "${file}.bak"
}

if [[ "${TARGET_CONTAINER_KIND}" == devcont ]]; then
    # Keep VS Code from reusing another checkout's container and /work mount.
    workspace_id="$(printf '%s' "${ROOT_DIR}" | git hash-object --stdin | cut -c1-12)"
    workspace_project="${COMPOSE_PROJECT_NAME:-opk-dev}-${workspace_id}"
    workspace_container="${OPK_DEV_CONTAINER_NAME:-opk-dev}-${workspace_id}"
    printf 'name: %s\nservices:\n  %s:\n    container_name: %s\n' \
        "${workspace_project}" "${TARGET_SERVICE_KIND}" "${workspace_container}" > \
        ".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.workspace.yaml"
    if [[ "${TARGET_SERVICE_KIND}" == opk-dev && "${OPK_WEBRTC_TURN}" == enabled && "${5:-}" == stop-other-devcontainers ]]; then
        printf '    restart: "no"\n  coturn:\n    restart: "no"\n' >> \
            ".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.workspace.yaml"
    fi

    if [[ "${5:-}" == stop-other-devcontainers ]]; then
        # Checkouts have distinct names but share the same host ports.
        running_containers="$(docker ps --filter label=com.docker.compose.project \
            --format '{{.ID}} {{.Label "com.docker.compose.project"}} {{.Label "com.docker.compose.service"}} {{.Label "com.docker.compose.project.config_files"}}')"
        while read -r container_id project service config_files; do
            [[ -n "${container_id}" && "${project}" != "${workspace_project}" ]] || continue
            case "${service}" in
                opk-dev | coturn) ;;
                *) continue ;;
            esac
            case ",${config_files}," in
                *"/.devcontainer/docker-compose.rich.yaml,"*) continue ;;
                *"/.devcontainer/compose.devcont.yaml,"*) ;;
                *) continue ;;
            esac
            # The project directory is unambiguous even when paths contain commas.
            project_directory="$(docker inspect --format '{{index .Config.Labels "com.docker.compose.project.working_dir"}}' "${container_id}")"
            if [[ "${project_directory}" -ef "${ROOT_DIR}/.devcontainer" || "${project_directory}" -ef "${ROOT_DIR}" ]]; then
                continue
            fi
            echo "Stopping previous OPK development service ${project}/${service} to release host ports."
            docker stop "${container_id}"
        done <<< "${running_containers}"
    fi
fi

chmod +x scripts/private/dev-init.sh || true
touch devices.env
bash ./scripts/private/dev-init.sh \
    "${TARGET_SERVICE_KIND}" "${TARGET_CONTAINER_KIND}" devices.env

NETWORK_OVERRIDE=".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.network.yaml"
TURN_OVERRIDE=".devcontainer/docker-compose.${TARGET_CONTAINER_KIND}.turn.yaml"
if [[ "${OPK_WEBRTC_TURN}" = enabled ]]; then
    cp "${TURN_OVERRIDE}" "${NETWORK_OVERRIDE}"
else
    printf "services:\n  %s: {}\n" "${TARGET_SERVICE_KIND}" > "${NETWORK_OVERRIDE}"
fi

upsert_env_value .env OPK_PICAMERA "$OPK_PICAMERA"
upsert_env_value .devcontainer/.env OPK_PICAMERA "$OPK_PICAMERA"
upsert_env_value devices.env OPK_PICAMERA "$OPK_PICAMERA"
upsert_env_value .env OPK_BUILD_BASE_IMAGE "$OPK_BUILD_BASE_IMAGE"
upsert_env_value .devcontainer/.env OPK_BUILD_BASE_IMAGE "$OPK_BUILD_BASE_IMAGE"
upsert_env_value devices.env OPK_BUILD_BASE_IMAGE "$OPK_BUILD_BASE_IMAGE"
# Compose build secrets require the source variable to exist. Keep only an empty
# interpolation fallback on disk; an exported host HF_TOKEN overrides it.
upsert_env_value .env HF_TOKEN ""
upsert_env_value .devcontainer/.env HF_TOKEN ""
HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST:-$(scripts/private/generate-hf-download-cachebust.sh)}"
upsert_env_value .env HF_DOWNLOAD_CACHEBUST "$HF_DOWNLOAD_CACHEBUST"
upsert_env_value .devcontainer/.env HF_DOWNLOAD_CACHEBUST "$HF_DOWNLOAD_CACHEBUST"

if [[ "${OPK_WEBRTC_TURN}" = enabled ]]; then
    chmod +x scripts/private/detect-webrtc-host-ip.sh || true
    WEBRTC_HOST_IP="${WEBRTC_HOST_IP:-"$(./scripts/private/detect-webrtc-host-ip.sh)"}"
    OPK_WEBRTC_TURN_MIN_PORT="${OPK_WEBRTC_TURN_MIN_PORT:-49000}"
    OPK_WEBRTC_TURN_MAX_PORT="${OPK_WEBRTC_TURN_MAX_PORT:-49050}"

    for env_file in .env .devcontainer/.env devices.env; do
        upsert_env_value "$env_file" WEBRTC_HOST_IP "$WEBRTC_HOST_IP"
        upsert_env_value "$env_file" OPK_WEBRTC_TURN_MIN_PORT "$OPK_WEBRTC_TURN_MIN_PORT"
        upsert_env_value "$env_file" OPK_WEBRTC_TURN_MAX_PORT "$OPK_WEBRTC_TURN_MAX_PORT"
    done

    echo "Using WebRTC host IP: ${WEBRTC_HOST_IP}"
    echo "Using WebRTC TURN relay ports: ${OPK_WEBRTC_TURN_MIN_PORT}-${OPK_WEBRTC_TURN_MAX_PORT}"

    if [[ "${TARGET_SERVICE_KIND}" == opk-dev && "${TARGET_CONTAINER_KIND}" == devcont && "${5:-}" == stop-other-devcontainers ]]; then
        # VS Code can reuse running containers or start stopped ones without recreating them.
        workspace_containers="$(docker ps --all --filter "label=com.docker.compose.project=${workspace_project}" \
            --filter label=com.docker.compose.oneoff=False \
            --format '{{.ID}} {{.Label "com.docker.compose.service"}} {{.Label "com.docker.compose.project.config_files"}}')"
        container_ids=()
        restart_ids=()
        dev_container=""
        turn_container=""
        refresh_containers=false
        while read -r container_id service config_files; do
            [[ -n "${container_id}" ]] || continue
            case "${service}" in
                opk-dev | coturn) ;;
                *) continue ;;
            esac
            case ",${config_files}," in
                *"/.devcontainer/docker-compose.rich.yaml,"*) continue ;;
                *"/.devcontainer/compose.devcont.yaml,"*) ;;
                *) continue ;;
            esac
            project_directory="$(docker inspect --format '{{index .Config.Labels "com.docker.compose.project.working_dir"}}' "${container_id}")"
            if [[ ! "${project_directory}" -ef "${ROOT_DIR}/.devcontainer" && ! "${project_directory}" -ef "${ROOT_DIR}" ]]; then
                continue
            fi
            container_ids+=("${container_id}")
            if [[ "${service}" == opk-dev ]]; then
                dev_container="${container_id}"
                container_settings="$(docker inspect --format '{{range .Config.Env}}{{println .}}{{end}}' "${container_id}")"
                ip_prefix="WEBRTC_HOST_IP="
            else
                turn_container="${container_id}"
                container_settings="$(docker inspect --format '{{range .Config.Cmd}}{{println .}}{{end}}' "${container_id}")"
                ip_prefix="--external-ip="
            fi
            stored_ip=""
            while IFS= read -r setting; do
                if [[ "${setting}" == "${ip_prefix}"* ]]; then
                    stored_ip="${setting#"${ip_prefix}"}"
                fi
            done <<< "${container_settings}"
            [[ "${stored_ip}" == "${WEBRTC_HOST_IP}" ]] || refresh_containers=true
            restart_policy="$(docker inspect --format '{{.HostConfig.RestartPolicy.Name}}' "${container_id}")"
            if [[ "${restart_policy}" != no ]]; then
                restart_ids+=("${container_id}")
            fi
        done <<< "${workspace_containers}"

        if [[ "${#container_ids[@]}" -gt 0 ]]; then
            if [[ "${refresh_containers}" == true || -z "${dev_container}" || -z "${turn_container}" ]]; then
                echo "Refreshing development containers for WebRTC."
                docker rm -f "${container_ids[@]}" > /dev/null
            else
                if [[ "${#restart_ids[@]}" -gt 0 ]]; then
                    docker update --restart=no "${restart_ids[@]}" > /dev/null
                fi
                dev_running="$(docker inspect --format '{{.State.Running}}' "${dev_container}")"
                if [[ "${dev_running}" == true ]]; then
                    turn_running="$(docker inspect --format '{{.State.Running}}' "${turn_container}")"
                    if [[ "${turn_running}" == false ]]; then
                        echo "Starting TURN relay."
                        docker start "${turn_container}" > /dev/null
                    fi
                fi
            fi
        fi
    fi
else
    for env_file in .env .devcontainer/.env devices.env; do
        remove_env_value "$env_file" WEBRTC_HOST_IP
        remove_env_value "$env_file" OPK_WEBRTC_TURN_MIN_PORT
        remove_env_value "$env_file" OPK_WEBRTC_TURN_MAX_PORT
    done
fi
