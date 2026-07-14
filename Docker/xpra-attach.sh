#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PORT=${XPRA_PORT:-}
XPRA_USERNAME=${XPRA_USERNAME:-emuleai}

if command -v docker >/dev/null 2>&1 && docker inspect emuleai >/dev/null 2>&1; then
	if [ -z "${PORT}" ]; then
		PORT=$(docker inspect --format '{{range .Config.Env}}{{println .}}{{end}}' emuleai | sed -n 's/^XPRA_PORT=//p' | head -n 1)
	fi
	if [ -n "${EMULEAI_DATA_DIR:-}" ]; then
		DATA_DIR=${EMULEAI_DATA_DIR}
	else
		DATA_DIR=$(docker inspect --format '{{range .Mounts}}{{if eq .Destination "/data"}}{{.Source}}{{end}}{{end}}' emuleai)
	fi
else
	DATA_DIR=${EMULEAI_DATA_DIR:-"${SCRIPT_DIR}/data/eMuleAI"}
fi

PORT=${PORT:-14500}
if [ -z "${DATA_DIR}" ]; then
	echo "ERROR: The host directory mounted at /data could not be determined." >&2
	exit 2
fi

PASSWORD_FILE="${DATA_DIR}/xpra/password.txt"
XPRA_DESKTOP_SCALING="${XPRA_DESKTOP_SCALING:-off}"

if ! command -v xpra >/dev/null 2>&1; then
	echo "ERROR: The Xpra client is not installed or is not in PATH." >&2
	exit 2
fi
if [ ! -s "${PASSWORD_FILE}" ]; then
	echo "ERROR: Xpra password file was not found: ${PASSWORD_FILE}" >&2
	echo "Start the container with DISPLAY_MODE=xpra first." >&2
	exit 2
fi

XPRA_PASSWORD=$(cat "${PASSWORD_FILE}")
export XPRA_PASSWORD

port_ready=0
attempt=1
while [ "${attempt}" -le 60 ]; do
	if command -v nc >/dev/null 2>&1; then
		if nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
			port_ready=1
			break
		fi
	elif ( exec 3<>"/dev/tcp/127.0.0.1/${PORT}" ) >/dev/null 2>&1; then
		port_ready=1
		break
	fi
	sleep 1
	attempt=$((attempt + 1))
done
if [ "${port_ready}" -ne 1 ]; then
	echo "ERROR: Xpra did not start listening on 127.0.0.1:${PORT} within 60 seconds." >&2
	exit 2
fi

exec xpra attach "tcp://${XPRA_USERNAME}@127.0.0.1:${PORT}/" --desktop-scaling="${XPRA_DESKTOP_SCALING}" --challenge-handlers=env --file-transfer=no --printing=no --open-files=no
