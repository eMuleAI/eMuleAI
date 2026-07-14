#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
CONFIG_FILE="${SCRIPT_DIR}/eMuleAI.conf"

fail() {
	printf '\nERROR: %s\n' "$1" >&2

	exit "${2:-2}"
}

is_supported_config_key() {
	case "$1" in
		IMAGE_SOURCE|REGISTRY_IMAGE|REGISTRY_TAG|LOCAL_IMAGE|LOCAL_TAG|\
		CLEAN_OLD_IMAGES|CONTAINER_NAME|PLATFORM|DISPLAY_MODE|DATA_DIR_WINDOWS|\
		DATA_DIR_LINUX|DATA_DIR_MACOS|PUID_WINDOWS|PGID_WINDOWS|PUID_LINUX|\
		PGID_LINUX|PUID_MACOS|PGID_MACOS|DISPLAY_BIND_ADDRESS|DISPLAY_CONNECT_ADDRESS|\
		EMULE_BIND_ADDRESS|XPRA_PORT|NOVNC_PORT|EMULE_TCP_PORT|EMULE_UDP_PORT|\
		DISPLAY_WIDTH|DISPLAY_HEIGHT|XPRA_CANVAS_WIDTH|XPRA_CANVAS_HEIGHT|DISPLAY_DEPTH|\
		XPRA_PASSWORD|XPRA_USERNAME|AUTO_ATTACH_XPRA|XPRA_CLIENT_EXE_WINDOWS|XPRA_CLIENT_EXE_LINUX|\
		XPRA_CLIENT_EXE_MACOS|XPRA_CLIENT_BACKEND_WINDOWS|XPRA_DESKTOP_SCALING|SHM_SIZE|RESTART_POLICY ) return 0 ;;
		*) return 1 ;;
	esac
}


load_config() {
	[ -f "${CONFIG_FILE}" ] || fail "Configuration file was not found: ${CONFIG_FILE}"
	local line key value line_number=0
	while IFS= read -r line || [ -n "${line}" ]; do
		line_number=$((line_number + 1))
		line="${line%$'\r'}"
		case "${line}" in
			''|'#'*) continue ;;
		esac
		case "${line}" in
			*=*) ;;
			*) fail "Invalid configuration line ${line_number} in ${CONFIG_FILE}. Expected KEY=value." ;;
		esac
		key="${line%%=*}"
		value="${line#*=}"
		case "${key}" in
			''|*[!A-Z0-9_]*) fail "Invalid configuration key '${key}' on line ${line_number}." ;;
		esac
		is_supported_config_key "${key}" || fail "Unknown configuration key '${key}' on line ${line_number}."
		printf -v "${key}" '%s' "${value}"
	done < "${CONFIG_FILE}"
}

require_config_value() {
	local key="$1"
	local value
	eval "value=\${${key}-}"
	[ -n "${value}" ] || fail "Configuration key ${key} is missing or empty."
}

expand_home_path() {
	case "$1" in
		'$HOME') printf '%s\n' "${HOME}" ;;
		'$HOME/'*) printf '%s/%s\n' "${HOME}" "${1#\$HOME/}" ;;
		'${HOME}') printf '%s\n' "${HOME}" ;;
		'${HOME}/'*) printf '%s/%s\n' "${HOME}" "${1#\$\{HOME\}/}" ;;
		'~/') printf '%s\n' "${HOME}" ;;
		'~/'*) printf '%s/%s\n' "${HOME}" "${1#~/}" ;;
		*) printf '%s\n' "$1" ;;
	esac
}

resolve_id() {
	local configured="$1"
	local automatic="$2"
	if [ "$(printf '%s' "${configured}" | tr '[:upper:]' '[:lower:]')" = "auto" ]; then
		printf '%s\n' "${automatic}"
	else
		printf '%s\n' "${configured}"
	fi
}

find_xpra_client() {
	if [ -n "${XPRA_CLIENT_EXE}" ]; then
		printf '%s\n' "${XPRA_CLIENT_EXE}"
	elif command -v xpra >/dev/null 2>&1; then
		command -v xpra

	fi
}

tcp_port_is_ready() {
	local host="$1"
	local port="$2"
	if command -v nc >/dev/null 2>&1; then
		nc -z "${host}" "${port}" >/dev/null 2>&1
	else
		( exec 3<>"/dev/tcp/${host}/${port}" ) >/dev/null 2>&1
	fi
}

wait_for_tcp_port() {
	local host="$1"
	local port="$2"
	local description="$3"
	local attempt=1
	while [ "${attempt}" -le 60 ]; do
		if tcp_port_is_ready "${host}" "${port}"; then
			return 0
		fi
		sleep 1
		attempt=$((attempt + 1))
	done
	printf 'ERROR: %s did not start listening on %s:%s within 60 seconds.\n' "${description}" "${host}" "${port}" >&2
	docker logs "${CONTAINER_NAME}" >&2 || true
	return 1
}

attach_xpra() {
	local xpra_client
	xpra_client="$(find_xpra_client)"
	if [ -z "${xpra_client}" ] || [ ! -x "${xpra_client}" ]; then
		printf 'WARNING: Xpra client was not found. Install Xpra or set XPRA_CLIENT_EXE_LINUX.\n' >&2
		return 0
	fi
	printf 'Starting Xpra client directly...\n'
	nohup env XPRA_PASSWORD="${XPRA_PASSWORD}" "${xpra_client}" attach \
		"tcp://${XPRA_USERNAME}@${DISPLAY_CONNECT_ADDRESS}:${XPRA_PORT}/" \
		--desktop-scaling="${XPRA_DESKTOP_SCALING}" \
		--challenge-handlers=env \
		--file-transfer=no \
		--printing=no \
		--open-files=no \
		>"${TMPDIR:-/tmp}/emuleai-xpra-client.log" 2>&1 &
}

is_integer() {
	case "$1" in
		''|*[!0-9]*) return 1 ;;
		*) return 0 ;;
	esac
}

is_id() {
	is_integer "$1" && [ "$1" -le 2147483647 ]
}

is_port() {
	is_integer "$1" && [ "$1" -ge 1 ] && [ "$1" -le 65535 ]
}

is_dimension() {
	is_integer "$1" && [ "$1" -ge 320 ] && [ "$1" -le 8192 ]
}

load_config
for required_key in \
	IMAGE_SOURCE REGISTRY_IMAGE REGISTRY_TAG LOCAL_IMAGE LOCAL_TAG \
	CLEAN_OLD_IMAGES CONTAINER_NAME PLATFORM DISPLAY_MODE DATA_DIR_LINUX \
	PUID_LINUX PGID_LINUX DISPLAY_BIND_ADDRESS DISPLAY_CONNECT_ADDRESS EMULE_BIND_ADDRESS \
	XPRA_PORT NOVNC_PORT EMULE_TCP_PORT EMULE_UDP_PORT DISPLAY_WIDTH \
	DISPLAY_HEIGHT XPRA_CANVAS_WIDTH XPRA_CANVAS_HEIGHT DISPLAY_DEPTH XPRA_PASSWORD \
	XPRA_USERNAME AUTO_ATTACH_XPRA XPRA_DESKTOP_SCALING SHM_SIZE RESTART_POLICY; do
	require_config_value "${required_key}"
done

IMAGE_SOURCE="$(printf '%s' "${IMAGE_SOURCE}" | tr '[:upper:]' '[:lower:]')"
DISPLAY_MODE="$(printf '%s' "${DISPLAY_MODE}" | tr '[:upper:]' '[:lower:]')"
CLEAN_OLD_IMAGES="$(printf '%s' "${CLEAN_OLD_IMAGES}" | tr '[:upper:]' '[:lower:]')"
AUTO_ATTACH_XPRA="$(printf '%s' "${AUTO_ATTACH_XPRA}" | tr '[:upper:]' '[:lower:]')"
XPRA_DESKTOP_SCALING="$(printf '%s' "${XPRA_DESKTOP_SCALING}" | tr '[:upper:]' '[:lower:]')"

DATA_DIR="$(expand_home_path "${DATA_DIR_LINUX}")"
PUID="$(resolve_id "${PUID_LINUX}" "$(id -u)")"
PGID="$(resolve_id "${PGID_LINUX}" "$(id -g)")"
XPRA_CLIENT_EXE="$(expand_home_path "${XPRA_CLIENT_EXE_LINUX:-}")"


case "${IMAGE_SOURCE}" in
	registry)
		DOCKER_IMAGE="${REGISTRY_IMAGE}"
		DOCKER_TAG="${REGISTRY_TAG}"
		;;
	local)
		DOCKER_IMAGE="${LOCAL_IMAGE}"
		DOCKER_TAG="${LOCAL_TAG}"
		;;
	*) fail "IMAGE_SOURCE must be registry or local." ;;
esac
IMAGE_REFERENCE="${DOCKER_IMAGE}:${DOCKER_TAG}"

case "${DISPLAY_MODE}" in
	xpra|novnc) ;;
	*) fail "DISPLAY_MODE must be xpra or novnc." ;;
esac
case "${CLEAN_OLD_IMAGES}" in
	yes|no) ;;
	*) fail "CLEAN_OLD_IMAGES must be yes or no." ;;
esac
case "${AUTO_ATTACH_XPRA}" in
	yes|no) ;;
	*) fail "AUTO_ATTACH_XPRA must be yes or no." ;;
esac
case "${XPRA_DESKTOP_SCALING}" in
	off|on|auto) ;;
	*) fail "XPRA_DESKTOP_SCALING must be off, on, or auto." ;;
esac


is_port "${XPRA_PORT}" || fail "XPRA_PORT must be between 1 and 65535."
is_port "${NOVNC_PORT}" || fail "NOVNC_PORT must be between 1 and 65535."
is_port "${EMULE_TCP_PORT}" || fail "EMULE_TCP_PORT must be between 1 and 65535."
is_integer "${EMULE_UDP_PORT}" && [ "${EMULE_UDP_PORT}" -ge 0 ] && [ "${EMULE_UDP_PORT}" -le 65535 ] || fail "EMULE_UDP_PORT must be between 0 and 65535."
is_dimension "${DISPLAY_WIDTH}" || fail "DISPLAY_WIDTH must be between 320 and 8192."
is_dimension "${DISPLAY_HEIGHT}" || fail "DISPLAY_HEIGHT must be between 320 and 8192."
is_dimension "${XPRA_CANVAS_WIDTH}" || fail "XPRA_CANVAS_WIDTH must be between 320 and 8192."
is_dimension "${XPRA_CANVAS_HEIGHT}" || fail "XPRA_CANVAS_HEIGHT must be between 320 and 8192."
[ "${DISPLAY_DEPTH}" = "16" ] || [ "${DISPLAY_DEPTH}" = "24" ] || fail "DISPLAY_DEPTH must be 16 or 24."
is_id "${PUID}" || fail "PUID_LINUX must be auto or an integer between 0 and 2147483647."
is_id "${PGID}" || fail "PGID_LINUX must be auto or an integer between 0 and 2147483647."

command -v docker >/dev/null 2>&1 || fail "Docker CLI was not found."
docker info >/dev/null 2>&1 || fail "Docker engine is not running or is not accessible."
mkdir -p "${DATA_DIR}" || fail "Could not create data directory: ${DATA_DIR}"
DATA_DIR="$(cd "${DATA_DIR}" && pwd -P)"

OLD_IMAGE_IDS="$(docker image ls --all --no-trunc --quiet --filter "reference=${DOCKER_IMAGE}:*" 2>/dev/null | awk '!seen[$0]++')"
BEFORE_IMAGE_ID="$(docker image inspect --format '{{.Id}}' "${IMAGE_REFERENCE}" 2>/dev/null || true)"

if [ "${IMAGE_SOURCE}" = "registry" ]; then
	printf 'Checking registry image: %s\n' "${IMAGE_REFERENCE}"
	docker pull --platform "${PLATFORM}" "${IMAGE_REFERENCE}" || fail "Could not pull ${IMAGE_REFERENCE}."
else
	printf 'Using local image: %s\n' "${IMAGE_REFERENCE}"
	docker image inspect "${IMAGE_REFERENCE}" >/dev/null 2>&1 || fail "Local image ${IMAGE_REFERENCE} was not found. Build it first or change IMAGE_SOURCE to registry."
fi

NEW_IMAGE_ID="$(docker image inspect --format '{{.Id}}' "${IMAGE_REFERENCE}" 2>/dev/null)"
[ -n "${NEW_IMAGE_ID}" ] || fail "Could not determine the selected image ID."
if [ "${IMAGE_SOURCE}" = "registry" ]; then
	if [ -z "${BEFORE_IMAGE_ID}" ]; then
		printf 'Downloaded registry image: %s\n' "${NEW_IMAGE_ID}"
	elif [ "${BEFORE_IMAGE_ID}" = "${NEW_IMAGE_ID}" ]; then
		printf 'The local registry image is already current. Existing layers were reused.\n'
	else
		printf 'Updated registry image: %s -> %s\n' "${BEFORE_IMAGE_ID}" "${NEW_IMAGE_ID}"
	fi
fi

if docker container inspect "${CONTAINER_NAME}" >/dev/null 2>&1; then
	printf 'Replacing existing container: %s\n' "${CONTAINER_NAME}"
	docker rm -f "${CONTAINER_NAME}" >/dev/null || fail "Could not remove the existing container."
fi

RUN_ARGS=(
	run -d
	--name "${CONTAINER_NAME}"
	--platform "${PLATFORM}"
	--restart "${RESTART_POLICY}"
	--shm-size "${SHM_SIZE}"
	--mount "type=bind,source=${DATA_DIR},target=/data"
	--env "PUID=${PUID}"
	--env "PGID=${PGID}"
	--env "DISPLAY_MODE=${DISPLAY_MODE}"
	--env "DISPLAY_WIDTH=${DISPLAY_WIDTH}"
	--env "DISPLAY_HEIGHT=${DISPLAY_HEIGHT}"
	--env "XPRA_CANVAS_WIDTH=${XPRA_CANVAS_WIDTH}"
	--env "XPRA_CANVAS_HEIGHT=${XPRA_CANVAS_HEIGHT}"
	--env "DISPLAY_DEPTH=${DISPLAY_DEPTH}"
	--env "XPRA_PORT=${XPRA_PORT}"
	--env "NOVNC_PORT=${NOVNC_PORT}"
	--env "XPRA_PASSWORD=${XPRA_PASSWORD}"
	--env "EMULE_TCP_PORT=${EMULE_TCP_PORT}"
	--env "EMULE_UDP_PORT=${EMULE_UDP_PORT}"
)

if [ "${DISPLAY_MODE}" = "xpra" ]; then
	RUN_ARGS+=(--publish "${DISPLAY_BIND_ADDRESS}:${XPRA_PORT}:${XPRA_PORT}/tcp")
	DISPLAY_PORT="${XPRA_PORT}"
	DISPLAY_DESCRIPTION="Xpra"
else
	RUN_ARGS+=(--publish "${DISPLAY_BIND_ADDRESS}:${NOVNC_PORT}:${NOVNC_PORT}/tcp")
	DISPLAY_PORT="${NOVNC_PORT}"
	DISPLAY_DESCRIPTION="noVNC"
fi
RUN_ARGS+=(--publish "${EMULE_BIND_ADDRESS}:${EMULE_TCP_PORT}:${EMULE_TCP_PORT}/tcp")
if [ "${EMULE_UDP_PORT}" -ne 0 ]; then
	RUN_ARGS+=(--publish "${EMULE_BIND_ADDRESS}:${EMULE_UDP_PORT}:${EMULE_UDP_PORT}/udp")
fi
RUN_ARGS+=("${IMAGE_REFERENCE}")

docker "${RUN_ARGS[@]}" >/dev/null || fail "Container could not be created."

RUNNING="false"
attempt=1
while [ "${attempt}" -le 10 ]; do
	RUNNING="$(docker inspect --format '{{.State.Running}}' "${CONTAINER_NAME}" 2>/dev/null || true)"
	[ "${RUNNING}" = "true" ] && break
	sleep 1
	attempt=$((attempt + 1))
done
if [ "${RUNNING}" != "true" ]; then
	docker logs "${CONTAINER_NAME}" 2>&1 || true
	fail "Container stopped during startup."
fi
wait_for_tcp_port "${DISPLAY_CONNECT_ADDRESS}" "${DISPLAY_PORT}" "${DISPLAY_DESCRIPTION}" || fail "The container is running but its display service is unavailable."

if [ "${CLEAN_OLD_IMAGES}" = "yes" ]; then
	while IFS= read -r image_ref; do
		[ -n "${image_ref}" ] || continue
		if [ "${image_ref}" != "${IMAGE_REFERENCE}" ] && [ "${image_ref}" != "<none>:<none>" ]; then
			docker image rm "${image_ref}" >/dev/null 2>&1 || true
		fi
	done <<EOF_IMAGES
$(docker image ls --filter "reference=${DOCKER_IMAGE}:*" --format '{{.Repository}}:{{.Tag}}' 2>/dev/null)
EOF_IMAGES

	for old_image_id in ${OLD_IMAGE_IDS}; do
		if [ "${old_image_id}" != "${NEW_IMAGE_ID}" ]; then
			docker image rm "${old_image_id}" >/dev/null 2>&1 || true
		fi
	done
fi

printf '\neMule AI is running.\n'
printf 'Container:    %s\n' "${CONTAINER_NAME}"
printf 'Image source: %s\n' "${IMAGE_SOURCE}"
printf 'Image:        %s\n' "${IMAGE_REFERENCE}"
printf 'Image ID:     %s\n' "${NEW_IMAGE_ID}"
printf 'Data:         %s\n' "${DATA_DIR}"
printf 'eMule TCP:    %s:%s\n' "${EMULE_BIND_ADDRESS}" "${EMULE_TCP_PORT}"
if [ "${EMULE_UDP_PORT}" -ne 0 ]; then
	printf 'eMule UDP:    %s:%s\n' "${EMULE_BIND_ADDRESS}" "${EMULE_UDP_PORT}"
else
	printf 'eMule UDP:    disabled\n'
fi
if [ "${DISPLAY_MODE}" = "xpra" ]; then
	printf 'Xpra canvas:  %sx%sx%s\n' "${XPRA_CANVAS_WIDTH}" "${XPRA_CANVAS_HEIGHT}" "${DISPLAY_DEPTH}"
	printf 'Xpra:         tcp://%s:%s\n' "${DISPLAY_CONNECT_ADDRESS}" "${XPRA_PORT}"
	printf 'Password:     %s\n' "${XPRA_PASSWORD}"
	if [ "${AUTO_ATTACH_XPRA}" = "yes" ]; then
		attach_xpra
	fi
else
	printf 'noVNC:        http://%s:%s/vnc.html?autoconnect=1&resize=scale\n' "${DISPLAY_CONNECT_ADDRESS}" "${NOVNC_PORT}"
fi
